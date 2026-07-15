#include "n3_ota.h"
#include "n3_ota_pubkey.h"
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <Update.h>
#include <esp_ota_ops.h>
#include <ArduinoJson.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <strings.h>
#include <mbedtls/base64.h>
#include <mbedtls/md.h>
#include <mbedtls/pk.h>

// A5 (audit 2026-07-05) : OTA metadata et/ou binaire peuvent etre servis en HTTPS pour fermer la
// fenetre de downgrade MITM (le binaire reste verifie sha256 + ECDSA P-256). Detection ADDITIVE du
// schema : une URL http:// utilise WiFiClient (comportement historique INCHANGE) ; une URL https://
// utilise WiFiClientSecure. Epinglage CA opt-in via n3_ota_ca_cert.h (meme mecanisme que n3_data) ;
// sinon setInsecure() (TLS chiffre sans validation de certificat). Aucun impact sur les firmwares
// qui restent en http://.
#if defined(__has_include)
#  if __has_include("n3_ota_ca_cert.h")
#    include "n3_ota_ca_cert.h"   // doit definir N3_OTA_CA_CERT_PEM (PEM du root CA serveur)
#    define N3_OTA_HAS_CA_CERT 1
#  endif
#endif

static bool n3OtaUrlIsHttps(const char* url) {
    return url != nullptr && strncasecmp(url, "https://", 8) == 0;
}

static void n3OtaPrepareSecureClient(WiFiClientSecure& client) {
#if defined(N3_OTA_HAS_CA_CERT)
    client.setCACert(N3_OTA_CA_CERT_PEM);
#else
    client.setInsecure();
#endif
}

static uint8_t s_lastLoggedOtaPercent = 255;
static void (*s_progressCallback)(int, int, uint8_t, void*) = nullptr;
static void* s_progressUserData = nullptr;

static void appendHexByte(char* out, size_t idx, uint8_t value) {
    static const char* kHex = "0123456789abcdef";
    out[idx] = kHex[(value >> 4) & 0x0F];
    out[idx + 1] = kHex[value & 0x0F];
}

static void logOtaProgress(int current, int total);

static bool verifyFirmwareSignature(const char* sha256Hex, const char* signatureB64, char* details, size_t detailsSize) {
    // Fail-safe (audit 2026-07, S5) : « impossible de verifier » n'est PAS
    // « valide ». Une signature absente/nulle (ou un hash nul) renvoie false, pour
    // eviter tout fail-open si un futur appelant oublie de garder ce cas.
    // IMPORTANT : ceci ne change RIEN au comportement actuel — l'unique appelant
    // (voir ~l.320) n'invoque cette fonction QUE lorsqu'une signature est presente,
    // et la POLITIQUE « signature absente » (accepter en sha256-only vs refuser)
    // reste decidee cote appelant via #if defined(N3_OTA_REQUIRE_SIGNATURE), non
    // active a ce jour (cf. docs/AUDIT_GENERAL_2026-07.md, decision : enforcement
    // OTA laisse OFF tant que le serveur ne signe pas systematiquement).
    if (!sha256Hex || !signatureB64 || signatureB64[0] == '\0') return false;

    uint8_t hash[32];
    for (size_t i = 0; i < 32; ++i) {
        unsigned int byteVal = 0;
        if (sscanf(&sha256Hex[i * 2], "%2x", &byteVal) != 1) {
            if (details && detailsSize > 0) snprintf(details, detailsSize, "Signature: sha256 invalide.");
            return false;
        }
        hash[i] = static_cast<uint8_t>(byteVal);
    }

    size_t sigMax = strlen(signatureB64);
    uint8_t* sig = static_cast<uint8_t*>(malloc(sigMax));
    if (!sig) {
        if (details && detailsSize > 0) snprintf(details, detailsSize, "Signature: memoire insuffisante.");
        return false;
    }

    size_t sigLen = 0;
    int b64Ret = mbedtls_base64_decode(sig, sigMax, &sigLen, reinterpret_cast<const unsigned char*>(signatureB64), strlen(signatureB64));
    if (b64Ret != 0) {
        if (details && detailsSize > 0) snprintf(details, detailsSize, "Signature: base64 invalide (%d).", b64Ret);
        free(sig);
        return false;
    }

    mbedtls_pk_context pk;
    mbedtls_pk_init(&pk);
    int pkRet = mbedtls_pk_parse_public_key(
        &pk,
        reinterpret_cast<const unsigned char*>(OTA_SIGNING_PUBLIC_KEY_PEM),
        strlen(OTA_SIGNING_PUBLIC_KEY_PEM) + 1);
    if (pkRet != 0) {
        if (details && detailsSize > 0) snprintf(details, detailsSize, "Signature: cle publique invalide (%d).", pkRet);
        mbedtls_pk_free(&pk);
        free(sig);
        return false;
    }

    int verifyRet = mbedtls_pk_verify(&pk, MBEDTLS_MD_SHA256, hash, sizeof(hash), sig, sigLen);
    mbedtls_pk_free(&pk);
    free(sig);
    if (verifyRet != 0) {
        if (details && detailsSize > 0) snprintf(details, detailsSize, "Signature OTA invalide (%d).", verifyRet);
        return false;
    }

    return true;
}

