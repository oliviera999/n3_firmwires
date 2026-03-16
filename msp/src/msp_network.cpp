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

void datatobdd() {
  if (displayOk) { display.drawCircle(5, 5, 5, WHITE); display.display(); }

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

  n3DataPost(cfg);
  delay(500);
  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
}

void variablestoesp() {
  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() == WL_CONNECTED) {
    outputsState = n3DataGet(serverNameOutput, &httpResponseCode);
    delay(200);
    if (httpResponseCode > 0) {
      JSONVar myObject = JSON.parse(outputsState);
      if (JSON.typeof(myObject) == "undefined") {
        return;
      }
      JSONVar keys = myObject.keys();

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

      JSONVar JAngleServoHB = myObject[keys[7]];
      AngleServoHB = atoi(JAngleServoHB);

      JSONVar JAngleServoGD = myObject[keys[8]];
      AngleServoGD = atoi(JAngleServoGD);

      JSONVar JWakeUp = myObject[keys[9]];
      WakeUp = atoi(JWakeUp);

      JSONVar JFreqWakeUp = myObject[keys[10]];
      FreqWakeUp = atoi(JFreqWakeUp);
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
    Serial.println("WiFi : echec. Tous les reseaux ont echoue.");
    if (displayOk) { display.println("ECHEC"); display.display(); }
  };
  cfg.onSuccess = [](const char*) {
    if (displayOk) { display.println(Wifiactif); display.println("OK"); display.display(); }
  };

  n3WifiConnect(cfg, &Wifiactif);
  delay(100);
}
