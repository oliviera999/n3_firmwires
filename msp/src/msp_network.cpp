/* MeteoStationPrototype (msp1) — Réseau
 * datatobdd, variablestoesp, Wificonnect
 */

#include "msp_network.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <WiFi.h>
#include <Arduino_JSON.h>
#include <stdlib.h>
#include "n3_wifi.h"
#include "n3_data.h"

static int readIntByKey(JSONVar& obj, const char* key, int defaultValue) {
  if (!obj.hasOwnProperty(key)) {
    return defaultValue;
  }
  JSONVar val = obj[key];
  String valueType = JSON.typeof(val);
  if (valueType == "undefined" || valueType == "null") {
    return defaultValue;
  }

  if (valueType == "number" || valueType == "boolean") {
    return (int)val;
  }

  if (valueType == "string") {
    const char* raw = (const char*)val;
    if (raw == nullptr || raw[0] == '\0') {
      return defaultValue;
    }
    return atoi(raw);
  }

  return defaultValue;
}

static bool tryReadIntByKey(JSONVar& obj, const char* key, int* outValue) {
  if (outValue == nullptr) return false;
  if (!obj.hasOwnProperty(key)) {
    return false;
  }
  JSONVar val = obj[key];
  String valueType = JSON.typeof(val);
  if (valueType == "undefined" || valueType == "null") {
    return false;
  }

  if (valueType == "number" || valueType == "boolean") {
    *outValue = (int)val;
    return true;
  }

  if (valueType == "string") {
    const char* raw = (const char*)val;
    if (raw == nullptr) {
      return false;
    }
    char* endPtr = nullptr;
    long parsed = strtol(raw, &endPtr, 10);
    if (endPtr == raw) {
      return false;
    }
    while (endPtr != nullptr && (*endPtr == ' ' || *endPtr == '\t' || *endPtr == '\r' || *endPtr == '\n')) {
      ++endPtr;
    }
    if (endPtr != nullptr && *endPtr != '\0') {
      return false;
    }
    *outValue = (int)parsed;
    return true;
  }

  return false;
}

static String readStringByKey(JSONVar& obj, const char* key, const String& defaultValue) {
  if (!obj.hasOwnProperty(key)) {
    return defaultValue;
  }
  JSONVar val = obj[key];
  String valueType = JSON.typeof(val);
  if (valueType == "undefined" || valueType == "null") {
    return defaultValue;
  }

  if (valueType == "string") {
    const char* raw = (const char*)val;
    if (raw == nullptr) {
      return defaultValue;
    }
    return String(raw);
  }

  // Fallback: stringify for numeric/boolean values returned by the server.
  return JSON.stringify(val);
}

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

  n3DataPost(cfg);
  delay(500);
  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
}

void variablestoesp() {
  static bool s_servoModeKnown = false;
  static bool s_prevServoModeAuto = true;
  static bool s_servoTargetsKnown = false;
  static int s_prevTargetGd = 0;
  static int s_prevTargetHb = 0;

  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[SERVER][GET] Lecture config distante depuis %s\n", serverNameOutput);
    outputsState = n3DataGet(serverNameOutput, &httpResponseCode);
    delay(200);
    if (httpResponseCode > 0) {
      Serial.printf("[SERVER][GET] HTTP=%u\n", httpResponseCode);
      Serial.println("[SERVER][GET][BODY] " + outputsState);
      JSONVar myObject = JSON.parse(outputsState);
      if (JSON.typeof(myObject) == "undefined") {
        Serial.println("[SERVER][GET][WARN] JSON invalide, config distante ignoree");
        return;
      }
      // Mapping robuste: lecture directe par GPIO explicite (contrat serveur).
      int parsedResetMode = resetMode ? 1 : 0;
      int parsedWakeUp = WakeUp ? 1 : 0;
      int parsedFreqWakeUp = FreqWakeUp;
      int parsedServoModeAuto = servoModeAuto ? 1 : 0;
      bool hasResetMode = tryReadIntByKey(myObject, "110", &parsedResetMode);
      bool hasWakeUp = tryReadIntByKey(myObject, "106", &parsedWakeUp);
      bool hasFreqWakeUp = tryReadIntByKey(myObject, "107", &parsedFreqWakeUp);
      bool hasServoModeAuto = tryReadIntByKey(myObject, "111", &parsedServoModeAuto);
      String raw110 = readStringByKey(myObject, "110", "<absent>");
      String raw106 = readStringByKey(myObject, "106", "<absent>");
      String raw107 = readStringByKey(myObject, "107", "<absent>");
      String raw111 = readStringByKey(myObject, "111", "<absent>");

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
                     String(FreqWakeUp) + " 111:" + raw111 + "=>" + String(servoModeAuto ? 1 : 0));
      Serial.println(String("[SERVER][GET] resetMode=") + String(resetMode ? 1 : 0) +
                     " wakeUp=" + String(WakeUp ? 1 : 0) + " sleep=" + String(FreqWakeUp) +
                     " servoModeAuto=" + String(servoModeAuto ? 1 : 0));
    } else {
      Serial.printf("[SERVER][GET][WARN] Requete echouee, HTTP=%u\n", httpResponseCode);
    }
    if (displayOk) { display.fillCircle(115, 5, 5, WHITE); display.display(); }
  }
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
  cfg.preScanDelayMs = 300;
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
