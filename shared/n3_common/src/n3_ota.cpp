#include "n3_ota.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>

static int compareVersions(const char* v1, const char* v2) {
    int maj1 = 0, min1 = 0, pat1 = 0;
    int maj2 = 0, min2 = 0, pat2 = 0;
    sscanf(v1, "%d.%d.%d", &maj1, &min1, &pat1);
    sscanf(v2, "%d.%d.%d", &maj2, &min2, &pat2);
    if (maj1 != maj2) return maj1 - maj2;
    if (min1 != min2) return min1 - min2;
    return pat1 - pat2;
}

static String jsonExtractString(const String& json, const char* key) {
    String search = String("\"") + key + "\"";
    int keyIdx = json.indexOf(search);
    if (keyIdx < 0) return "";

    int colonIdx = json.indexOf(':', keyIdx + search.length());
    if (colonIdx < 0) return "";

    int quoteStart = json.indexOf('"', colonIdx + 1);
    if (quoteStart < 0) return "";

    int quoteEnd = json.indexOf('"', quoteStart + 1);
    if (quoteEnd < 0) return "";

    return json.substring(quoteStart + 1, quoteEnd);
}

bool n3OtaCheck(const N3OtaConfig& config) {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("[OTA] Pas de WiFi, verification ignoree");
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
        Serial.printf("[OTA] Erreur HTTP %d\n", code);
        http.end();
        return false;
    }

    String payload = http.getString();
    http.end();

    String remoteVersion = jsonExtractString(payload, "version");
    String firmwareUrl   = jsonExtractString(payload, "url");

    if (remoteVersion.isEmpty() || firmwareUrl.isEmpty()) {
        Serial.println("[OTA] JSON invalide ou champs manquants");
        return false;
    }

    Serial.printf("[OTA] Version distante : %s\n", remoteVersion.c_str());

    if (compareVersions(remoteVersion.c_str(), config.currentVersion) <= 0) {
        Serial.println("[OTA] Deja a jour");
        return false;
    }

    Serial.printf("[OTA] MAJ disponible %s -> %s, telechargement...\n",
                  config.currentVersion, remoteVersion.c_str());

    if (config.ledPin >= 0) {
        httpUpdate.setLedPin(config.ledPin, LOW);
    }
    httpUpdate.rebootOnUpdate(true);

    WiFiClient updateClient;
    t_httpUpdate_return ret = httpUpdate.update(updateClient, firmwareUrl);

    switch (ret) {
        case HTTP_UPDATE_OK:
            Serial.println("[OTA] MAJ reussie, redemarrage...");
            return true;
        case HTTP_UPDATE_FAILED:
            Serial.printf("[OTA] Echec MAJ: %s\n",
                          httpUpdate.getLastErrorString().c_str());
            break;
        case HTTP_UPDATE_NO_UPDATES:
            Serial.println("[OTA] Pas de MAJ");
            break;
    }
    return false;
}
