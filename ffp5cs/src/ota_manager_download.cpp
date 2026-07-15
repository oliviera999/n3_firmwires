// ota_manager_download.cpp — Méthodes de téléchargement/flash OTAManager
// (metadata, firmware modern/fallback/ultra, filesystem, updateTask). Extrait de
// ota_manager.cpp (audit optimisation v13.93) : définitions de méthodes membres
// réparties sur plusieurs TU, classe et comportement inchangés.
#include "ota_manager.h"
#include "nvs_manager.h" // v11.109
#include "nvs_keys.h"
#include <WiFi.h>
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <Update.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <ArduinoJson.h>
#include <esp_ota_ops.h>
#include <esp_partition.h>
#include <esp_task_wdt.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <limits.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <algorithm>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <mbedtls/md.h>
#include <mbedtls/base64.h>
#include <mbedtls/pk.h>
#include "ota_signing_pubkey.h"  // OTA_SIGNING_PUBLIC_KEY_PEM (v14.17)
#include "config.h"
#include "mailer.h"
#include "automatism.h"
#include "diagnostics.h"
#include "task_monitor.h"
#include "tls_mutex.h"
#include "display_view.h"

bool OTAManager::downloadMetadata(char* payload, size_t payloadSize) {
    log("🔍 Début de la vérification des mises à jour...");
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT avant GET métadonnées (TLS peut bloquer > 30s)
    }
    HTTPClient http;
    char metadataUrl[256];
    OTAConfig::getMetadataUrl(metadataUrl, sizeof(metadataUrl));
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "📡 URL métadonnées: %s", metadataUrl);
    log(logMsg);

    if (!http.begin(metadataUrl)) {
        logError("Échec initialisation HTTPClient");
        return false;
    }

    http.setTimeout(OTAConfig::HTTP_TIMEOUT);
    snprintf(logMsg, sizeof(logMsg), "⏱️ Timeout HTTP: %d ms", OTAConfig::HTTP_TIMEOUT);
    log(logMsg);

    const int MAX_METADATA_RETRIES = 2;  // 1 tentative initiale + 2 retries
    int code = -1;
    for (int attempt = 0; attempt <= MAX_METADATA_RETRIES; attempt++) {
        if (attempt > 0) {
            log("🔄 Retry GET metadata...");
            vTaskDelay(pdMS_TO_TICKS(1000));  // 1s entre tentatives
        }
        code = http.GET();
        if (esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();
        }
        snprintf(logMsg, sizeof(logMsg), "📡 Code de réponse HTTP: %d (tentative %d)", code, attempt + 1);
        log(logMsg);
        if (code == HTTP_CODE_OK) break;
        // Retry sur erreurs temporaires : 5xx, -1 (échec connexion), 0 (timeout)
        if (attempt < MAX_METADATA_RETRIES && (code >= 500 || code == -1 || code == 0)) {
            continue;
        }
        break;
    }

    if (code != HTTP_CODE_OK) {
        snprintf(logMsg, sizeof(logMsg), "Erreur GET métadonnées: %d", code);
        logError(logMsg);
        if (code == -1) {
            log("   (connexion ou timeout: serveur injoignable, DNS, ou délai dépassé)");
        }
        http.end();
        return false;
    }

    // Lecture directe dans le buffer appelant (évite double buffer ~4 Ko sur stack otaTask)
    if (payloadSize < 2) {
        http.end();
        logError("Buffer metadata trop petit");
        return false;
    }
    WiFiClient* stream = http.getStreamPtr();
    size_t payloadLen = 0;
    if (stream) {
      // v11.178: Ajout timeout pour éviter blocage infini (audit bugs-high)
      unsigned long streamStart = millis();
      const unsigned long STREAM_TIMEOUT_MS = 5000;
      while (stream->available() && payloadLen < payloadSize - 1
             && (millis() - streamStart) < STREAM_TIMEOUT_MS) {
        if (esp_task_wdt_status(NULL) == ESP_OK) {
          esp_task_wdt_reset();
        }
        size_t bytesRead = stream->readBytes(payload + payloadLen, payloadSize - payloadLen - 1);
        payloadLen += bytesRead;
      }
      payload[payloadLen] = '\0';
      // Détection troncation : buffer plein et données restantes
      if (payloadLen >= payloadSize - 1 && stream->available() > 0) {
          log("⚠️ Payload metadata tronqué (buffer sortie insuffisant), JSON peut être invalide");
      }
    } else {
      // v11.180: Suppression getString() - cause crashes LoadProhibited dans destructeur String
      log("⚠️ Pas de stream HTTP disponible");
      payload[0] = '\0';
      payloadLen = 0;
    }
    http.end();

    snprintf(logMsg, sizeof(logMsg), "📄 Taille payload: %zu bytes", payloadLen);
    log(logMsg);
    snprintf(logMsg, sizeof(logMsg), "📄 Payload: %s", payload);
    log(logMsg);

    return true;
}