static void logOtaPartitionDiagnostics() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* next = esp_ota_get_next_update_partition(nullptr);
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (running) {
        Serial.printf("[OTA] partition courante: %s @0x%x taille=%u\n",
                      running->label,
                      static_cast<unsigned>(running->address),
                      static_cast<unsigned>(running->size));
    }
    if (next) {
        Serial.printf("[OTA] partition OTA cible: %s @0x%x taille=%u\n",
                      next->label,
                      static_cast<unsigned>(next->address),
                      static_cast<unsigned>(next->size));
    }
    if (boot && running && boot->address != running->address) {
        Serial.printf("[OTA][WARN] partition boot (%s) != courante (%s)\n",
                      boot->label, running->label);
    }
}

/** Telecharge une seule fois, verifie sha256/signature et flashe via Update (evite le 2e GET httpUpdate). */
static bool downloadAndFlashFirmware(const char* firmwareUrl,
                                     const char* expectedSha256,
                                     const char* signatureB64,
                                     char* details,
                                     size_t detailsSize) {
    if (!firmwareUrl || !expectedSha256 || strlen(expectedSha256) != 64) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "Metadata OTA invalide: champ sha256 manquant/incorrect.");
        }
        return false;
    }

    n3OtaSyncBootPartition();
    logOtaPartitionDiagnostics();

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    WiFiClient* client;
    if (n3OtaUrlIsHttps(firmwareUrl)) {
        n3OtaPrepareSecureClient(secureClient);
        client = &secureClient;
    } else {
        client = &plainClient;
    }

    HTTPClient http;
    http.begin(*client, firmwareUrl);
    http.setTimeout(30000);
    int code = http.GET();
    if (code != 200) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "OTA firmware: HTTP %d.", code);
        }
        http.end();
        return false;
    }

    const int contentLen = http.getSize();
    if (contentLen <= 0) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "OTA firmware: taille inconnue (Content-Length absent).");
        }
        http.end();
        return false;
    }

    const esp_partition_t* updatePart = esp_ota_get_next_update_partition(nullptr);
    if (updatePart && static_cast<size_t>(contentLen) > updatePart->size) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize,
                     "OTA firmware %d octets > partition %s (%u). Re-flasher USB (min_spiffs).",
                     contentLen,
                     updatePart->label,
                     static_cast<unsigned>(updatePart->size));
        }
        http.end();
        return false;
    }

    if (!Update.begin(static_cast<size_t>(contentLen), U_FLASH)) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "OTA begin: %s", Update.errorString());
        }
        Update.abort();
        http.end();
        return false;
    }

    mbedtls_md_context_t mdCtx;
    mbedtls_md_init(&mdCtx);
    const mbedtls_md_info_t* mdInfo = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (mdInfo == nullptr || mbedtls_md_setup(&mdCtx, mdInfo, 0) != 0 || mbedtls_md_starts(&mdCtx) != 0) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "SHA256: init mbedtls a echoue.");
        }
        mbedtls_md_free(&mdCtx);
        Update.abort();
        http.end();
        return false;
    }

    s_lastLoggedOtaPercent = 255;
    Serial.println("[OTA][PROGRESS] debut telechargement");

    WiFiClient* stream = http.getStreamPtr();
    uint8_t buffer[1024];
    int remaining = contentLen;
    int writtenTotal = 0;
    bool magicChecked = false;

    while (http.connected() && remaining > 0) {
        size_t availableBytes = stream->available();
        if (availableBytes == 0) {
            delay(1);
            continue;
        }
        if (availableBytes > sizeof(buffer)) {
            availableBytes = sizeof(buffer);
        }
        if (static_cast<int>(availableBytes) > remaining) {
            availableBytes = static_cast<size_t>(remaining);
        }
        const int readLen = stream->readBytes(buffer, availableBytes);
        if (readLen <= 0) {
            break;
        }

        if (!magicChecked) {
            if (buffer[0] != 0xE9) {
                if (details && detailsSize > 0) {
                    snprintf(details, detailsSize,
                             "OTA firmware: magic 0x%02x (attendu 0xE9) — binaire ou reponse HTTP invalide.",
                             buffer[0]);
                }
                mbedtls_md_free(&mdCtx);
                Update.abort();
                http.end();
                return false;
            }
            magicChecked = true;
        }

        if (Update.write(buffer, static_cast<size_t>(readLen)) != static_cast<size_t>(readLen)) {
            if (details && detailsSize > 0) {
                snprintf(details, detailsSize, "OTA write: %s", Update.errorString());
            }
            mbedtls_md_free(&mdCtx);
            Update.abort();
            http.end();
            return false;
        }

        if (mbedtls_md_update(&mdCtx, buffer, static_cast<size_t>(readLen)) != 0) {
            if (details && detailsSize > 0) {
                snprintf(details, detailsSize, "SHA256: update mbedtls a echoue.");
            }
            mbedtls_md_free(&mdCtx);
            Update.abort();
            http.end();
            return false;
        }

        writtenTotal += readLen;
        remaining -= readLen;
        logOtaProgress(writtenTotal, contentLen);
    }
    http.end();

    if (writtenTotal != contentLen) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize,
                     "OTA firmware: telechargement incomplet (%d/%d octets).",
                     writtenTotal, contentLen);
        }
        mbedtls_md_free(&mdCtx);
        Update.abort();
        return false;
    }

    uint8_t digest[32];
    if (mbedtls_md_finish(&mdCtx, digest) != 0) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "SHA256: finish mbedtls a echoue.");
        }
        mbedtls_md_free(&mdCtx);
        Update.abort();
        return false;
    }
    mbedtls_md_free(&mdCtx);

    char computedSha256[65];
    for (size_t i = 0; i < sizeof(digest); ++i) {
        appendHexByte(computedSha256, i * 2, digest[i]);
    }
    computedSha256[64] = '\0';
    Serial.printf("[OTA] sha256 metadata=%s\n", expectedSha256);
    Serial.printf("[OTA] sha256 calcule =%s\n", computedSha256);

    if (strcasecmp(expectedSha256, computedSha256) != 0) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "SHA256 OTA mismatch (metadata != calcule).");
        }
        Update.abort();
        return false;
    }

    if (signatureB64 && signatureB64[0] != '\0') {
        if (!verifyFirmwareSignature(computedSha256, signatureB64, details, detailsSize)) {
            Update.abort();
            return false;
        }
        Serial.println("[OTA] signature ECDSA valide.");
    } else {
#if defined(N3_OTA_REQUIRE_SIGNATURE)
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize,
                     "Signature OTA requise (N3_OTA_REQUIRE_SIGNATURE) mais absente de la metadata.");
        }
        Serial.println("[OTA][SECURITE] signature absente et requise -> mise a jour refusee.");
        Update.abort();
        return false;
