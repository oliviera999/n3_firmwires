#include "n3pp_network.h"
#include "n3pp_globals.h"
#include <Arduino_JSON.h>
#include <stdlib.h>
#include "n3_wifi.h"
#include "n3_data.h"
#include "n3pp_automation.h"

static const uint16_t OUTPUTS_FETCH_DELAY_MS = 250;

static int readIntByKey(const JSONVar& obj, const char* key, int defaultValue) {
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

static bool tryReadIntByKey(const JSONVar& obj, const char* key, int* outValue) {
  if (outValue == nullptr) return false;
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

static String readStringByKey(const JSONVar& obj, const char* key, const String& defaultValue) {
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

  return JSON.stringify(val);
}

void datatobdd() {
  Serial.println("DATATOBDD!!!");
  if (displayOk) { display.drawCircle(5, 5, 5, WHITE); display.display(); }

  N3DataField fields[] = {
    {"api_key",       apiKeyValue},
    {"sensor",        sensorName},
    {"version",       version},
    {"TempAir",       String(temperatureAir)},
    {"Humidite",      String(h)},
    {"Luminosite",    String(photocellReading)},
    {"Humid1",        String(Humid1)},
    {"Humid2",        String(Humid2)},
    {"Humid3",        String(Humid3)},
    {"Humid4",        String(Humid4)},
    {"HumidMoy",      String(HumidMoy)},
    {"PontDiv",       String(PontDiv)},
    {"WakeUp",        String(WakeUp)},
    {"ArrosageManu",  String(ArrosageManu)},
    {"SeuilSec",      String(SeuilSec)},
    {"FreqWakeUp",    String(FreqWakeUp)},
    {"SeuilPontDiv",  String(SeuilPontDiv)},
    {"mail",          inputMessageMailAd},
    {"mailNotif",     enableEmailChecked},
    {"HeureArrosage", String(HeureArrosage)},
    {"resetMode",     String(resetMode)},
    {"etatPompe",     String(etatPompe)},
    {"tempsArrosage", String(tempsArrosageSec)},
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
      "&TempAir=" + String(temperatureAir) +
      "&Humidite=" + String(h) +
      "&Luminosite=" + String(photocellReading) +
      "&Humid1=" + String(Humid1) +
      "&Humid2=" + String(Humid2) +
      "&Humid3=" + String(Humid3) +
      "&Humid4=" + String(Humid4) +
      "&HumidMoy=" + String(HumidMoy) +
      "&PontDiv=" + String(PontDiv) +
      "&WakeUp=" + String(WakeUp) +
      "&ArrosageManu=" + String(ArrosageManu) +
      "&SeuilSec=" + String(SeuilSec) +
      "&FreqWakeUp=" + String(FreqWakeUp) +
      "&SeuilPontDiv=" + String(SeuilPontDiv) +
      "&mail=" + inputMessageMailAd +
      "&mailNotif=" + enableEmailChecked +
      "&HeureArrosage=" + String(HeureArrosage) +
      "&resetMode=" + String(resetMode) +
      "&etatPompe=" + String(etatPompe) +
      "&tempsArrosage=" + String(tempsArrosageSec) +
      "&bootCount=" + String(bootCount);
  Serial.println("[SERVER][POST][PAYLOAD] " + postPreview);
  cfg.onSending = []() {
    if (displayOk) { display.fillCircle(5, 5, 5, WHITE); display.display(); }
  };
  cfg.onResult = [](int code) {
    if (code == 200) {
      Serial.println("Envoi BDD: OK");
    } else {
      Serial.printf("Envoi BDD: erreur HTTP %d\n", code);
    }
  };

  int code = n3DataPost(cfg);
  (void)code;
  delay(500);
}

void variablestoesp() {
  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[SERVER][GET] Lecture config distante depuis %s\n", serverNameOutput);

    outputsState = n3DataGet(serverNameOutput, &httpResponseCode);
    delay(OUTPUTS_FETCH_DELAY_MS);

    Serial.printf("[SERVER][GET] HTTP=%u\n", httpResponseCode);
    Serial.println("[SERVER][GET][BODY] " + outputsState);
    if (httpResponseCode == 0) {
      Serial.println("[SERVER][GET][WARN] Requete echouee, config distante ignoree");
      return;
    }
    JSONVar myObject = JSON.parse(outputsState);
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("[SERVER][GET][WARN] JSON invalide, config distante ignoree");
      return;
    }
    Serial.print("GPIO bdd : ");
    Serial.println(myObject);

    String pumpGpioKey = String(POMPE);
    int pumpState = readIntByKey(myObject, pumpGpioKey.c_str(), 0);
    Serial.print("variable Pompe est ");
    Serial.println(pumpState);
    pinMode(POMPE, OUTPUT);
    digitalWrite(POMPE, pumpState);

    String relaisGpioKey = String(RELAIS);
    ArrosageManu = readIntByKey(myObject, relaisGpioKey.c_str(), ArrosageManu);
    int parsedResetMode = resetMode ? 1 : 0;
    int parsedWakeUp = WakeUp ? 1 : 0;
    int parsedFreqWakeUp = FreqWakeUp;
    bool hasResetMode = tryReadIntByKey(myObject, "110", &parsedResetMode);
    bool hasWakeUp = tryReadIntByKey(myObject, "106", &parsedWakeUp);
    bool hasFreqWakeUp = tryReadIntByKey(myObject, "107", &parsedFreqWakeUp);
    String raw110 = readStringByKey(myObject, "110", "<absent>");
    String raw106 = readStringByKey(myObject, "106", "<absent>");
    String raw107 = readStringByKey(myObject, "107", "<absent>");

    if (!hasResetMode) {
      Serial.printf("[SERVER][GET][WARN] Cle 110 absente/invalide (raw=%s), conservation=%d\n",
                    raw110.c_str(), resetMode ? 1 : 0);
    } else {
      resetMode = (parsedResetMode != 0);
    }

    if (!hasWakeUp) {
      Serial.printf("[SERVER][GET][WARN] Cle 106 absente/invalide (raw=%s), conservation=%d\n",
                    raw106.c_str(), WakeUp ? 1 : 0);
    } else {
      WakeUp = (parsedWakeUp != 0);
    }

    if (!hasFreqWakeUp) {
      Serial.printf("[SERVER][GET][WARN] Cle 107 absente/invalide (raw=%s), conservation=%d\n",
                    raw107.c_str(), FreqWakeUp);
    } else if (parsedFreqWakeUp < 1 || parsedFreqWakeUp > 86400) {
      Serial.printf("[SERVER][GET][WARN] Cle 107 hors plage (raw=%s parsed=%d), conservation=%d\n",
                    raw107.c_str(), parsedFreqWakeUp, FreqWakeUp);
    } else {
      FreqWakeUp = parsedFreqWakeUp;
    }
    inputMessageMailAd = readStringByKey(myObject, "100", inputMessageMailAd);
    enableEmailChecked = readStringByKey(myObject, "101", enableEmailChecked);
    SeuilSec = readIntByKey(myObject, "102", SeuilSec);
    SeuilPontDiv = readIntByKey(myObject, "103", SeuilPontDiv);
    HeureArrosage = readIntByKey(myObject, "104", HeureArrosage);
    tempsArrosageSec = readIntByKey(myObject, "105", tempsArrosageSec);
    Serial.printf("[SERVER][GET][APPLY] 110:%s=>%d 106:%s=>%d 107:%s=>%d\n",
                  raw110.c_str(), resetMode ? 1 : 0,
                  raw106.c_str(), WakeUp ? 1 : 0,
                  raw107.c_str(), FreqWakeUp);
    Serial.printf("[SERVER][GET] resetMode=%d wakeUp=%d sleep=%d\n",
                  resetMode ? 1 : 0,
                  WakeUp ? 1 : 0,
                  FreqWakeUp);
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
  cfg.timeoutMs = 5000;
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
    Serial.println("WiFi : echec. Tous les reseaux ont echoue.");
    HeureSansWifi();
    if (displayOk) { display.println("ECHEC"); display.display(); }
  };
  cfg.onSuccess = [](const char* s) {
    (void)s;
    if (displayOk) { display.println(Wifiactif); display.println("OK"); display.display(); }
  };

  n3WifiConnect(cfg, &Wifiactif);

  delay(100);
  Serial.print("Reseau wifi: ");
  Serial.println(Wifiactif);
  Serial.print("Adresse IP : ");
  Serial.println(WiFi.localIP());
}