// Nouvelle méthode utilisant esp_http_client pour plus de stabilité
// ---------------------------------------------------------------------------
// C2 — Téléchargement OTA unifié et RÉSUMABLE (HTTP Range)
//
// Remplace l'ancienne cascade downloadFirmwareModern → downloadFirmware →
// downloadFirmwareUltraRevolutionary par UN SEUL chemin (esp_http_client), capable
// de reprendre un téléchargement interrompu via l'en-tête HTTP `Range`.
//
// Stratégie de reprise (serveur n3_serveur/OtaFileController, contrat testé) :
//   • coupure réseau en cours → rouvrir avec `Range: bytes=<octets_écrits>-`
//   • 206 Partial Content  → POURSUIVRE l'écriture dans la MÊME session Update
//                            (le contexte MD5 reste celui du fichier complet 0..EOF)
//   • 200 OK (Range ignoré)→ REDÉMARRAGE PROPRE (Update.abort + begin, ré-écriture depuis 0)
//   • 416 Range invalide   → erreur FATALE
//
// GARDE-FOU MD5 (clé de sûreté) : on ne marque JAMAIS la partition boot si
// Update.end() (qui vérifie le MD5 du flux complet) échoue. Même si la logique de
// reprise était subtilement fausse, un flash corrompu ne sera jamais booté.
//
// ⚠️ VALIDATION BANC REQUISE (cf. BACKLOG §2/§4) : le comportement exact d'Update
// sur reprise 206 (continuité du contexte MD5, alignement de l'offset renvoyé par le
// serveur) DOIT être prouvé sur matériel WROOM **et** S3 avant tout merge.
// ---------------------------------------------------------------------------

// Ouvre une connexion HTTP(S) pour le firmware, avec reprise optionnelle via Range.
// rangeStart>0 ⇒ envoie `Range: bytes=<rangeStart>-`. Retourne le handle (ou nullptr
// sur échec transport) et place le code HTTP de la réponse dans outStatus.
esp_http_client_handle_t OTAManager::openFirmwareConnection(const char* url, size_t rangeStart, int& outStatus) {
    outStatus = -1;
    esp_http_client_config_t config = {};
    config.url = url;
    config.timeout_ms = NetworkConfig::OTA_CONNECT_TIMEOUT_MS;
    config.buffer_size = BufferConfig::HTTP_BUFFER_SIZE;
    config.buffer_size_tx = BufferConfig::HTTP_TX_BUFFER_SIZE;
    // v13.60 (audit sécurité): exiger la validation du CN du certificat HTTPS.
    config.skip_cert_common_name_check = false;
    // Bundle TLS : S3/Arduino 2.x → arduino_esp_crt_bundle_attach ; WROOM Arduino 3.x → esp_crt_bundle_attach.
#if defined(CONFIG_IDF_TARGET_ESP32S3)
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#elif defined(ESP_ARDUINO_VERSION_MAJOR) && (ESP_ARDUINO_VERSION_MAJOR >= 3)
    config.crt_bundle_attach = esp_crt_bundle_attach;
#else
    config.crt_bundle_attach = arduino_esp_crt_bundle_attach;
#endif
    config.disable_auto_redirect = false;
    config.max_redirection_count = 3;
    config.user_agent = NetworkConfig::HTTP_USER_AGENT;

    esp_http_client_handle_t client = esp_http_client_init(&config);
    if (!client) {
        return nullptr;
    }
    esp_http_client_set_header(client, "User-Agent", NetworkConfig::HTTP_USER_AGENT);
    esp_http_client_set_header(client, "Accept", "application/octet-stream");
    esp_http_client_set_header(client, "Connection", "keep-alive");
    if (rangeStart > 0) {
        char rangeHdr[40];
        snprintf(rangeHdr, sizeof(rangeHdr), "bytes=%lu-", (unsigned long)rangeStart);
        esp_http_client_set_header(client, "Range", rangeHdr);
    }
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT avant open (TLS handshake peut bloquer)
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return nullptr;
    }
    if (esp_task_wdt_status(NULL) == ESP_OK) {
        esp_task_wdt_reset();  // Feed WDT après open, avant fetch_headers
    }
    // IMPORTANT : fetch_headers() renvoie la CONTENT-LENGTH (pas le code HTTP). Le code de
    // statut s'obtient via get_status_code() — l'ancien code comparait à tort le retour de
    // fetch_headers à 200 (bug latent corrigé ici).
    esp_http_client_fetch_headers(client);
    outStatus = esp_http_client_get_status_code(client);
    return client;
}