#else
        Serial.println("[OTA] signature absente, verification sha256 uniquement.");
#endif
    }

    if (!Update.end(true)) {
        if (details && detailsSize > 0) {
            snprintf(details, detailsSize, "OTA end: %s", Update.errorString());
        }
        Update.abort();
        return false;
    }

    Serial.println("[OTA] MAJ reussie, redemarrage...");
    delay(100);
    ESP.restart();
    return true;
}

static void logOtaProgress(int current, int total) {
    if (total <= 0 || current < 0) return;

    uint8_t pct = static_cast<uint8_t>((static_cast<uint64_t>(current) * 100ULL) / static_cast<uint64_t>(total));
    if (pct > 100) pct = 100;

    // Log only milestones (every 5%) + bounds to keep monitor readable.
    if (pct == s_lastLoggedOtaPercent) return;
    if (pct != 0 && pct != 100 && (pct % 5) != 0) return;

    Serial.printf("[OTA][PROGRESS] %u%% (%d/%d)\n",
                  static_cast<unsigned>(pct),
                  current,
                  total);
    if (s_progressCallback) {
        s_progressCallback(current, total, pct, s_progressUserData);
    }
    s_lastLoggedOtaPercent = pct;
}

void n3OtaSyncBootPartition() {
    const esp_partition_t* running = esp_ota_get_running_partition();
    const esp_partition_t* boot = esp_ota_get_boot_partition();
    if (!running || !boot) return;
    if (running->address != boot->address) {
        esp_err_t err = esp_ota_set_boot_partition(running);
        if (err == ESP_OK) {
            Serial.printf("[OTA] Boot aligne sur partition courante: %s\n", running->label);
        }
    }
}

