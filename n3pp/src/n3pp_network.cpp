#include "n3pp_network.h"
#include "n3pp_globals.h"
#include <Arduino_JSON.h>
#include "n3_wifi.h"
#include "n3_data.h"
#include "n3pp_automation.h"

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
    delay(2000);

    Serial.println(outputsState);
    JSONVar myObject = JSON.parse(outputsState);
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Erreur : parsing JSON echoue.");
      return;
    }
    Serial.print("GPIO bdd : ");
    Serial.println(myObject);

    JSONVar keys = myObject.keys();

    JSONVar Pompe = myObject[keys[0]];
    Serial.print("variable Pompe est ");
    Serial.println(Pompe);
    pinMode(atoi(keys[0]), OUTPUT);
    digitalWrite(atoi(keys[0]), atoi(Pompe));

    JSONVar JArrosageManu = myObject[keys[1]];
    ArrosageManu = atoi(JArrosageManu);

    JSONVar Jreset = myObject[keys[2]];
    resetMode = atoi(Jreset);

    String Jmail = myObject[keys[3]];
    inputMessageMailAd = Jmail;

    String JmailNotif = myObject[keys[4]];
    enableEmailChecked = JmailNotif;

    JSONVar JSeuilSec = myObject[keys[5]];
    SeuilSec = atoi(JSeuilSec);

    JSONVar JSeuilPontDiv = myObject[keys[6]];
    SeuilPontDiv = atoi(JSeuilPontDiv);

    JSONVar JHeureArrosage = myObject[keys[7]];
    HeureArrosage = atoi(JHeureArrosage);

    JSONVar JtempsArrosage = myObject[keys[8]];
    tempsArrosageSec = atoi(JtempsArrosage);

    JSONVar JWakeUp = myObject[keys[9]];
    WakeUp = atoi(JWakeUp);

    JSONVar JFreqWakeUp = myObject[keys[10]];
    FreqWakeUp = atoi(JFreqWakeUp);
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
