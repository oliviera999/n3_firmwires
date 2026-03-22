#include "n3pp_network.h"
#include "n3pp_globals.h"
#include <Arduino_JSON.h>
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
    Serial.println("recup info bdd");

    outputsState = n3DataGet(serverNameOutput, &httpResponseCode);
    delay(OUTPUTS_FETCH_DELAY_MS);

    Serial.println(outputsState);
    JSONVar myObject = JSON.parse(outputsState);
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Erreur : parsing JSON echoue.");
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
    resetMode = readIntByKey(myObject, "110", resetMode);
    inputMessageMailAd = readStringByKey(myObject, "100", inputMessageMailAd);
    enableEmailChecked = readStringByKey(myObject, "101", enableEmailChecked);
    SeuilSec = readIntByKey(myObject, "102", SeuilSec);
    SeuilPontDiv = readIntByKey(myObject, "103", SeuilPontDiv);
    HeureArrosage = readIntByKey(myObject, "104", HeureArrosage);
    tempsArrosageSec = readIntByKey(myObject, "105", tempsArrosageSec);
    WakeUp = readIntByKey(myObject, "106", WakeUp);
    FreqWakeUp = readIntByKey(myObject, "107", FreqWakeUp);
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