// v14.17 — Vérification d'authenticité du binaire flashé (sha256 + signature ECDSA).
// Relit la partition cible (pas le réseau) pour hacher EXACTEMENT ce qui est en flash,
// puis compare le sha256 au champ metadata et vérifie la signature ECDSA. Appelée APRÈS
// Update.end() (MD5/taille OK) et AVANT esp_ota_set_boot_partition : un binaire non
// authentifié n'est jamais marqué bootable. Memory-light : un seul buffer heap de 1 Ko +
// contextes mbedtls transitoires (libérés sur tous les chemins).
bool OTAManager::verifyFlashedFirmware(const esp_partition_t* partition, size_t firmwareSize) {
    const bool haveSha = (m_firmwareSha256 && strlen(m_firmwareSha256) == 64);
    const bool haveSig = (m_firmwareSignature && strlen(m_firmwareSignature) > 0);

    // Aucun champ d'authenticité : phase de transition (MD5 déjà vérifié par Update.end()).
    if (!haveSha && !haveSig) {
        if (OTAConfig::OTA_REQUIRE_SIGNATURE) {
            logError("Signature OTA obligatoire mais absente des metadata → refus");
            return false;
        }
        log("⚠️ Intégrité étendue absente (sha256/signature) → MD5 seul (déjà validé)");
        return true;
    }

    if (!partition || firmwareSize == 0) {
        logError("Partition/taille invalide pour vérification sha256");
        return false;
    }

    // --- sha256 du binaire flashé (relecture flash par blocs de 1 Ko) ---
    uint8_t* buf = static_cast<uint8_t*>(malloc(1024));
    if (!buf) { logError("OOM buffer vérification sha256"); return false; }

    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (!mdInfo || mbedtls_md_setup(&mdCtx, mdInfo, 0) != 0 || mbedtls_md_starts(&mdCtx) != 0) {
        logError("Init mbedtls sha256 échouée");
        mbedtls_md_free(&mdCtx);
        free(buf);
        return false;
    }

    bool ok = true;
    size_t offset = 0;
    while (offset < firmwareSize) {
        size_t chunk = firmwareSize - offset;
        if (chunk > 1024) chunk = 1024;
        esp_err_t rerr = esp_partition_read(partition, offset, buf, chunk);
        if (rerr != ESP_OK) {
            char m[96];
            snprintf(m, sizeof(m), "Lecture partition échouée @%u: %s",
                     (unsigned)offset, esp_err_to_name(rerr));
            logError(m);
            ok = false;
            break;
        }
        if (mbedtls_md_update(&mdCtx, buf, chunk) != 0) {
            logError("mbedtls_md_update sha256 échec");
            ok = false;
            break;
        }
        offset += chunk;
        if ((offset & 0x3FFFF) == 0 && esp_task_wdt_status(NULL) == ESP_OK) {
            esp_task_wdt_reset();  // feed WDT tous les 256 Ko lus
        }
    }
    free(buf);

    uint8_t digest[32];
    if (ok && mbedtls_md_finish(&mdCtx, digest) != 0) {
        logError("mbedtls_md_finish sha256 échec");
        ok = false;
    }
    mbedtls_md_free(&mdCtx);
    if (!ok) return false;

    char computedHex[65];
    static const char* kHex = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        computedHex[i * 2]     = kHex[(digest[i] >> 4) & 0x0F];
        computedHex[i * 2 + 1] = kHex[digest[i] & 0x0F];
    }
    computedHex[64] = '\0';

    // --- Comparaison sha256 (si fourni dans metadata) ---
    if (haveSha) {
        if (strcasecmp(m_firmwareSha256, computedHex) != 0) {
            char m[160];
            snprintf(m, sizeof(m), "sha256 MISMATCH: metadata=%s calculé=%s",
                     m_firmwareSha256, computedHex);
            logError(m);
            return false;
        }
        log("✅ sha256 du binaire flashé conforme aux metadata");
    }

    // --- Vérification signature ECDSA (si fournie) ---
    if (haveSig) {
        size_t sigMax = strlen(m_firmwareSignature);
        uint8_t* sig = static_cast<uint8_t*>(malloc(sigMax));
        if (!sig) { logError("OOM buffer signature"); return false; }

        size_t sigLen = 0;
        int b64 = mbedtls_base64_decode(sig, sigMax, &sigLen,
                                        reinterpret_cast<const unsigned char*>(m_firmwareSignature), sigMax);
        if (b64 != 0) {
            char m[64];
            snprintf(m, sizeof(m), "Signature base64 invalide (%d)", b64);
            logError(m);
            free(sig);
            return false;
        }

        mbedtls_pk_context pk;
        mbedtls_pk_init(&pk);
        int pr = mbedtls_pk_parse_public_key(
            &pk,
            reinterpret_cast<const unsigned char*>(OTA_SIGNING_PUBLIC_KEY_PEM),
            strlen(OTA_SIGNING_PUBLIC_KEY_PEM) + 1);
        if (pr != 0) {
            char m[64];
            snprintf(m, sizeof(m), "Clé publique OTA invalide (%d)", pr);
            logError(m);
            mbedtls_pk_free(&pk);
            free(sig);
            return false;
        }

        int vr = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, digest, sizeof(digest), sig, sigLen);
        mbedtls_pk_free(&pk);
        free(sig);
        if (vr != 0) {
            char m[64];
            snprintf(m, sizeof(m), "Signature OTA INVALIDE (%d) → refus", vr);
            logError(m);
            return false;
        }
        log("✅ Signature ECDSA du firmware VALIDE (authenticité confirmée)");
    } else if (OTAConfig::OTA_REQUIRE_SIGNATURE) {
        logError("Signature OTA obligatoire mais absente → refus");
        return false;
    }

    return true;
}