static int compareVersions(const char* v1, const char* v2) {
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);
    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

bool n3OtaCheck(const N3OtaConfig& config) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] Pas de WiFi, verification ignoree");
        if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: WiFi indisponible.", config.userData);
        return false;
    }

    n3OtaSyncBootPartition();

    Serial.printf("[OTA] Verification MAJ : %s (locale: %s)\n",
                  config.metadataUrl, config.currentVersion);

    WiFiClient plainClient;
    WiFiClientSecure secureClient;
    WiFiClient* client;
    if (n3OtaUrlIsHttps(config.metadataUrl)) {
        n3OtaPrepareSecureClient(secureClient);
        client = &secureClient;
    } else {
        client = &plainClient;
    }
    HTTPClient http;
    http.begin(*client, config.metadataUrl);
    http.setTimeout(10000);
    int code = http.GET();

    if (code != 200) {
        if (code == 404) {
            Serial.println("[OTA] Metadata introuvable (404) - normal si env test sans fichier deploye");
            if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: metadata 404.", config.userData);
        } else {
            Serial.printf("[OTA] Erreur HTTP %d\n", code);
            if (config.onUpdateEnd) {
                char details[96];
                snprintf(details, sizeof(details), "OTA ignoree: erreur HTTP metadata %d.", code);
                config.onUpdateEnd(false, details, config.userData);
            }
        }
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("[OTA] JSON invalide: %s\n", err.c_str());
        if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: metadata JSON invalide.", config.userData);
        return false;
    }

    JsonObject entry;
    if (config.metadataKey && config.metadataKey[0] != '\0') {
        if (!doc[config.metadataKey].is<JsonObject>()) {
            Serial.printf("[OTA] Cle %s absente\n", config.metadataKey);
            if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: cle metadata absente.", config.userData);
            return false;
        }
        entry = doc[config.metadataKey].as<JsonObject>();
    } else {
        entry = doc.as<JsonObject>();
    }

    const char* remoteVersion = entry["version"].as<const char*>();
    const char* firmwareUrl = entry["url"].as<const char*>();
    const char* expectedSha256 = entry["sha256"].as<const char*>();
    const char* signatureB64 = entry["signature"].as<const char*>();
    if (!remoteVersion || !firmwareUrl) {
        Serial.println("[OTA] JSON invalide ou champs version/url manquants");
        if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: champs version/url manquants.", config.userData);
        return false;
    }

    Serial.printf("[OTA] Version distante : %s\n", remoteVersion);

    if (compareVersions(remoteVersion, config.currentVersion) <= 0) {
        Serial.println("[OTA] Deja a jour");
        if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: firmware deja a jour.", config.userData);
        return false;
    }

    Serial.printf("[OTA] MAJ disponible %s -> %s, telechargement...\n",
                  config.currentVersion, remoteVersion);
    if (config.onUpdateStart) {
        config.onUpdateStart(config.currentVersion, remoteVersion, firmwareUrl, config.userData);
    }

    char integrityDetails[192];
    s_progressCallback = config.onUpdateProgress;
    s_progressUserData = config.userData;
    if (!downloadAndFlashFirmware(firmwareUrl, expectedSha256, signatureB64,
                                  integrityDetails, sizeof(integrityDetails))) {
        Serial.printf("[OTA] Echec MAJ: %s\n", integrityDetails);
        s_progressCallback = nullptr;
        s_progressUserData = nullptr;
        if (config.onUpdateEnd) {
            config.onUpdateEnd(false, integrityDetails, config.userData);
        }
        return false;
    }

    s_progressCallback = nullptr;
    s_progressUserData = nullptr;
    if (config.onUpdateEnd) {
        config.onUpdateEnd(true, "OTA terminee avec succes. Redemarrage imminent.", config.userData);
    }
    return true;
}
