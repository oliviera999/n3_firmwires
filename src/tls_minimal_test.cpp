// Sketch TLS minimal pour diagnostic du bug start_ssl_client / RNG
// Objectif: reproduire (ou non) le panic TLS sans tout le firmware FFP5CS

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include <cstring>

// Réutiliser exactement les mêmes credentials WiFi que le firmware (sans WifiManager)
#include "secrets.h"

static const char* TEST_URL =
  "https://iot.olution.info/ffp3/api/outputs-test/state";

WiFiClientSecure g_client;
HTTPClient       g_http;

unsigned long g_lastGetMs = 0;
const unsigned long GET_INTERVAL_MS = 30000; // 30s entre GET

void logHeap(const char* prefix) {
  uint32_t freeHeap = ESP.getFreeHeap();
  uint32_t minHeap  = ESP.getMinFreeHeap();
  Serial.printf("[%s] Heap libre=%u bytes, MinHeap=%u bytes\n",
                prefix, freeHeap, minHeap);
}

void connectWifiSimple() {
  Serial.println("\n=== WIFI CONNECT (minimal) ===");
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.setAutoReconnect(false);
  WiFi.disconnect(false, true);
  delay(500);

  const unsigned long perSsidTimeoutMs = 15000;

  for (size_t i = 0; i < Secrets::WIFI_COUNT; i++) {
    const char* ssid = Secrets::WIFI_LIST[i].ssid;
    const char* pass = Secrets::WIFI_LIST[i].password;
    Serial.printf("[WiFi] Tentative %u/%u: '%s'\n",
                  static_cast<unsigned>(i + 1),
                  static_cast<unsigned>(Secrets::WIFI_COUNT),
                  ssid);

    WiFi.begin(ssid, pass);
    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED &&
           millis() - start < perSsidTimeoutMs) {
      delay(250);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("[WiFi] ✅ Connecté");
      Serial.printf("[WiFi] SSID: %s\n", ssid);
      IPAddress wifiIP = WiFi.localIP();
      Serial.printf("[WiFi] IP: %d.%d.%d.%d\n", wifiIP[0], wifiIP[1], wifiIP[2], wifiIP[3]);
      Serial.printf("[WiFi] RSSI: %d dBm\n", WiFi.RSSI());
      return;
    }

    Serial.println("[WiFi] ❌ Échec, purge état et essai suivant");
    WiFi.disconnect(false, true);
    delay(500);
  }

  Serial.println("[WiFi] ❌ Échec connexion à tous les SSID");
}

bool runSingleHttpsGet() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TEST] ❌ WiFi non connecté, skip GET");
    return false;
  }

  Serial.println("\n=== DÉBUT GET HTTPS MINIMAL ===");
  unsigned long startMs = millis();

  logHeap("BEFORE_GET");

  Serial.printf("[TEST] URL: %s\n", TEST_URL);
  Serial.printf("[TEST] WiFi status=%d connected=%s RSSI=%d dBm\n",
                WiFi.status(),
                WiFi.isConnected() ? "YES" : "NO",
                WiFi.RSSI());

  // Client TLS très basique
  g_client.setInsecure();           // évite gestion CA pour le test
  g_client.setHandshakeTimeout(5000);
  WiFi.setSleep(false);

  Serial.println("[TEST] 🔒 Préparation client HTTPS...");

  if (!g_http.begin(g_client, TEST_URL)) {
    Serial.println("[TEST] ❌ http.begin() a échoué");
    return false;
  }

  Serial.println("[TEST] 🔒 AVANT OPÉRATION TLS CRITIQUE");
  logHeap("BEFORE_TLS");

  // GET avec timeout raisonnable
  g_http.setTimeout(5000);
  int code = g_http.GET();
  unsigned long duration = millis() - startMs;

  Serial.printf("[TEST] GET terminé, code=%d, durée=%lu ms\n", code, duration);
  logHeap("AFTER_GET");

  if (code > 0) {
    // Lire la réponse dans un buffer statique
    const size_t MAX_PAYLOAD = 2048;
    char payload[MAX_PAYLOAD];
    WiFiClient* stream = g_http.getStreamPtr();
    size_t payloadLen = 0;
    if (stream) {
      while (stream->available() && payloadLen < MAX_PAYLOAD - 1) {
        size_t bytesRead = stream->readBytes(payload + payloadLen, MAX_PAYLOAD - payloadLen - 1);
        if (bytesRead == 0) break;
        payloadLen += bytesRead;
      }
      payload[payloadLen] = '\0';
    } else {
      // v11.180: Suppression getString() - cause crashes LoadProhibited
      Serial.println("[TEST] ⚠️ Pas de stream HTTP disponible");
      payload[0] = '\0';
      payloadLen = 0;
    }
    Serial.printf("[TEST] Réponse: %zu bytes\n", payloadLen);
  } else {
    // v11.180: Suppression errorToString() - cause crashes LoadProhibited
    Serial.printf("[TEST] ❌ Erreur GET code: %d\n", code);
  }

  g_http.end();
  Serial.println("=== FIN GET HTTPS MINIMAL ===");
  return code > 0 && code < 400;
}

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\n=== SKETCH TLS MINIMAL TEST ===");

  logHeap("BOOT");
  connectWifiSimple();

  // Premier GET immédiat
  runSingleHttpsGet();
  g_lastGetMs = millis();
}

void loop() {
  unsigned long now = millis();
  if (now - g_lastGetMs > GET_INTERVAL_MS) {
    runSingleHttpsGet();
    g_lastGetMs = now;
  }

  delay(100);
}

