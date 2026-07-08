/* MeteoStationPrototype (msp1) — Réseau
 * datatobdd, variablestoesp, Wificonnect
 */

#include "msp_network.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <WiFi.h>
#include <Arduino_JSON.h>
#include "n3_wifi.h"
#include "n3_data.h"
#include "n3_outputs_json.h"

// Helpers de parsing : depuis v2.43 factorises dans shared/n3_common/n3_outputs_json
// (autrefois dupliques entre n3pp_network.cpp et msp_network.cpp).
using N3Outputs::readIntByKey;
using N3Outputs::tryReadIntByKey;
using N3Outputs::readStringByKey;

void datatobdd() {
  if (displayOk) { display.drawCircle(5, 5, 5, WHITE); display.display(); }
  Serial.printf("[SERVER][POST] Debut envoi vers %s\n", serverNamePostData);

  N3DataField fields[] = {
    {"api_key",       apiKeyValue},
    {"sensor",        sensorName},
    {"version",       version},
    {"TempAirInt",    String(tempAirInt)},
    {"TempAirExt",    String(tempAirExt)},
    {"HumidAirInt",   String(humidAirInt)},
    {"HumidAirExt",   String(humidAirExt)},
    {"LuminositeA",   String(photocellReadingA)},
    {"LuminositeB",   String(photocellReadingB)},
    {"LuminositeC",   String(photocellReadingC)},
    {"LuminositeD",   String(photocellReadingD)},
    {"LuminositeMoy", String(photocellReadingMoy)},
    {"ServoHB",       String(AngleServoHB)},
    {"ServoGD",       String(AngleServoGD)},
    {"HumidSol",      String(HumidSol)},
    {"Pluie",         String(Pluie)},
    {"TempEau",       String(temperatureSol)},
    {"PontDiv",       String(PontDiv)},
    {"WakeUp",        String(WakeUp)},
    {"VeilleInfinie", String(VeilleInfinie)},
    {"SeuilSec",      String(SeuilSec)},
    {"FreqWakeUp",    String(FreqWakeUp)},
    {"SeuilPontDiv",  String(SeuilPontDiv)},
    {"mail",          inputMessageMailAd},
    {"mailNotif",     enableEmailChecked},
    {"resetMode",     String(resetMode)},
    {"bootCount",     String(bootCount)},
  };

  N3PostConfig cfg = {};
  cfg.url = serverNamePostData;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  // Auth HMAC FFP3 si API_SIG_SECRET est defini ET RTC sync (epoch > 1577836800 = 2020-01-01).
  cfg.sigSecret = (API_SIG_SECRET[0] != '\0') ? API_SIG_SECRET : nullptr;
  const unsigned long epochNow = (unsigned long)rtc.getEpoch();
  cfg.currentEpochSeconds = (epochNow > 1577836800UL) ? epochNow : 0UL;
  String postPreview =
      "api_key=<masque>&sensor=" + sensorName +
      "&version=" + version +
      "&TempAirInt=" + String(tempAirInt) +
      "&TempAirExt=" + String(tempAirExt) +
      "&HumidAirInt=" + String(humidAirInt) +
      "&HumidAirExt=" + String(humidAirExt) +
      "&LuminositeA=" + String(photocellReadingA) +
      "&LuminositeB=" + String(photocellReadingB) +
      "&LuminositeC=" + String(photocellReadingC) +
      "&LuminositeD=" + String(photocellReadingD) +
      "&LuminositeMoy=" + String(photocellReadingMoy) +
      "&ServoHB=" + String(AngleServoHB) +
      "&ServoGD=" + String(AngleServoGD) +
      "&HumidSol=" + String(HumidSol) +
      "&Pluie=" + String(Pluie) +
      "&TempEau=" + String(temperatureSol) +
      "&PontDiv=" + String(PontDiv) +
      "&WakeUp=" + String(WakeUp) +
      "&VeilleInfinie=" + String(VeilleInfinie) +
      "&SeuilSec=" + String(SeuilSec) +
      "&FreqWakeUp=" + String(FreqWakeUp) +
      "&SeuilPontDiv=" + String(SeuilPontDiv) +
      "&mail=" + inputMessageMailAd +
      "&mailNotif=" + enableEmailChecked +
      "&resetMode=" + String(resetMode) +
      "&bootCount=" + String(bootCount);
  Serial.println("[SERVER][POST][PAYLOAD] " + postPreview);
  cfg.onSending = []() {
    if (displayOk) { display.fillCircle(5, 5, 5, WHITE); display.display(); }
  };
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("[SERVER][POST] Envoi BDD: OK (HTTP 200)");
    } else {
      Serial.printf("[SERVER][POST][WARN] Envoi BDD: erreur HTTP %d\n", code);
    }
  };

  const int postCode = n3DataPost(cfg);
  // Phase 3 arbitrage mails : capture LOCALE du succes du POST de ce reveil
  // (resultat auparavant ignore). 200 = le serveur a nos donnees -> il est
  // l'emetteur PRIMAIRE de l'alerte batterie ; sinon failover local.
  postOkThisWake = (postCode == 200);
  if (postOkThisWake) {
    failoverMailsSent = 0;  // retour en ligne : re-arme le budget failover
  }
  delay(500);
  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
}