bool OTAManager::downloadFirmwareAdaptiveResumable(const char* url, size_t expectedSize) {
    log("📥 OTA: téléchargement adaptatif résumable (HTTP Range)...");

    if (WiFi.status() != WL_CONNECTED) {
        logError("WiFi non connecté pour OTA");
        return false;
    }
    // Partition cible sauvegardée AVANT Update.begin() pour garantir l'alternance.
    const esp_partition_t* target_partition = esp_ota_get_next_update_partition(NULL);
    if (!target_partition) {
        logError("Partition OTA cible introuvable");
        return false;
    }
    char logMsg[128];
    snprintf(logMsg, sizeof(logMsg), "📍 Partition cible: %s (0x%x)", target_partition->label, (unsigned)target_partition->address);
    log(logMsg);

    // Nettoyer un éventuel client résiduel.
    if (m_httpClient) {
        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;
    }

    int contentTotal = 0;        // taille totale attendue du firmware
    bool sizeKnown = false;
    bool updateBegun = false;    // session Update active (MD5 en cours)
    size_t totalWritten = 0;     // octets déjà flashés ET hashés par Update
    uint8_t buffer[1024];
    const unsigned long globalStart = millis();
    unsigned long lastProgress = globalStart;

    for (int attempt = 0; attempt < NetworkConfig::OTA_RESUME_MAX_ATTEMPTS; ++attempt) {
        if (millis() - globalStart > NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS) {
            logError("Timeout global OTA");
            if (updateBegun) Update.abort();
            return false;
        }
        // Backoff exponentiel plafonné entre reprises (la 1re tentative n'attend pas).
        if (attempt > 0) {
            uint32_t backoff = NetworkConfig::OTA_RESUME_BACKOFF_BASE_MS << (attempt - 1);
            if (backoff > NetworkConfig::OTA_RESUME_BACKOFF_MAX_MS) {
                backoff = NetworkConfig::OTA_RESUME_BACKOFF_MAX_MS;
            }
            snprintf(logMsg, sizeof(logMsg), "🔁 Reprise OTA #%d à %lu octets (backoff %lu ms)",
                     attempt, (unsigned long)totalWritten, (unsigned long)backoff);
            log(logMsg);
            for (uint32_t waited = 0; waited < backoff; waited += 200) {
                if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
                vTaskDelay(pdMS_TO_TICKS(200));
            }
        }

        int status = -1;
        m_httpClient = openFirmwareConnection(url, totalWritten, status);
        if (!m_httpClient) {
            logError("Échec ouverture connexion OTA");
            continue;  // retry
        }

        // Interprétation du code HTTP selon l'état de reprise.
        if (totalWritten == 0) {
            // Première écriture : 200 attendu (Range non envoyé).
            if (status != NetworkConfig::HTTP_OK && status != NetworkConfig::HTTP_PARTIAL_CONTENT) {
                snprintf(logMsg, sizeof(logMsg), "HTTP inattendu (init): %d", status);
                logError(logMsg);
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                continue;
            }
        } else {
            if (status == NetworkConfig::HTTP_OK) {
                // Le serveur a ignoré Range → flux complet depuis 0 : redémarrage propre.
                log("⚠️ Serveur a ignoré Range (200) → redémarrage propre du flash");
                if (updateBegun) { Update.abort(); updateBegun = false; }
                totalWritten = 0;
            } else if (status == NetworkConfig::HTTP_RANGE_NOT_SATISFIABLE) {
                logError("Range non satisfiable (416) → abandon");
                if (updateBegun) Update.abort();
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;  // fatal
            } else if (status != NetworkConfig::HTTP_PARTIAL_CONTENT) {
                snprintf(logMsg, sizeof(logMsg), "HTTP inattendu (reprise): %d", status);
                logError(logMsg);
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                continue;
            }
            // 206 : on poursuit dans la même session Update (offset = totalWritten).
        }

        // Déterminer la taille totale au premier en-tête exploitable.
        if (!sizeKnown) {
            int chunkLen = esp_http_client_get_content_length(m_httpClient);
            if (status == NetworkConfig::HTTP_OK && chunkLen > 0) {
                contentTotal = chunkLen;  // 200 → content-length == taille totale
            } else if (expectedSize > 0) {
                contentTotal = (int)expectedSize;  // métadonnée serveur (fallback fiable)
            }
            if (contentTotal <= 0) {
                logError("Taille firmware inconnue → abandon");
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
            sizeKnown = true;
            char sizeBuf[16];
            formatBytes(contentTotal, sizeBuf, sizeof(sizeBuf));
            snprintf(logMsg, sizeof(logMsg), "📊 Taille firmware: %s", sizeBuf);
            log(logMsg);
        }

        // (Ré)initialiser la session Update si nécessaire (1re fois ou après restart 200).
        if (!updateBegun) {
            if (!Update.begin((size_t)contentTotal)) {
                char updErr[64];
                strncpy(updErr, Update.errorString(), sizeof(updErr) - 1);
                updErr[sizeof(updErr) - 1] = '\0';
                snprintf(logMsg, sizeof(logMsg), "Update.begin échec: %s", updErr);
                logError(logMsg);
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
            // MD5 du fichier COMPLET : valide tant qu'on ne ré-initialise pas Update entre
            // deux fragments 206 contigus (cf. garde-fou en fin de fonction).
            if (strlen(m_firmwareMD5) > 0) {
                Update.setMD5(m_firmwareMD5);
                log("🔐 MD5 firmware complet défini");
            }
            updateBegun = true;
        }

        // Boucle de lecture / écriture flash.
        while (totalWritten < (size_t)contentTotal) {
            if (esp_task_wdt_status(NULL) == ESP_OK) esp_task_wdt_reset();
            if (millis() - globalStart > NetworkConfig::OTA_DOWNLOAD_TIMEOUT_MS) {
                logError("Timeout global OTA (lecture)");
                Update.abort();
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
            int bytesRead = esp_http_client_read(m_httpClient, (char*)buffer, sizeof(buffer));
            if (bytesRead < 0) {
                break;  // erreur transport → reprise via Range
            }
            if (bytesRead == 0) {
                break;  // fin de flux (complète ou coupure prématurée) → test post-boucle
            }
            size_t written = Update.write(buffer, (size_t)bytesRead);
            if (written != (size_t)bytesRead) {
                snprintf(logMsg, sizeof(logMsg), "Écriture flash: %lu/%d octets", (unsigned long)written, bytesRead);
                logError(logMsg);
                Update.abort();  // erreur flash = fatale (ne jamais finaliser)
                esp_http_client_cleanup(m_httpClient);
                m_httpClient = nullptr;
                return false;
            }
            totalWritten += written;

            unsigned long now = millis();
            if (now - lastProgress >= TimingConfig::OTA_PROGRESS_UPDATE_INTERVAL_MS) {
                int pct = (int)((totalWritten * 100) / (size_t)contentTotal);
                unsigned long elapsed = (now - globalStart) / 1000;
                float speed = elapsed > 0 ? (totalWritten / 1024.0f) / elapsed : 0;
                logProgress(pct, totalWritten, (size_t)contentTotal, speed);
                lastProgress = now;
            }
        }

        esp_http_client_cleanup(m_httpClient);
        m_httpClient = nullptr;

        if (totalWritten >= (size_t)contentTotal) {
            break;  // téléchargement complet → finalisation
        }
        snprintf(logMsg, sizeof(logMsg), "⚠️ Coupure à %lu/%d octets — tentative de reprise",
                 (unsigned long)totalWritten, contentTotal);
        log(logMsg);
        // boucle for → reprise via Range
    }

    // Tentatives épuisées sans téléchargement complet.
    if (!updateBegun || totalWritten < (size_t)contentTotal) {
        logError("OTA: téléchargement incomplet après reprises");
        if (updateBegun) Update.abort();
        return false;
    }

    logProgress(100, totalWritten, totalWritten, 0);

    // GARDE-FOU MD5 : Update.end() (sans evenIfRemaining) vérifie la taille ET le MD5 du flux
    // complet. En cas d'échec on retourne false SANS marquer la partition boot.
    log("🔧 Finalisation (Update.end + vérification MD5)...");
    if (!Update.end()) {
        char updErr[64];
        strncpy(updErr, Update.errorString(), sizeof(updErr) - 1);
        updErr[sizeof(updErr) - 1] = '\0';
        snprintf(logMsg, sizeof(logMsg), "Update.end échec (MD5/taille?): %s", updErr);
        logError(logMsg);
        return false;
    }
    if (Update.hasError()) {
        logError("Update: erreur résiduelle → abandon (pas de marquage boot)");
        return false;
    }

    // v14.17 — Authenticité (sha256 + signature ECDSA) AVANT tout marquage boot. Un binaire
    // corrompu ou substitué (MITM HTTP) ne sera jamais rendu bootable, même si le MD5 passe.
    if (!verifyFlashedFirmware(target_partition, totalWritten)) {
        logError("Authenticité OTA non vérifiée → partition de boot NON modifiée");
        return false;
    }

    // MD5 + sha256/signature validés : marquer la partition cible comme partition de boot.
    esp_err_t err = esp_ota_set_boot_partition(target_partition);
    if (err != ESP_OK) {
        snprintf(logMsg, sizeof(logMsg), "set_boot_partition échec: %s", esp_err_to_name(err));
        logError(logMsg);
        return false;
    }
    const esp_partition_t* bootNow = esp_ota_get_boot_partition();
    snprintf(logMsg, sizeof(logMsg), "✅ OTA OK (MD5 validé), boot=%s",
             bootNow ? bootNow->label : target_partition->label);
    log(logMsg);
    return true;
}

// Tâche dédiée pour l'OTA
void OTAManager::updateTask(void* parameter) {
    OTAManager* ota = static_cast<OTAManager*>(parameter);
    ota->log("🚀 Démarrage tâche OTA");

    TaskMonitor::Snapshot baselineSnapshot = TaskMonitor::collectSnapshot();
    TaskMonitor::logSnapshot(baselineSnapshot, "ota-task-start");
    TaskMonitor::detectAnomalies(baselineSnapshot, "ota-task-start");
    
    // Diagnostic des partitions AVANT la mise à jour
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(NULL);
    
    ota->log("📊 État des partitions AVANT mise à jour:");
    char logMsgTask[128];
    if (running) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Partition en cours: %s (0x%x)", running->label, running->address);
        ota->log(logMsgTask);
    }
    if (boot) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Partition de boot: %s (0x%x)", boot->label, boot->address);
        ota->log(logMsgTask);
    }
    if (next) {
        snprintf(logMsgTask, sizeof(logMsgTask), "  - Prochaine partition OTA: %s (0x%x)", next->label, next->address);
        ota->log(logMsgTask);
    }
    
    // Email de début d'OTA (serveur distant)
    extern Mailer mailer;
    extern Automatism g_autoCtrl;
    bool emailEnabled = g_autoCtrl.isEmailEnabled();
    const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
    char body[256];
    char sizeBufEmail[16];
    formatBytes(ota->getFirmwareSize(), sizeBufEmail, sizeof(sizeBufEmail));
    snprintf(body, sizeof(body),
             "OTA distant démarré\n\nAncienne version: %s\nNouvelle version: %s\nEnvironnement: %s\nTaille firmware: %s",
             ota->getCurrentVersion(), ota->getRemoteVersion(), Utils::getProfileName(), sizeBufEmail);
    mailer.sendAlert("OTA début - Serveur distant", body, toEmail, true);
    
    // Ajouter cette tâche au TWDT et conserver le watchdog ACTIF pendant l'OTA
    esp_task_wdt_add(NULL);
    esp_task_wdt_reset();  // Démarrer la fenêtre TWDT (évite reset avant premier feed dans download)
    ota->log("🛡️ Watchdog actif pendant OTA (reset périodique)");

    // Persister l'ancienne version pour notification post-reboot
    {
        g_nvsManager.saveString(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_PREV_VER, ota->getCurrentVersion());
        // Clé harmonisée (snake_case)
        g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_IN_PROGRESS, true);
        // Migration: supprimer l'ancienne clé si elle existe
        g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "ota_inProgress");
    }
    
    // C2 : chemin unique résumable (Range). Les reprises/backoff sont gérés en interne ;
    // plus de cascade Modern→Classic→Ultra. Rollback = revert de cette PR (ré-active la cascade).
    bool success = ota->downloadFirmwareAdaptiveResumable(ota->m_firmwareUrl, ota->m_firmwareSize);
    
    // Si le firmware a été mis à jour avec succès, essayer de mettre à jour le filesystem
    if (success) {
        ota->log("✅ Mise à jour firmware réussie, vérification du filesystem...");
        
        // Mise à jour du filesystem si disponible
        if (strlen(ota->m_filesystemUrl) > 0) {
            ota->log("📁 Mise à jour du filesystem en cours...");
            bool filesystemSuccess = ota->downloadFilesystem(ota->m_filesystemUrl, ota->m_filesystemSize, ota->m_filesystemMD5);
            if (filesystemSuccess) {
                ota->log("✅ Mise à jour filesystem réussie");
            } else {
                ota->log("⚠️ Échec mise à jour filesystem, mais firmware mis à jour");
                // On continue même si le filesystem échoue
            }
        } else {
            ota->log("ℹ️ Aucun filesystem à mettre à jour");
        }
    }
    
    if (success) {
        extern Diagnostics diag;
        diag.recordOtaResult(true, nullptr);
        ota->log("🎉 Mise à jour OTA réussie");
        
        // Diagnostic des partitions APRÈS la mise à jour
        const esp_partition_t* new_running = esp_ota_get_running_partition();
        const esp_partition_t* new_boot = esp_ota_get_boot_partition();
        const esp_partition_t* new_next = esp_ota_get_next_update_partition(NULL);
        
        ota->log("📊 État des partitions APRÈS mise à jour:");
        if (new_running) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Partition en cours: %s (0x%x)", new_running->label, new_running->address);
            ota->log(logMsgPart);
        }
        if (new_boot) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Partition de boot (prochaine): %s (0x%x)", new_boot->label, new_boot->address);
            ota->log(logMsgPart);
        }
        if (new_next) {
            char logMsgPart[128];
            snprintf(logMsgPart, sizeof(logMsgPart), "  - Prochaine partition OTA: %s (0x%x)", new_next->label, new_next->address);
            ota->log(logMsgPart);
        }

        // OLED: masquer l'overlay et afficher 100% et partitions avant reboot (via display du contexte)
        if (ota->m_display && ota->m_display->isPresent()) {
            ota->m_display->hideOtaProgressOverlay();
            ota->m_display->lockScreen(2000);
            const esp_partition_t* prev_running = esp_ota_get_running_partition();
            const char* fromLbl = prev_running ? prev_running->label : "?";
            const char* toLbl   = new_boot ? new_boot->label : "?";
            const char* curV = ProjectConfig::VERSION;
            const char* newV = ota->getRemoteVersion();
            ota->m_display->showOtaProgressEx(100, fromLbl, toLbl, "Terminé", curV, newV, "OTA");
        }

        // Email de fin d'OTA (succès) — synchrone avant reboot (sendAlert async perdait le mail au restart)
        {
            extern Mailer mailer;
            extern Automatism g_autoCtrl;
            bool emailEnabled = g_autoCtrl.isEmailEnabled();
            const char* toEmail = emailEnabled ? g_autoCtrl.getEmailAddress() : EmailConfig::DEFAULT_RECIPIENT;
            char firmwareSizeBuf[16];
            formatBytes(ota->getFirmwareSize(), firmwareSizeBuf, sizeof(firmwareSizeBuf));
            char body[512];
            snprintf(body, sizeof(body),
                     "OTA distant terminé\n\nAncienne version: %s\nNouvelle version: %s\nEnvironnement: %s\nTaille firmware: %s",
                     ota->getCurrentVersion(), ota->getRemoteVersion(), Utils::getProfileName(), firmwareSizeBuf);
            esp_task_wdt_reset();
            const bool mailOk = mailer.sendAlertSync("OTA fin - Serveur distant", body, toEmail, true);
            if (!mailOk) {
                ota->log("⚠️ Échec envoi mail OTA fin (reboot quand même)");
            }
        }
        
        // Nettoyer le flag inProgress avant reboot
        {
            // Clé harmonisée (snake_case)
            g_nvsManager.saveBool(NVS_NAMESPACES::SYSTEM, NVSKeys::System::OTA_IN_PROGRESS, false);
            // Migration: supprimer l'ancienne clé si elle existe
            g_nvsManager.removeKey(NVS_NAMESPACES::SYSTEM, "ota_inProgress");
        }
        TaskMonitor::Snapshot successSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(successSnapshot, "ota-task-success");
        TaskMonitor::logDiff(baselineSnapshot, successSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(successSnapshot, "ota-task-success");
        Serial.printf("[Event] OTA success %s -> %s\n",
                       ota->getCurrentVersion(),
                       ota->getRemoteVersion());

        ota->log("🔄 Redémarrage dans 3 secondes...");
        // Utiliser vTaskDelay() avec reset watchdog pour respecter la règle "jamais bloquer > 3s"
        for (int i = 0; i < 6; i++) {
          esp_task_wdt_reset();
          vTaskDelay(pdMS_TO_TICKS(500));
        }
        ESP.restart();
    } else {
        extern Diagnostics diag;
        diag.recordOtaResult(false, "download/update failed");
        ota->log("❌ Échec mise à jour OTA");
        ota->m_otaLock = false;
        TaskMonitor::Snapshot failureSnapshot = TaskMonitor::collectSnapshot();
        TaskMonitor::logSnapshot(failureSnapshot, "ota-task-failure");
        TaskMonitor::logDiff(baselineSnapshot, failureSnapshot, "ota-task");
        TaskMonitor::detectAnomalies(failureSnapshot, "ota-task-failure");
        Serial.printf("[Event] OTA failure %s -> %s\n",
                       ota->getCurrentVersion(),
                       ota->getRemoteVersion());
        
        // Masquer l'overlay OTA en cas d'échec (via display du contexte)
        if (ota->m_display && ota->m_display->isPresent()) {
            ota->m_display->hideOtaProgressOverlay();
        }
    }

    // Libérer le handle AVANT l'auto-suppression : sinon performUpdate() voit
    // m_updateTaskHandle != nullptr et refuse tout OTA ultérieur ("déjà en cours")
    // après un premier échec. (Le chemin succès reboote via ESP.restart() plus haut.)
    ota->m_updateTaskHandle = nullptr;
    vTaskDelete(NULL);
}


