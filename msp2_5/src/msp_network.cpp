/* MeteoStationPrototype (msp1) — Réseau
 * httpGETRequest, datatobdd, variablestoesp, Wificonnect
 */

#include "msp_network.h"
#include "msp_config.h"
#include "msp_globals.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "n3_wifi.h"
#include "n3_hmac.h"

// Fonction pour envoyer une requête HTTP GET
String httpGETRequest(const char* serverNameOutput) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverNameOutput);  // Initialisation de la requête
  httpResponseCode = http.GET();         // Envoi de la requête GET

  String payload = "{}";  // Si la réponse est vide, retourner un objet vide

  if (httpResponseCode > 0) {
    payload = http.getString();  // Obtenir la réponse sous forme de chaîne de caractères
  }
  http.end();      // Libération des ressources
  return payload;  // Retourne la réponse HTTP reçue
}

// Fonction d'envoi des données vers la base de données
void datatobdd() {
  display.drawCircle(5, 5, 5, WHITE);
  display.display();
  if (WiFi.status() == WL_CONNECTED) {  // Vérifie l'état de connexion WiFi
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNamePostData);                               // Initialisation requête POST
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");  // Spécifie le type de contenu
    String httpRequestData = "api_key=" + apiKeyValue + "&sensor=" + sensorName
                             + "&version=" + version
                             + "&TempAirInt=" + tempAirInt
                             + "&TempAirExt=" + tempAirExt
                             + "&HumidAirInt=" + humidAirInt
                             + "&HumidAirExt=" + humidAirExt
                             + "&LuminositeA=" + photocellReadingA
                             + "&LuminositeB=" + photocellReadingB
                             + "&LuminositeC=" + photocellReadingC
                             + "&LuminositeD=" + photocellReadingD
                             + "&LuminositeMoy=" + photocellReadingMoy
                             + "&ServoHB=" + AngleServoHB
                             + "&ServoGD=" + AngleServoGD
                             + "&HumidSol=" + HumidSol
                             + "&Pluie=" + Pluie
                             + "&TempEau=" + temperatureSol
                             + "&PontDiv=" + PontDiv
                             + "&WakeUp=" + WakeUp
                             + "&SeuilSec=" + SeuilSec
                             + "&FreqWakeUp=" + FreqWakeUp
                             + "&SeuilPontDiv=" + SeuilPontDiv
                             + "&mail=" + inputMessageMailAd + "&mailNotif=" + enableEmailChecked
                             + "&resetMode=" + resetMode
                             + "&bootCount=" + bootCount;
    n3HmacSignRequest(http, API_KEY, httpRequestData.c_str());
    display.fillCircle(5, 5, 5, WHITE);
    display.display();
    httpResponseCode = http.POST(httpRequestData);  // Envoi des données
    delay(500);
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
  }
}

// Etape de changement d'état de pin en fonction de la bdd
void variablestoesp() {
  display.drawCircle(115, 5, 5, WHITE);
  display.display();
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNameOutput);  // Votre nom de domaine avec le chemin d'URL ou l'adresse IP avec le chemin
    outputsState = httpGETRequest(serverNameOutput);
    delay(200);
    if (httpResponseCode > 0) {

      JSONVar myObject = JSON.parse(outputsState);  // Parsage de l'objet JSON
      if (JSON.typeof(myObject) == "undefined") {
        return;
      }
      JSONVar keys = myObject.keys();  // Récupération des clés de l'objet JSON
                                       /*
      JSONVar Pompe = myObject[keys[0]];
      pinMode(atoi(keys[0]), OUTPUT);  // Paramétrage du pin selon données
      digitalWrite(atoi(keys[0]), atoi(Pompe));
      delay(100);*/

      JSONVar Jreset = myObject[keys[2]];
      resetMode = atoi(Jreset);
      delay(100);

      String Jmail = myObject[keys[3]];
      inputMessageMailAd = Jmail;
      delay(100);

      String JmailNotif = myObject[keys[4]];
      enableEmailChecked = JmailNotif;
      delay(100);

      JSONVar JSeuilSec = myObject[keys[5]];
      SeuilSec = atoi(JSeuilSec);
      delay(100);

      JSONVar JSeuilPontDiv = myObject[keys[6]];
      SeuilPontDiv = atoi(JSeuilPontDiv);
      delay(100);

      JSONVar JAngleServoHB = myObject[keys[7]];
      AngleServoHB = atoi(JAngleServoHB);
      delay(100);

      JSONVar JAngleServoGD = myObject[keys[8]];
      AngleServoGD = atoi(JAngleServoGD);
      delay(100);

      JSONVar JWakeUp = myObject[keys[9]];
      WakeUp = atoi(JWakeUp);
      delay(100);

      JSONVar JFreqWakeUp = myObject[keys[10]];
      FreqWakeUp = atoi(JFreqWakeUp);
      delay(100);
    }
    display.fillCircle(115, 5, 5, WHITE);
    display.display();
  }
}

// Connection à un des réseaux WiFi disponibles (scan + RSSI + BSSID via n3_wifi)
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
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("Wifi");
    display.println("CONNEXION...");
    display.display();
  };
  cfg.onFailure = []() {
    display.println("ECHEC");
    display.display();
  };
  cfg.onSuccess = [](const char*) {
    display.println(Wifiactif);
    display.println("OK");
    display.display();
  };

  n3WifiConnect(cfg, &Wifiactif);
  delay(100);
}