void sendHeartbeat() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER][HB][SKIP] WiFi non connecte");
    return;
  }

  static uint32_t minHeap = UINT32_MAX;
  const uint32_t freeHeap = ESP.getFreeHeap();
  if (freeHeap < minHeap) {
    minHeap = freeHeap;
  }

  const uint32_t uptimeSec = millis() / 1000UL;
  const int rssi = WiFi.RSSI();

  N3DataField fields[] = {
    {"api_key",  apiKeyValue},
    {"sensor",   sensorName},
    {"version",  version},
    {"uptime",   String(uptimeSec)},
    {"free",     String(freeHeap)},
    {"min",      String(minHeap == UINT32_MAX ? freeHeap : minHeap)},
    {"reboots",  String(bootCount)},
    {"rssi",     String(rssi)},
  };

  N3PostConfig cfg = {};
  cfg.url = serverNameHeartbeat;
  cfg.apiKey = API_KEY;
  cfg.fields = fields;
  cfg.fieldCount = sizeof(fields) / sizeof(fields[0]);
  cfg.sigSecret = (API_SIG_SECRET[0] != '\0') ? API_SIG_SECRET : nullptr;
  const unsigned long epochNow = (unsigned long)rtc.getEpoch();
  cfg.currentEpochSeconds = (epochNow > 1577836800UL) ? epochNow : 0UL;
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("[SERVER][HB] OK");
    } else {
      Serial.printf("[SERVER][HB] erreur HTTP %d\n", code);
    }
  };

  Serial.printf("[SERVER][HB] Envoi vers %s\n", serverNameHeartbeat);
  (void)n3DataPost(cfg);
  delay(200);
}

// Compteur d'echecs consecutifs pour le polling /outputs_state cote msp.
static unsigned int s_outputsGetFailureCount = 0;

