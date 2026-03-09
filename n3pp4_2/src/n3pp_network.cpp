#include "n3pp_network.h"
#include "n3pp_globals.h"
#include <HTTPClient.h>
#include <Arduino_JSON.h>
#include "n3_wifi.h"
#include "n3_hmac.h"
#include "n3pp_automation.h"

//requête pour savoir si olution répond pour la récupération de variables
String httpGETRequest(const char* serverNameOutput) {
  WiFiClient client;
  HTTPClient http;

  http.begin(client, serverNameOutput);  // Initialisation de la requête
  httpResponseCode = http.GET();     // Envoi de la requête GET

  String payload = "{}";  // Si la réponse est vide, retourner un objet vide

  if (httpResponseCode > 0) {
    payload = http.getString();  // Obtenir la réponse sous forme de chaîne de caractères
  }
  http.end();      // Libération des ressources
  return payload;  // Retourne la réponse HTTP reçue
}

void datatobdd() {
  Serial.println("DATATOBDD!!!");
  if (displayOk) { display.drawCircle(5, 5, 5, WHITE); display.display(); }
  if (WiFi.status() == WL_CONNECTED) {  // Vérifier la connexion WiFi
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNamePostData);                               // URL du serveur (domaine ou IP + chemin)
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");  // Type de contenu POST
    // Construction des données du corps de la requête POST
    String httpRequestData = "api_key=" + apiKeyValue + "&sensor=" + sensorName
                             + "&version=" + version + "&TempAir=" + String(temperatureAir)
                             + "&Humidite=" + String(h) + "&Luminosite=" + photocellReading
                             + "&Humid1=" + String(Humid1) + "&Humid2=" + String(Humid2) + "&Humid3=" + String(Humid3) + "&Humid4=" + String(Humid4)
                             + "&HumidMoy=" + HumidMoy
                             + "&PontDiv=" + String(analogRead(pontdiv))
                             + "&WakeUp=" + WakeUp
                             + "&ArrosageManu=" + ArrosageManu
                             + "&SeuilSec=" + SeuilSec
                             + "&FreqWakeUp=" + FreqWakeUp
                             + "&SeuilPontDiv=" + SeuilPontDiv
                             + "&mail=" + inputMessageMailAd + "&mailNotif=" + enableEmailChecked
                             + "&HeureArrosage=" + HeureArrosage
                             + "&resetMode=" + resetMode
                             + "&etatPompe=" + etatPompe
                             + "&tempsArrosage=" + tempsArrosageSec
                             + "&bootCount=" + bootCount
                             + "";
    n3HmacSignRequest(http, API_KEY, httpRequestData.c_str());
    if (displayOk) { display.fillCircle(5, 5, 5, WHITE); display.display(); }
    Serial.println(httpRequestData);
    int httpResponseCode = http.POST(httpRequestData);
    Serial.println(httpResponseCode);
    if (httpResponseCode == 200) {
      Serial.println("Envoi BDD: OK");
    } else {
      Serial.printf("Envoi BDD: erreur HTTP %d\n", httpResponseCode);
    }
    delay(500);

    /*// reset Mode
      if (resetMode == 1){
        resetMode = 0;
        ESP.restart();
      }*/
  }
}

//Etape de changement d'état de pin en fonction de la bdd
void variablestoesp() {
  if (displayOk) { display.drawCircle(115, 5, 5, WHITE); display.display(); }
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("recup info bdd");

    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNameOutput);  // URL du serveur (état des sorties)
    outputsState = httpGETRequest(serverNameOutput);
    delay(2000);

    Serial.println(outputsState);
    //WebSerial.println(outputsState);
    JSONVar myObject = JSON.parse(outputsState);
    // JSON.typeof(jsonVar) can be used to get the type of the var
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Erreur : parsing JSON echoue.");
      //WebSerial.println("Parsing input failed!");
      return;
    }
    Serial.print("GPIO bdd : ");
    //WebSerial.print("GPIO bdd : ");
    Serial.println(myObject);
    //WebSerial.println(myObject);

    // myObject.keys() can be used to get an array of all the keys in the object
    JSONVar keys = myObject.keys();

    JSONVar Pompe = myObject[keys[0]];
    Serial.print("variable Pompe est ");
    Serial.println(Pompe);
    pinMode(atoi(keys[0]), OUTPUT);
    digitalWrite(atoi(keys[0]), atoi(Pompe));
    Serial.print("variable Pompe est ");
    Serial.println(Pompe);

    JSONVar JArrosageManu = myObject[keys[1]];
    Serial.print("variable JArrosageManu est ");
    Serial.println(JArrosageManu);
    ArrosageManu = atoi(JArrosageManu);
    Serial.print("variable ArrosageManu est ");
    Serial.println(ArrosageManu);

    JSONVar Jreset = myObject[keys[2]];
    Serial.print("variable Jreset est ");
    Serial.println(Jreset);
    resetMode = atoi(Jreset);
    Serial.print("variable reset est ");
    Serial.println(resetMode);

    String Jmail = myObject[keys[3]];
    Serial.print("variable Jmail est ");
    Serial.println(Jmail);
    inputMessageMailAd = Jmail;

    String JmailNotif = myObject[keys[4]];
    Serial.print("variable JmailNotif est ");
    Serial.println(JmailNotif);
    enableEmailChecked = JmailNotif;

    JSONVar JSeuilSec = myObject[keys[5]];
    Serial.print("variable JSeuilSec est ");
    Serial.println(JSeuilSec);
    SeuilSec = atoi(JSeuilSec);
    Serial.print("variable SeuilSec est ");
    Serial.println(SeuilSec);

    JSONVar JSeuilPontDiv = myObject[keys[6]];
    Serial.print("variable JtankTres est ");
    Serial.println(JSeuilPontDiv);
    SeuilPontDiv = atoi(JSeuilPontDiv);

    JSONVar JHeureArrosage = myObject[keys[7]];
    Serial.print("variable JHeureArrosage est ");
    Serial.println(JHeureArrosage);
    HeureArrosage = atoi(JHeureArrosage);

    JSONVar JtempsArrosage = myObject[keys[8]];
    Serial.print("variable JtempsArrosage est ");
    Serial.println(JtempsArrosage);
    tempsArrosageSec = atoi(JtempsArrosage);

    JSONVar JWakeUp = myObject[keys[9]];
    Serial.print("variable est ");
    Serial.println(JWakeUp);
    WakeUp = atoi(JWakeUp);

    JSONVar JFreqWakeUp = myObject[keys[10]];
    Serial.print("variable est ");
    Serial.println(JFreqWakeUp);
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
