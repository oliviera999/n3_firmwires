#include "n3_ota.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <esp_ota_ops.h>
#include <ArduinoJson.h>
#include <cstdio>

static uint8_t s_lastLoggedOtaPercent = 255;

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

    Serial.printf("[OTA] Verification MAJ : %s (locale: %s)\n",
                  config.metadataUrl, config.currentVersion);

    WiFiClient client;
    HTTPClient http;
    http.begin(client, config.metadataUrl);
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

    if (config.ledPin >= 0) {
        httpUpdate.setLedPin(config.ledPin, LOW);
    }
    s_lastLoggedOtaPercent = 255;
    Serial.println("[OTA][PROGRESS] debut telechargement");
    httpUpdate.onProgress(logOtaProgress);
    httpUpdate.rebootOnUpdate(true);

    WiFiClient updateClient;
    t_httpUpdate_return ret = httpUpdate.update(updateClient, firmwareUrl);

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] MAJ reussie, redemarrage...");
            if (config.onUpdateEnd) config.onUpdateEnd(true, "OTA terminee avec succes. Redemarrage imminent.", config.userData);
            return true;
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Echec MAJ: %s\n",
                          httpUpdate.getLastErrorString().c_str());
            if (config.onUpdateEnd) {
                char details[192];
                snprintf(details, sizeof(details), "OTA echec: %s", httpUpdate.getLastErrorString().c_str());
                config.onUpdateEnd(false, details, config.userData);
            }
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] Pas de MAJ");
            if (config.onUpdateEnd) config.onUpdateEnd(false, "OTA ignoree: pas de mise a jour.", config.userData);
            break;
    }
    return false;
}