void variablestoesp() {
  static bool s_servoModeKnown = false;
  static bool s_prevServoModeAuto = true;
  static bool s_servoTargetsKnown = false;
  static int s_prevTargetGd = 0;
  static int s_prevTargetHb = 0;

  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[SERVER][GET][SKIP] WiFi non connecte, config distante non rafraichie");
    return;
  }

  Serial.printf("[SERVER][GET] Lecture config distante depuis %s\n", serverNameOutput);
  // X-Api-Key envoye aussi sur le GET d'etat : sans effet sur un serveur ancien
  // (en-tete ignore), mais permet a un serveur a jour d'exiger la cle sur ce GET
  // (flag FIRMWARE_STATE_REQUIRE_KEY) au lieu de le laisser totalement public.
  outputsState = n3DataGet(serverNameOutput, &httpResponseCode, API_KEY);
  delay(200);
  Serial.printf("[SERVER][GET] HTTP=%u\n", httpResponseCode);
  Serial.println("[SERVER][GET][BODY] " + outputsState);

  if (httpResponseCode != 200) {
    ++s_outputsGetFailureCount;
    Serial.printf("[SERVER][GET][OFFLINE] Echec consecutif #%u (HTTP=%u), config locale conservee\n",
                  s_outputsGetFailureCount, httpResponseCode);
    if (displayOk) { display.fillCircle(115, 5, 5, WHITE); display.display(); }
    return;
  }

  JSONVar myObject = JSON.parse(outputsState);
  if (JSON.typeof(myObject) == "undefined") {
    ++s_outputsGetFailureCount;
    Serial.printf("[SERVER][GET][OFFLINE] JSON invalide (#%u), config locale conservee\n",
                  s_outputsGetFailureCount);
    if (displayOk) { display.fillCircle(115, 5, 5, WHITE); display.display(); }
    return;
  }
  if (s_outputsGetFailureCount > 0) {
    Serial.printf("[SERVER][GET] Reprise apres %u echec(s)\n", s_outputsGetFailureCount);
    s_outputsGetFailureCount = 0;
  }
  {
      // Mapping robuste: lecture directe par GPIO explicite (contrat serveur).
      int parsedResetMode = resetMode ? 1 : 0;
      int parsedWakeUp = WakeUp ? 1 : 0;
      int parsedFreqWakeUp = FreqWakeUp;
      int parsedServoModeAuto = servoModeAuto ? 1 : 0;
      int parsedVeilleInfinie = VeilleInfinie ? 1 : 0;
      bool hasResetMode = tryReadIntByKey(myObject, "110", &parsedResetMode);
      bool hasWakeUp = tryReadIntByKey(myObject, "106", &parsedWakeUp);
      bool hasFreqWakeUp = tryReadIntByKey(myObject, "107", &parsedFreqWakeUp);
      bool hasServoModeAuto = tryReadIntByKey(myObject, "111", &parsedServoModeAuto);
      bool hasVeilleInfinie = tryReadIntByKey(myObject, "112", &parsedVeilleInfinie);
      String raw110 = readStringByKey(myObject, "110", "<absent>");
      String raw106 = readStringByKey(myObject, "106", "<absent>");
      String raw107 = readStringByKey(myObject, "107", "<absent>");
      String raw111 = readStringByKey(myObject, "111", "<absent>");
      String raw112 = readStringByKey(myObject, "112", "<absent>");

      if (!hasResetMode) {
        Serial.println(String("[SERVER][GET][WARN] Cle 110 absente/invalide (raw=") + raw110 +
                       "), conservation=" + String(resetMode ? 1 : 0));
      } else {
        resetMode = (parsedResetMode != 0);
      }

      if (!hasWakeUp) {
        Serial.println(String("[SERVER][GET][WARN] Cle 106 absente/invalide (raw=") + raw106 +
                       "), conservation=" + String(WakeUp ? 1 : 0));
      } else {
        WakeUp = (parsedWakeUp != 0);
      }

      if (!hasFreqWakeUp) {
        Serial.println(String("[SERVER][GET][WARN] Cle 107 absente/invalide (raw=") + raw107 +
                       "), conservation=" + String(FreqWakeUp));
      } else if (parsedFreqWakeUp < 1 || parsedFreqWakeUp > 86400) {
        Serial.println(String("[SERVER][GET][WARN] Cle 107 hors plage (raw=") + raw107 +
                       " parsed=" + String(parsedFreqWakeUp) + "), conservation=" + String(FreqWakeUp));
      } else {
        FreqWakeUp = parsedFreqWakeUp;
      }

      if (!hasServoModeAuto) {
        Serial.println(String("[SERVER][GET][WARN] Cle 111 absente/invalide (raw=") + raw111 +
                       "), conservation=" + String(servoModeAuto ? 1 : 0));
      } else {
        servoModeAuto = (parsedServoModeAuto != 0);
      }

      if (!hasVeilleInfinie) {
        Serial.println(String("[SERVER][GET][WARN] Cle 112 absente/invalide (raw=") + raw112 +
                       "), conservation=" + String(VeilleInfinie ? 1 : 0));
      } else {
        VeilleInfinie = (parsedVeilleInfinie != 0);
      }

      inputMessageMailAd = readStringByKey(myObject, "100", inputMessageMailAd);
      enableEmailChecked = readStringByKey(myObject, "101", enableEmailChecked);
      SeuilSec = readIntByKey(myObject, "102", SeuilSec);
      SeuilPontDiv = readIntByKey(myObject, "103", SeuilPontDiv);
      AngleServoHB = readIntByKey(myObject, "104", AngleServoHB);
      AngleServoGD = readIntByKey(myObject, "105", AngleServoGD);
      if (!s_servoModeKnown || s_prevServoModeAuto != servoModeAuto) {
        Serial.println(String("[SERVO][MODE] source=server mode=") +
                       (servoModeAuto ? "AUTO" : "MANUEL") + " (raw111=" + raw111 + ")");
        s_prevServoModeAuto = servoModeAuto;
        s_servoModeKnown = true;
      }
      if (!s_servoTargetsKnown || s_prevTargetGd != AngleServoGD || s_prevTargetHb != AngleServoHB) {
        Serial.println(String("[SERVO][TARGET] source=server mode=") +
                       (servoModeAuto ? "AUTO" : "MANUEL") + " GD=" + String(AngleServoGD) +
                       " HB=" + String(AngleServoHB));
        s_prevTargetGd = AngleServoGD;
        s_prevTargetHb = AngleServoHB;
        s_servoTargetsKnown = true;
      }
      Serial.println(String("[SERVER][GET][APPLY] 110:") + raw110 + "=>" + String(resetMode ? 1 : 0) +
                     " 106:" + raw106 + "=>" + String(WakeUp ? 1 : 0) + " 107:" + raw107 + "=>" +
                     String(FreqWakeUp) + " 111:" + raw111 + "=>" + String(servoModeAuto ? 1 : 0) +
                     " 112:" + raw112 + "=>" + String(VeilleInfinie ? 1 : 0));
      Serial.println(String("[SERVER][GET] resetMode=") + String(resetMode ? 1 : 0) +
                     " wakeUp=" + String(WakeUp ? 1 : 0) + " sleep=" + String(FreqWakeUp) +
                     " servoModeAuto=" + String(servoModeAuto ? 1 : 0) +
                     " veilleInfinie=" + String(VeilleInfinie ? 1 : 0));
  }
  if (displayOk) { display.fillCircle(115, 5, 5, WHITE); display.display(); }
}

void Wificonnect() {
  static const N3WifiNetwork networks[] = {
    {ssid, password}, {ssid2, password2}, {ssid3, password3}
  };
  N3WifiConfig cfg = {};
  cfg.networks = networks;
  cfg.networkCount = 3;
  cfg.timeoutMs = N3_WIFI_TIMEOUT_MS;
  cfg.delayBetweenMs = 250;
  cfg.preScanDelayMs = 100;  // 300 ms était conservateur ; la radio se stabilise en ~100 ms
  cfg.scanMax = 10;
  cfg.onConnecting = []() {
    if (displayOk) {
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println("Wifi");
      display.println("CONNEXION...");
      display.display();
    }
  };
  cfg.onFailure = []() {
    Serial.println("[WIFI][WARN] Echec: tous les reseaux configures ont echoue");
    if (displayOk) { display.println("ECHEC"); display.display(); }
  };
  cfg.onSuccess = [](const char*) {
    if (displayOk) { display.println(Wifiactif); display.println("OK"); display.display(); }
  };

  n3WifiConnect(cfg, &Wifiactif);
  delay(100);
}
