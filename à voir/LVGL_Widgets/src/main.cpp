// --- Début fusion FFP3 ---
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <HTTPClient.h>
#include <ESP_Mail_Client.h>
#include <Arduino_JSON.h>
#include <LittleFS.h>
#include <ESP32Servo.h>
#include "Ultrasonic.h"
#include <esp_sleep.h>
#include <ESP32Time.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include "credentials.h"

constexpr int MAX_ULTRASON_DISTANCE = 90;
constexpr int MIN_ULTRASON_DISTANCE = 2;
constexpr int DEFAULT_NUM_READINGS = 5;

Ultrasonic ultrasonAqua(4);
Ultrasonic ultrasonTank(19);
Ultrasonic ultrasonPota(33);
#define EAUPOTA 33
#define LUMINOSITE 3
#define POMPEAQUA      15
#define POMPERESERV    16
#define RADIATEURS     2
#define LUMIERE        15
#define BOUFFEGROS     6
#define BOUFFEPETITS   7
#define DHTPIN         46
const unsigned int oneWireBus = 8;

bool bouffeMatinOk;
bool bouffeMidiOk;
bool bouffeSoirOk;
int lastJourBouf = -1;
String bouffePetits = "0";
String bouffeGros = "0";
bool resetMode = 0;
String StrTempsGros = "10";
String StrTempsPetits = "10";
unsigned int tempsGros = 10;
unsigned int tempsPetits = 10;
unsigned int tpsGmilAvt;
unsigned int tpsGmilArr;
unsigned int tpsPmilAvt;
unsigned int tpsPmilArr;
bool tankPumpLocked = 0;
bool emailFlooding = 0;
bool emailFloodRisk = 0;
unsigned int limFlood = 8;
bool pompeAquaLocked = false;
unsigned int tempsRemplissage;
unsigned int tempsRemplissageSec = 120;
// OLED supprimé
enum { /* ... */ };
unsigned int AWL;
unsigned int AWLmax = 0;
unsigned int TWL;
unsigned int WL;
unsigned int photocellReading;
float temperatureEau;
float temperatureAir;
float h;
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);
Servo myservobig;
Servo myservosmall;
#define emailSubject "Information FFP3"
// SMTP credentials: voir include/credentials.h (ignoré par git)
String inputMessageMailAd = DEST_EMAIL;
String enableEmailChecked = "checked";
String emailMessage;
SMTPSession smtp;
bool ChauffageOn = 1;
time_t now;
String inputMessageAqLim = "18";
String inputMessageTaLim = "80";
String inputMessageChauffage = "18";
String inputMessageMatFeed = "8";
String inputMessageMidFeed = "12";
String inputMessageSoirFeed = "18";
const char* serverNamePostData = "http://iot.olution.info/ffp3/ffp3datas/post-ffp3-data2.php";
const char* serverNameOutput = "http://iot.olution.info/ffp3/ffp3control/ffp3-outputs-action2.php?action=outputs_state&board=1";
String version = "4.0";
String apiKeyValue = API_KEY_VALUE;
String sensorName = "FFP3";
String sensorLocation = "T06";
OneWire oneWire(oneWireBus);
DallasTemperature sensors(&oneWire);
ESP32Time rtc;
Preferences preferences;
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;
const char* ntpServer = "pool.ntp.org";
const long gmtOffset_sec = 0;
const int daylightOffset_sec = 0;
#define TIME_TO_STOP FreqWakeUp
#define uS_TO_S_FACTOR 1000000ULL
RTC_DATA_ATTR bool WakeUp = 0;
RTC_DATA_ATTR int FreqWakeUp = 600;
RTC_DATA_ATTR uint64_t last_sleep_time_us = 0;
int numReadings = 5;
int total1 = 0, total2 = 0, totalWL = 0, totalAWL = 0, totalTWL = 0;
int average1 = 0, average2 = 0, averageWL = 0, averageAWL = 0, averageTWL = 0;
bool wifiWasConnected = false;


// Fonction utilitaire générique pour requêtes HTTP (GET/POST)
String httpRequest(const char* url, const char* method = "GET", const String& payload = "", const char* contentType = "application/x-www-form-urlencoded") {
  WiFiClient client;
  HTTPClient http;
  String response = "{}";
  int httpResponseCode = -1;

  if (WiFi.status() == WL_CONNECTED) {
      http.begin(client, url);
      if (String(method) == "POST") {
          http.addHeader("Content-Type", contentType);
          httpResponseCode = http.POST(payload);
      } else {
          httpResponseCode = http.GET();
      }
      if (httpResponseCode > 0) {
          response = http.getString();
      } else {
          Serial.print("Erreur HTTP, code : ");
          Serial.println(httpResponseCode);
      }
      http.end();
  }
  return response;
}
struct UserConfig {
  String inputMessageAqLim;
  String inputMessageTaLim;
  String inputMessageChauffage;
  String inputMessageMatFeed;
  String inputMessageMidFeed;
  String inputMessageSoirFeed;
  String StrTempsGros;
  String StrTempsPetits;
  unsigned int limFlood;
  unsigned int tempsRemplissageSec;
  String inputMessageMailAd;
  String enableEmailChecked;
};
UserConfig config;

void saveConfig() {
  JSONVar j;
  j["inputMessageAqLim"] = config.inputMessageAqLim;
  j["inputMessageTaLim"] = config.inputMessageTaLim;
  j["inputMessageChauffage"] = config.inputMessageChauffage;
  j["inputMessageMatFeed"] = config.inputMessageMatFeed;
  j["inputMessageMidFeed"] = config.inputMessageMidFeed;
  j["inputMessageSoirFeed"] = config.inputMessageSoirFeed;
  j["StrTempsGros"] = config.StrTempsGros;
  j["StrTempsPetits"] = config.StrTempsPetits;
  j["limFlood"] = config.limFlood;
  j["tempsRemplissageSec"] = config.tempsRemplissageSec;
  j["inputMessageMailAd"] = config.inputMessageMailAd;
  j["enableEmailChecked"] = config.enableEmailChecked;
  File f = LittleFS.open("/config.json", "w");
  if (f) {
    f.print(JSON.stringify(j));
    f.close();
  }
}

void loadConfig() {
  if (!LittleFS.exists("/config.json")) return;
  File f = LittleFS.open("/config.json", "r");
  if (!f) return;
  String s = f.readString();
  f.close();
  JSONVar j = JSON.parse(s);
  if (JSON.typeof(j) == "undefined") return;
  config.inputMessageAqLim = (const char*)j["inputMessageAqLim"];
  config.inputMessageTaLim = (const char*)j["inputMessageTaLim"];
  config.inputMessageChauffage = (const char*)j["inputMessageChauffage"];
  config.inputMessageMatFeed = (const char*)j["inputMessageMatFeed"];
  config.inputMessageMidFeed = (const char*)j["inputMessageMidFeed"];
  config.inputMessageSoirFeed = (const char*)j["inputMessageSoirFeed"];
  config.StrTempsGros = (const char*)j["StrTempsGros"];
  config.StrTempsPetits = (const char*)j["StrTempsPetits"];
  config.limFlood = (int)j["limFlood"];
  config.tempsRemplissageSec = (int)j["tempsRemplissageSec"];
  config.inputMessageMailAd = (const char*)j["inputMessageMailAd"];
  config.enableEmailChecked = (const char*)j["enableEmailChecked"];
}

// Configuration et envoi d'un email d'alerte (SMTP)
void sendEmailNotification() {
  /* Paramètres de session SMTP (serveur, port, identifiants) */
  Session_Config config;

  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;

  /* Message à envoyer */
  SMTP_Message message;

  /* En-têtes du message (expéditeur, destinataire, sujet) */
  message.sender.name = F("OAL");
  message.sender.email = AUTHOR_EMAIL;

  message.subject = emailSubject;

  message.addRecipient(F("OAL"), inputMessageMailAd);

  message.text.content = emailMessage;

  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;

  /* Connexion au serveur SMTP */
  if (!smtp.connect(&config)) {
    MailClient.printf("SMTP erreur connexion, Status: %d, Error: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  if (!smtp.isLoggedIn()) {
    Serial.println("SMTP : pas encore connecte.");
  } else {
    if (smtp.isAuthenticated())
      Serial.println("SMTP : authentification reussie.");
    else
      Serial.println("SMTP : connecte sans auth.");
  }

  /* Envoi de l'email puis fermeture de la session */
  if (!MailClient.sendMail(&smtp, &message))
    MailClient.printf("SMTP erreur envoi, Status: %d, Error: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());

  // Vider le journal des résultats d'envoi (évite accumulation en mémoire)
  smtp.sendingResult.clear();
}


unsigned long currentMillisDelta;
unsigned long previousMillisDelta = 0;
const long intervalDelta = 20000;
int WakeUpButton = 0;
int diffMaree;
bool mesureMaree = 0;
//#define TOUCH_WAKEUP_PIN 4  // GPIO du touchpad à utiliser (T0 = GPIO 4)

String Wifiactif;

// Intervalle entre deux lectures capteurs (en ms)
unsigned long previousMillisDatas = 0;
const long intervalDatas = 120000;

// Serveur web asynchrone sur le port 80 (interface locale)
AsyncWebServer server(80);

//récupération du temps sur un serveur NTP
WiFiUDP wifiUdp;

// Variable pour le contrôle des pins à distance
String outputsState;
String variablesState;

// Code de réponse HTTP (requêtes GET/POST)
unsigned int httpResponseCode;

// Indicateurs d'envoi d'email déjà effectué (évite spam)
bool emailNiveauxSent = false;
bool emailChauffageSent = false;
bool emailTankSent = false;

// Objet SMTP : config et données d'envoi (ESP Mail Client)
bool oledPresent = true;

/*void //safe//display() {
  if (oledPresent) //display.//display();  // Affiche le contenu du buffer d'affichage OLED
  else Serial.println("OLED absente, pas de mise a jour affichage.");
}*/

/**
 * \brief Affiche la raison du réveil
 *
 * Utilise la fonction esp_sleep_get_wakeup_cause() pour déterminer la raison du réveil
 * et l'affiche sur la console série (ou sur l'écran OLED si celui-ci est présent)
 */
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;

  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("Reveil : signal externe (RTC_IO)");
      WakeUpButton = 1;
      if (oledPresent) {

      //display.clear//display();
      //display.println("Load H RTC");
      //display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
      //safe//display();
      delay(500);
      }
      break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Reveil : signal externe (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("Reveil : timer");
      if (oledPresent) {

      //display.clear//display();
      //display.println("Load H RTC");
      //display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
      //safe//display();
      }
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Reveil : touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Reveil : programme ULP"); break;
    default:
      Serial.printf("Reveil non cause par deep sleep : %d\n", wakeup_reason);
     // HeureSansWifi();
      break;
  }
}

/**
 * \brief Sends an HTTP GET request to the specified server.
 *
 * This function utilizes the httpRequest utility to perform an HTTP
 * GET request to the server specified by the provided URL.
 *
 * \param serverNameOutput The URL of the server to which the GET request is sent.
 * \return A string containing the response payload from the server.
 */

String httpGETRequest(const char* serverNameOutput) {
  return httpRequest(serverNameOutput, "GET");
}

void datatobdd() {
    Serial.println("DATATOBDD!!!");
    //display.drawCircle(5, 5, 5, WHITE);
    //safe//display();
    
    if (WiFi.status() == WL_CONNECTED) {
        char httpRequestData[1024];
        int len = snprintf(httpRequestData, sizeof(httpRequestData),
            "api_key=%s&sensor=%s&version=%s&TempAir=%.1f&Humidite=%.1f&TempEau=%.1f&EauPotager=%u&EauAquarium=%u&EauReserve=%u&diffMaree=%d&Luminosite=%u&etatPompeAqua=%d&etatPompeTank=%d&etatHeat=%d&etatUV=%d&bouffeMatin=%s&bouffeMidi=%s&bouffeSoir=%s&bouffePetits=%s&bouffeGros=%s&tempsGros=%s&tempsPetits=%s&aqThreshold=%s&tankThreshold=%s&chauffageThreshold=%s&mail=%s&mailNotif=%s&resetMode=%d&tempsRemplissageSec=%u&limFlood=%u&WakeUp=%d&FreqWakeUp=%d",
            apiKeyValue.c_str(), sensorName.c_str(), version.c_str(), temperatureAir, h, temperatureEau, WL, AWL, TWL, diffMaree, photocellReading,
            digitalRead(POMPEAQUA), digitalRead(POMPERESERV), digitalRead(RADIATEURS), digitalRead(LUMIERE),
            config.inputMessageMatFeed.c_str(), config.inputMessageMidFeed.c_str(), config.inputMessageSoirFeed.c_str(),
            bouffePetits.c_str(), bouffeGros.c_str(), config.StrTempsGros.c_str(), config.StrTempsPetits.c_str(),
            config.inputMessageAqLim.c_str(), config.inputMessageTaLim.c_str(), config.inputMessageChauffage.c_str(),
            config.inputMessageMailAd.c_str(), config.enableEmailChecked.c_str(), resetMode, config.tempsRemplissageSec,
            config.limFlood, WakeUp, FreqWakeUp
        );

        if (len < 0 || len >= sizeof(httpRequestData)) {
            Serial.println("Erreur : snprintf ou taille buffer insuffisante.");
            return;
        }

        String response;
        response = httpRequest(serverNamePostData, "POST", httpRequestData);
        Serial.println(response);
        /*
        try {
            response = httpRequest(serverNamePostData, "POST", httpRequestData);
        } catch (const std::exception& e) {
            Serial.printf("Exception caught during HTTP request: %s\n", e.what());
            return;
        }

        if (response.isEmpty()) {
            Serial.println("Erreur : pas de reponse ou reponse vide.");
        } else {
            Serial.println(response);
        }*/
    } else {
        Serial.println("Erreur : WiFi non connecte.");
    }
}

// Fonctions en lien avec nourrir les poissons

// Fonction utilitaire pour afficher l'état du nourrissage sur l'écran OLED
/*void afficherNourrissage(const char* type, const char* phase, int temps) {
  if (!oledPresent) return;
  //display.clear//display();
  //display.setTextSize(2);
  //display.setCursor(0, 0);
  //display.println("Nourriture");
  //display.println(type);
  //display.print(phase);
  if (temps >= 0) {
      //display.println(temps);
  }
  //safe//display();
}*/

// Fonction générique pour nourrir les poissons
void nourrissagePoissons(Servo& servo, const char* type, int duree, int angle, int repos) {
  int dureeArr = duree / 2;
  // Phase aller
  Serial.println(String("Début de la mise à jour ") + type);
  for (int i = 0; i < duree; i++) {
      //afficherNourrissage(type, "Temps av", duree - i);
      servo.write(angle);
      delay(1000);
  }
  // Phase retour
  for (int i = 0; i < dureeArr; i++) {
      //afficherNourrissage(type, "Temps arr", dureeArr - i);
      servo.write(angle);
      delay(1000);
  }
  // Position repos
  servo.write(repos);
  //afficherNourrissage(type, "FIN", -1);
  datatobdd();
}

// Ancienne fonction nourrissageGros, maintenant un simple appel paramétré
void nourrissageGros() {
  int temps = config.StrTempsGros.toInt();
  nourrissagePoissons(myservobig, "GROS", temps, 140, 88);
}

// Ancienne fonction nourrissagePetits, maintenant un simple appel paramétré
void nourrissagePetits() {
  int temps = config.StrTempsPetits.toInt();
  nourrissagePoissons(myservosmall, "PETITS", temps, 140, 88);
}

void dataJSONtoesp() {
  //Etape de changement d'état de pin en fonction de la bdd
  //display.drawCircle(115, 5, 5, WHITE);
  //safe//display();
  if (WiFi.status() == WL_CONNECTED) {
    delay(200);
    //WiFiClient client;
    //HTTPClient http;
    //http.begin(client, serverNameOutput);  // Your Domain name with URL path or IP address with path
    //outputsState = httpGETRequest(serverNameOutput);

    String response = httpRequest(serverNameOutput);
    if (response != "{}") {
      outputsState = response;

    //httpResponseCode = http.POST(httpRequestData);
    Serial.println(outputsState);
    Serial.print("Longueur tableau : ");
    Serial.println(sizeof(outputsState));
    if (sizeof(outputsState) > 0) { 
    //if (httpResponseCode > 0) {
      Serial.println("ok");
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
      //myObject.keys()can be used to get an array of all the keys in the object
      JSONVar keys = myObject.keys();

      String Jmail = myObject[keys[7]];
      Serial.print("variable Jmail est ");
      Serial.println(Jmail);
      //WebSerial.print("variable Jmail est ");
      //WebSerial.println(Jmail);
      config.inputMessageMailAd = Jmail;

      String JmailNotif = myObject[keys[8]];
      Serial.print("variable JmailNotif est ");
      Serial.println(JmailNotif);
      //WebSerial.print("variable JmailNotif est ");
      //WebSerial.println(JmailNotif);
      config.enableEmailChecked = JmailNotif;

      String JaqTres = myObject[keys[9]];
      Serial.print("variable JaqTres est ");
      Serial.println(JaqTres);
      //WebSerial.print("variable JaqTres est ");
      //WebSerial.println(JaqTres);
      config.inputMessageAqLim = JaqTres;

      String JtankTres = myObject[keys[10]];
      Serial.print("variable JtankTres est ");
      Serial.println(JtankTres);
      //WebSerial.print("variable JtankTres est ");
      //WebSerial.println(JtankTres);
      config.inputMessageTaLim = JtankTres;

      String JchauffTres = myObject[keys[11]];
      Serial.print("variable JchauffTres est ");
      Serial.println(JchauffTres);
      //WebSerial.print("variable JchauffTres est ");
      //WebSerial.println(JchauffTres);
      config.inputMessageChauffage = JchauffTres;

      String JbouffMat = myObject[keys[12]];
      Serial.print("variable est ");
      Serial.println(JbouffMat);
      //WebSerial.print("variable est ");
      //WebSerial.println(JbouffMat);
      config.inputMessageMatFeed = JbouffMat;

      String JbouffMid = myObject[keys[13]];
      Serial.print("variable JbouffMid est ");
      Serial.println(JbouffMid);
      //WebSerial.print("variable JbouffMid est ");
      //WebSerial.println(JbouffMid);
      config.inputMessageMidFeed = JbouffMid;

      String JbouffSo = myObject[keys[14]];
      Serial.print("variable JbouffSo est ");
      Serial.println(JbouffSo);
      //WebSerial.print("variable JbouffSo est ");
      //WebSerial.println(JbouffSo);
      config.inputMessageSoirFeed = JbouffSo;
      Serial.print("variable bouffSo est ");
      Serial.println(config.inputMessageSoirFeed);
      //WebSerial.print("variable bouffSo est ");
      //WebSerial.println(config.inputMessageSoirFeed);

      String JbouffePetits = myObject[keys[4]];
      Serial.print("variable JbouffePetits est ");
      Serial.println(JbouffePetits);
      //WebSerial.print("variable JbouffePetits est ");
      //WebSerial.println(JbouffePetits);
      bouffePetits = JbouffePetits;
      Serial.print("variable bouffePetits est ");
      Serial.println(bouffePetits);
      //WebSerial.print("variable bouffePetits est ");
      //WebSerial.println(bouffePetits);

      String JbouffeGros = myObject[keys[5]];
      Serial.print("variable JbouffeGros est ");
      Serial.println(JbouffeGros);
      //WebSerial.print("variable JbouffeGros est ");
      //WebSerial.println(JbouffeGros);
      bouffeGros = JbouffeGros;
      Serial.print("variable bouffeGros est ");
      Serial.println(bouffeGros);
      //WebSerial.print("variable bouffeGros est ");
      //WebSerial.println(JbouffeGros);

      JSONVar Jreset = myObject[keys[6]];
      resetMode = atoi(Jreset);
      Serial.print("variable reset est ");
      Serial.println(resetMode);
      //WebSerial.print("variable reset est ");
      //WebSerial.println(resetMode);

      String JtempsGros = myObject[keys[15]];
      Serial.print("variable JtempsGros est ");
      Serial.println(JtempsGros);
      //WebSerial.print("variable JtempsGros est ");
      //WebSerial.println(JtempsGros);
      config.StrTempsGros = JtempsGros;
      Serial.print("variable tempsGros est ");
      Serial.println(config.StrTempsGros);
      //WebSerial.print("variable tempsGros est ");
      //WebSerial.println(config.StrTempsGros);

      String JtempsPetits = myObject[keys[16]];
      Serial.print("variable JtempsPetits est ");
      Serial.println(JtempsPetits);
      //WebSerial.print("variable JtempsPetits est ");
      //WebSerial.println(JtempsPetits);
      config.StrTempsPetits = JtempsPetits;
      Serial.print("variable tempsPetits est ");
      Serial.println(config.StrTempsPetits);
      //WebSerial.print("variable tempsPetits est ");
      //WebSerial.println(config.StrTempsPetits);

      JSONVar JtempsRemplissageSec = myObject[keys[17]];
      config.tempsRemplissageSec = atoi(JtempsRemplissageSec);
      Serial.print("variable temps de remplissage est ");
      Serial.println(config.tempsRemplissageSec);
      //WebSerial.print("variable temps de remplissage est ");
      //WebSerial.println(config.tempsRemplissageSec);

      JSONVar JlimFlood = myObject[keys[18]];
      config.limFlood = atoi(JlimFlood);
      Serial.print("variable débordement est ");
      Serial.println(config.limFlood);
      //WebSerial.print("variable temps de remplissage est ");
      //WebSerial.println(config.limFlood);

      JSONVar JWakeUp = myObject[keys[19]];
      WakeUp = atoi(JWakeUp);
      Serial.print("variable forçage éveil ");
      Serial.println(WakeUp);

      JSONVar JFreqWakeUp = myObject[keys[20]];
      FreqWakeUp = atoi(JFreqWakeUp);
      Serial.print("variable temps sommeil ");
      Serial.println(FreqWakeUp);

      for (int i = 0; i <= 3; i++) {
        JSONVar value = myObject[keys[i]];
        Serial.print("GPIO ");
        //WebSerial.print("GPIO ");
        Serial.print(keys[i]);
        //WebSerial.print(keys[i]);
        Serial.print(" est à l'état");
        //WebSerial.print(" est à l'état");
        Serial.println(value);
        //WebSerial.println(value);
        pinMode(atoi(keys[i]), OUTPUT);
        Serial.println("1");
        digitalWrite(atoi(keys[i]), atoi(value));
        Serial.println("2");
      }
      delay(500);
      Serial.println("données récupérées de la bdd");
    }
  }
}
  else {
      //display.drawLine(110, 0, 120, 10, WHITE);
      //safe//display();
      Serial.println("pas de connexion Wifi");
      //WebSerial.println("pas de connexion Wifi");
    }
  tempsRemplissage = config.tempsRemplissageSec * 1000;
}

void bouffeManuelle() {
  // Fonction pour nourrir manuellement les poissons
    //bouffe manuelle
    //petits
    if (bouffePetits == "1") {
      datatobdd();
      nourrissagePetits();
      bouffePetits = "0";
      datatobdd();
    }
    //gros
    if (bouffeGros == "1") {
      datatobdd();
      nourrissageGros();
      bouffeGros = "0";
      datatobdd();
    }
    //display.fillCircle(115, 5, 5, WHITE);
    //safe//display();
  }

//fonction de récupération des datas de la base de données de contrôle sur iot.olution.info
void variablestoesp() {
  dataJSONtoesp();
  bouffeManuelle();
  }

//tout ce qui concerne la connexion au Wifi
struct WifiCredential {
  const char* ssid;
  const char* password;
};

WifiCredential wifiList[] = {
  { WIFI_SSID_1, WIFI_PASSWORD_1 },
  //{ "raspN3", "n3LLrasp" },
  //{ "VOTRE_SSID_1", "VOTRE_MOT_DE_PASSE_1" },
  //{ "dlink", "n3LLdlink" },
  { WIFI_SSID_2, WIFI_PASSWORD_2 },

};

const int wifiTimeout = 8000; // ms

bool connectToWiFi() {
  if (oledPresent) {

  //display.clear//display();
  //display.setTextSize(2);
  //display.setCursor(0, 0);
  //display.println("Wifi");
  //display.println("SCAN...");
  //safe//display();
  }

  int n = WiFi.scanNetworks();
  if (n == 0) {
    //display.println("No networks");
    //safe//display();
    delay(1000);
    return false;
  }

  for (unsigned int i = 0; i < sizeof(wifiList) / sizeof(WifiCredential); i++) {
    for (int j = 0; j < n; j++) {
      String foundSSID = WiFi.SSID(j);
      if (foundSSID == wifiList[i].ssid) {
        WiFi.begin(wifiList[i].ssid, wifiList[i].password);
        if (oledPresent) {
        //display.clear//display();
        //display.setCursor(0, 0);
        //display.print("Try: ");
        //display.println(wifiList[i].ssid);
        //safe//display();
        }

        unsigned long startAttempt = millis();
        while (WiFi.status() != WL_CONNECTED && millis() - startAttempt < wifiTimeout) {
          delay(100);
        }
        if (WiFi.status() == WL_CONNECTED) {
          Wifiactif = wifiList[i].ssid;
          if (oledPresent) {
          //display.clear//display();
          //display.println("Connecte:");
          //display.println(wifiList[i].ssid);
          //safe//display();
          delay(500);
          }
          return true;
        }
      }
    }
  }
  if (oledPresent) {
  //display.clear//display();
  //display.println("Wifi FAILED");
  //safe//display();
  delay(1000);
  }
  return false;
  Serial.println("Wifi");

}

void printWiFiStatusReason() {
  wl_status_t status = WiFi.status();
  switch (status) {
    case WL_NO_SHIELD: Serial.println("🚫 Pas de shield Wi-Fi"); break;
    case WL_IDLE_STATUS: Serial.println("💤 Wi-Fi idle"); break;
    case WL_NO_SSID_AVAIL: Serial.println("📶 SSID introuvable"); break;
    case WL_SCAN_COMPLETED: Serial.println("🔍 Scan terminé"); break;
    case WL_CONNECT_FAILED: Serial.println("❌ Connexion impossible"); break;
    case WL_CONNECTION_LOST: Serial.println("🔌 Connexion perdue"); break;
    case WL_DISCONNECTED: Serial.println("❎ Déconnecté"); break;
    case WL_CONNECTED: Serial.println("✅ Connecté !"); break;
    default: Serial.println("❓ Statut inconnu");
  }
}
/*
void OLEDprincipal() {
  //display.clear//display();
  //display.setTextSize(1);
  //display.setCursor(0, 0);
  //affichage version
  //display.print("    ffp3cs ");
  //display.print(" v");
  //display.println(version);
  //display.println(" ");
  // nom réseau Wifi
  //display.println(WiFi.SSID());
  //IP réseau Wifi
  //display.println(WiFi.localIP());
  //display.print("Te:");
  //display.print(temperatureEau, 1);
  //display.cp437(true);
  //display.write(167);
  //display.print("C ");
  // //display temperature
  //display.print("Ta:");
  //display.print(temperatureAir, 1);
  //display.cp437(true);
  //display.write(167);
  //display.println("C");
  ////display eau aquarium
  //display.print("La:");
  //display.print(AWL);
  ////display eau réserve
  //display.print(" Lr:");
  //display.print(ultrasonTank.MeasureInCentimeters());
  ////display eau potager
  //display.print(" Lp:");
  //display.println(ultrasonPota.MeasureInCentimeters());
  ////display humidité
  //display.print("H:");
  //display.print(h, 1);
  //display.print("% ");
  // //display Luminosité
  //display.print("L:");
  //display.println(photocellReading);
  // //display heure et date
  //display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));

  //safe//display();
}*/

/*void OLEDvariables() {
  //display.clear//display();
  //display.setTextSize(1);
  //display.setCursor(0, 0);
  //affichage variables
  //display.print("  ffp3cs ");
  // état des relais
  //display.println(" variables");
  //display.println("       relais");
  //display.print("Paq:");
  //display.print(digitalRead(POMPEAQUA));
  //display.print(" Pta:");
  //display.print(digitalRead(POMPERESERV));
  //display.print(" R:");
  //display.print(digitalRead(RADIATEURS));
  //display.print(" L:");
  //display.println(digitalRead(LUMIERE));
  //Heures de nourrisage
  //display.print("Heures bouffe:");
  //display.print(config.inputMessageMatFeed);
  //display.print(" ");
  //display.print(config.inputMessageMidFeed);
  //display.print(" ");
  //display.println(config.inputMessageSoirFeed);
  //Temps de nourrisage
  //display.print("Tps bouffe P:");
  //display.print(config.StrTempsPetits);
  //display.print(" G:");
  //display.println(config.StrTempsGros);
  //Seuils
  //display.println("       Seuils");
  //display.print("Aq:");
  //display.print(config.inputMessageAqLim);
  //display.print(" Res:");
  //display.print(config.inputMessageTaLim);
  //display.print(" Cha:");
  //display.println(config.inputMessageChauffage);
  //display.print("Tps remp:");
  //display.print(config.tempsRemplissageSec);
  //display.print(" Flood:");
  //display.print(config.limFlood);
  //safe//display();
}*/

//affichage OLED
bool oled = 0;  // Variable pour l'état de l'affichage OLED
/*void affichageOLED() {
  if (oledPresent) {

  if (oled == 0) {
    oled = 1;
  } else {
    oled = 0;
  }
  if (oled == 1) {
    OLEDprincipal();
 } 

 if (oled == 0) {
   OLEDvariables();
 }

}
Serial.println("oled principal");

}*/

/**
 * Sauvegarde les flags de nourrisage et le dernier jour de nourrisage
 * dans la mémoire non volatile.
 */
void saveBouffeFlags() {
  preferences.begin("bouffe", false);
  preferences.putBool("bouffeMatinOk", bouffeMatinOk);
  preferences.putBool("bouffeMidiOk", bouffeMidiOk);
  preferences.putBool("bouffeSoirOk", bouffeSoirOk);
  preferences.putInt("lastJourBouf", lastJourBouf);
  preferences.putBool("pompeAquaLocked", pompeAquaLocked);
  preferences.end();
  Serial.println("bouffe dans preferences");

}

//notification mail
void notifMail() {
  if (WiFi.status() == WL_CONNECTED) {

    //notification mail pour les niveaux d'eau
    //mail si le niveau de l'aquarium est trop bas
    if ((AWL > config.inputMessageAqLim.toInt()) && config.enableEmailChecked == "checked" && !emailNiveauxSent) {
      emailMessage = String("Le niveau d'eau est bas. Le niveau d'eau est de ");
      emailMessage += String(AWL);
      sendEmailNotification();
      Serial.println(emailMessage);
      //WebSerial.println(emailMessage);
      emailNiveauxSent = true;
    }

    //mail si le niveau de la réserve est trop bas
    if ((TWL > config.inputMessageTaLim.toInt()) && config.enableEmailChecked == "checked" && !emailTankSent) {
      emailMessage = String("Le niveau d'eau de la réserve est bas. Il est de ");
      emailMessage += String(TWL);
      sendEmailNotification();
      Serial.println(emailMessage);
      emailTankSent = true;
    }
    //mail si le niveau de la réserve est remonté
    if ((TWL < config.inputMessageTaLim.toInt()) && config.enableEmailChecked == "checked" && emailTankSent) {
      emailMessage = String("Le niveau d'eau de la réserve est remonté. Il est de ");
      emailMessage += String(TWL);
      sendEmailNotification();
      Serial.println(emailMessage);
      //WebSerial.println(emailMessage);
      emailTankSent = false;
    }

    //empêche la pompe de réserve de s'activer et la vérouille si l'aquarium déborde
    if (AWL < (config.limFlood - 2) && AWL != 0) {
      delay(200);
      digitalWrite(POMPERESERV, LOW);
      Serial.println("Pompe réserve neutralisée");
      //WebSerial.println("Pompe réserve neutralisée");
      if (config.enableEmailChecked == "checked" && !emailFlooding) {
        char emailMsg[128];
        snprintf(emailMsg, sizeof(emailMsg), "L'aquarium déborde !!! La pompe de réserve est neutralisée et verrouillée. Son niveau est de %u", AWL);
        emailMessage = emailMsg;
        sendEmailNotification();
        Serial.println(emailMessage);
        //WebSerial.println(emailMessage);
        datatobdd();
        emailFlooding = true;
        tankPumpLocked = true;
      }
    }

    //retour à un niveau normal
    if (AWL > (config.limFlood + 6) && (emailFlooding || emailFloodRisk) && config.enableEmailChecked) {
      emailFlooding = false;
      emailFloodRisk = false;
      tankPumpLocked = false;
      emailMessage = String("Retour à un niveau correct de l'aquarium. Il est de ");
      emailMessage += String(AWL);
      sendEmailNotification();
      Serial.println(emailMessage);
      //WebSerial.println(emailMessage);
      datatobdd();
    }
  }
  Serial.println("notif mail");

}

unsigned int readUltrasonic(Ultrasonic& sensor, int maxDistance, int minDistance) {
    int reading = sensor.MeasureInCentimeters();
    delay(100);
    if (reading > maxDistance || reading < minDistance) {
        reading = sensor.MeasureInCentimeters();
        delay(200);
        if (reading > maxDistance || reading < minDistance) {
            return 0; // Valeur invalide
        }
    }
    return reading;
    Serial.println("vérif mesure ultrason ");

}

void mesuresUltrasons(){
  //appel température de l'eau
  Serial.println("mesures ultrasons début");
  numReadings = DEFAULT_NUM_READINGS;

  totalWL = 0;
  totalAWL = 0;
  totalTWL = 0;
  int badWL = 0;
  int badAWL = 0;
  int badTWL = 0;
  WL = 0;
  AWL = 0;
  TWL = 0;

  for (int i = 0; i < numReadings; i++) {
    // Lecture et filtrage pour les capteurs associés au premier servomoteur
    int currentReadingWL = readUltrasonic(ultrasonPota, 35, MIN_ULTRASON_DISTANCE);
    if (currentReadingWL == 0) {
      badWL = badWL + 1;
    }

    totalWL = totalWL + currentReadingWL;
  }

  numReadings -= badWL;
  if (numReadings == 0) {
    numReadings = 1;
  }
  WL = totalWL / numReadings;

  numReadings = DEFAULT_NUM_READINGS;

  for (int i = 0; i < numReadings; i++) {
    // Lecture et filtrage pour les capteurs associés au premier servomoteur
    int currentReadingAWL = readUltrasonic(ultrasonAqua, MAX_ULTRASON_DISTANCE, MIN_ULTRASON_DISTANCE);
    if (currentReadingAWL == 0) {
      badAWL = badAWL + 1;
    }
    totalAWL = totalAWL + currentReadingAWL;
  }

  numReadings -= badAWL;
  if (numReadings == 0) {
    numReadings = 1;
  }
  AWL = totalAWL / numReadings;

  numReadings = DEFAULT_NUM_READINGS;

  for (int i = 0; i < numReadings; i++) {
    // Lecture et filtrage pour les capteurs associés au premier servomoteur
    int currentReadingTWL = readUltrasonic(ultrasonTank, MAX_ULTRASON_DISTANCE, MIN_ULTRASON_DISTANCE);
    if (currentReadingTWL == 0) {
      badTWL = badTWL + 1;
    }
    totalTWL = totalTWL + currentReadingTWL;
  }

  numReadings -= badTWL;
  if (numReadings == 0) {
    numReadings = 1;
  }
  TWL = totalTWL / numReadings;

  numReadings = DEFAULT_NUM_READINGS;
}

void mesureTeau() {
  Serial.println("mesure de la température de l'eau");
  //lecture de la température de l'eau
  sensors.requestTemperatures();
  delay(200);
  //variable locale niveau eau potager
  temperatureEau = sensors.getTempCByIndex(0);
  delay(200);
  if (temperatureEau < 2) {
    temperatureEau = sensors.getTempCByIndex(0);
    delay(200);
  } else if (temperatureEau == 25.00) {
    temperatureEau = sensors.getTempCByIndex(0);
    delay(200);
  }
}

void mesureDHT() {
  Serial.println("mesure DHT début");
  //lecture du capteur DHT
  //variables locales temperature and humidity
  temperatureAir = dht.readTemperature();
  delay(200);  // fait paniquer le coeur
  h = dht.readHumidity();
  delay(200);
  if (isnan(h) || isnan(temperatureAir)) {
  }
  if (temperatureAir < 2) {
    temperatureAir = dht.readTemperature();
    delay(500);
  }
}

  void mesureLum() {
    Serial.println("mesure de la luminosité");
    //lecture du capteur de luminosité
    photocellReading = analogRead(LUMINOSITE);  // the analog reading from the analog resistor divider
    delay(100);
    if (photocellReading == 0) {
      photocellReading = 1;
    }
  }
void mesuresCapteurs() {

  Serial.println("mesures capteurs début");

  mesuresUltrasons();
  mesureTeau();
  mesureDHT();
  mesureLum();

  Serial.println("mesures capteurs fin");

}

void gestionRemplissageAquarium() {
//remplissage de l'aquarium cas si l'aquarium est trop bas et la réserve assez remplie
if (!tankPumpLocked) {
  if (((AWL > config.inputMessageAqLim.toInt()) && TWL < config.inputMessageTaLim.toInt()) || (digitalRead(POMPERESERV) == HIGH)) {
    delay(200);
    unsigned int SecuAWL = ultrasonAqua.MeasureInCentimeters();  // two measurements should keep an interval
    delay(500);
    if (((AWL > config.inputMessageAqLim.toInt()) && SecuAWL <= (AWL + 5) && SecuAWL >= (AWL - 5)) || (digitalRead(POMPERESERV) == HIGH)) {
      for (int i = 0; i < config.tempsRemplissageSec; i++) {
        if (oledPresent) {

        //display.clear//display();
        //display.setTextSize(2);
        //display.setCursor(0, 0);
        //display.println("Niveau Aqu");
        //display.println("Temps : ");
        int tpsRemplissageRestant = config.tempsRemplissageSec - i;
        //display.println(tpsRemplissageRestant);
        //safe//display();
        }
        Serial.println(config.inputMessageAqLim.toFloat());
        Serial.println(config.inputMessageAqLim.toInt());
        Serial.println("début remise à niveau aquarium");
        digitalWrite(POMPERESERV, HIGH);
        delay(1000);
      }
      datatobdd();
      digitalWrite(POMPERESERV, LOW);
      delay(100);
      if (oledPresent) {

      //display.clear//display();
      //display.setTextSize(2);
      //display.setCursor(0, 0);
      //display.println("Niveau");
      //display.println("Aquarium");
      //display.print("FIN");
      //safe//display();
      Serial.println("fin remise à niveau aquarium");
      Serial.println(digitalRead(POMPERESERV));
      delay(600);
      }
      datatobdd();
    }
  }
}}

void gestionBouffe() {
  rtc.getTime("%H:%M:%S %d/%m/%Y");
  heure = rtc.getTime("%H").toInt();
  int jourActuel = rtc.getTime("%d").toInt();


 // Réinitialisation des témoins de bouffe uniquement au changement de jour
if (jourActuel != lastJourBouf) {
  bouffeMatinOk = 0;
  bouffeMidiOk = 0;
  bouffeSoirOk = 0;
  lastJourBouf = jourActuel;
  saveBouffeFlags();
}

// --- Bouffe matin ---
if ((config.inputMessageMatFeed.toInt() == heure) && bouffeMatinOk == 0) {
  nourrissageGros();
  nourrissagePetits();
  datatobdd();
  bouffeGros = "0";
  bouffePetits = "0";
  datatobdd();
  bouffeMatinOk = 1;
  Serial.print("bouffe matin 2 :");
  Serial.println(bouffeMatinOk);
  emailMessage = String("nourrissage matin effectué ");
  sendEmailNotification();
  Serial.println(emailMessage);
} else if (config.inputMessageMatFeed.toInt() == heure) {
  Serial.println("nourrissage matin déjà effectué");
}

// --- Bouffe midi ---
if ((config.inputMessageMidFeed.toInt() == heure) && bouffeMidiOk == 0) {
  nourrissageGros();
  nourrissagePetits();
  datatobdd();
  bouffeGros = "0";
  bouffePetits = "0";
  datatobdd();
  bouffeMidiOk = 1;
  Serial.print("bouffe midi 2 :");
  Serial.println(bouffeMidiOk);
  emailMessage = String("nourrissage midi effectué ");
  sendEmailNotification();
  Serial.println(emailMessage);
} else if (config.inputMessageMidFeed.toInt() == heure) {
  Serial.println("nourrissage midi déjà effectué");
}

// --- Bouffe soir ---
if ((config.inputMessageSoirFeed.toInt() == heure) && bouffeSoirOk == 0) {
  nourrissageGros();
  nourrissagePetits();
  datatobdd();
  bouffeGros = "0";
  bouffePetits = "0";
  datatobdd();
  bouffeSoirOk = 1;
  Serial.print("bouffe soir 2 :");
  Serial.println(bouffeSoirOk);
  emailMessage = String("nourrissage soir effectué ");
  sendEmailNotification();
  Serial.println(emailMessage);
} else if (config.inputMessageSoirFeed.toInt() == heure) {
  Serial.println("nourrissage soir déjà effectué");
}

// Sauvegarde des témoins de bouffe
saveBouffeFlags();
}

void gestionChauffage() {
  //contrôle automatique du chauffage
  //chauffage off (il est allumé par défaut quand le relais est éteint)
  if (temperatureEau > config.inputMessageChauffage.toInt() && ChauffageOn == 1) {
    digitalWrite(RADIATEURS, LOW);
    ChauffageOn = 0;
    datatobdd();
  }
  //chauffage on
  if (temperatureEau < config.inputMessageChauffage.toInt() && ChauffageOn == 0) {
    digitalWrite(RADIATEURS, HIGH);
    ChauffageOn = 1;
    datatobdd();
  }}

void gestionAntiDebordement() {
  //empêche la pompe de réserve de s'activer si le niveau d'eau est trop haut
  if (AWL < config.limFlood && AWL != 0) {
    delay(200);
    digitalWrite(POMPERESERV, LOW);
    Serial.println("Pompe réserve neutralisée");
    //WebSerial.println("Pompe réserve neutralisée");
    if (config.enableEmailChecked == "checked" && !emailFloodRisk) {
      emailMessage = String("L'aquarium risque de déborber. La pompe de réserve est neutralisée. Son niveau est de ");
      emailMessage += String(AWL);
      sendEmailNotification();
      //WebSerial.println(emailMessage);
      datatobdd();
      emailFloodRisk = true;
    }
  }}
void secuPompeAqua() {
     // Sécurité critique : arrêt pompe aquarium si niveau > 50
     if (AWL > 50 && !pompeAquaLocked) {
      digitalWrite(POMPEAQUA, LOW); // Arrêt pompe aquarium
      pompeAquaLocked = true;
      emailMessage = String("SECURITE : Pompe aquarium arrêtée car niveau d'eau > 50 (") + String(AWL) + ")";
      sendEmailNotification();
      Serial.println(emailMessage);
      datatobdd();
    }
    if (AWL < 50 && pompeAquaLocked) {
      digitalWrite(POMPEAQUA, HIGH); // démarrage pompe aquarium
      emailMessage = String("SECURITE : Pompe aquarium remise en route < 50 (") + String(AWL) + ")";
      sendEmailNotification();
      pompeAquaLocked = false;
      datatobdd();
    }
  }


  /*void scanI2C() {
  // Scan I2C pour détecter les périphériques
  byte error, address;
  int nDevices = 0;

  Serial.println("Scan I2C en cours...");
  for (address = 1; address < 127; address++) {
    Wire.beginTransmission(address);
    error = Wire.endTransmission();

    if (error == 0) {
      Serial.print("I2C device trouvé à l'adresse 0x");
      if (address < 16) Serial.print("0");
      Serial.print(address, HEX);
      Serial.println(" !");
      nDevices++;
    } else if (error == 4) {
      Serial.print("Erreur inconnue à l'adresse 0x");
      if (address < 16) Serial.print("0");
      Serial.println(address, HEX);
    }
  }
  if (nDevices == 0)
    Serial.println("Aucun périphérique I2C trouvé\n");
  else
    Serial.println("Scan terminé\n");

  delay(5000); // Attendre 5 secondes avant de recommencer
}*/

void automatismes() {
  secuPompeAqua();
  gestionRemplissageAquarium();
  gestionBouffe();
  gestionChauffage();
  gestionAntiDebordement();
}
void bouffes() {
  //bouffe manuelle
  //petits
  if (bouffePetits == "1") {
    datatobdd();
    nourrissagePetits();
    bouffePetits = "0";
    datatobdd();
  }
  //gros
  if (bouffeGros == "1") {
    datatobdd();
    nourrissageGros();
    bouffeGros = "0";
    datatobdd();
  }
  Serial.println("automatisme");
}

void HeureSansWifi() {
  preferences.begin("rtc", true);
  time_t savedEpoch = preferences.getULong("epoch", 0);
  preferences.end();

  if (savedEpoch == 0) savedEpoch = 1704067200; // 1/1/2024 00:00:00 par défaut
  rtc.setTime(savedEpoch);
  now = rtc.getEpoch();  // Récupère le timestamp Unix actuel

  // Affiche l'heure sauvegardée AVANT correction
  Serial.print("heure sauvegardée : ");
  Serial.print(savedEpoch);
  Serial.print(" => ");
  Serial.println(ctime(&savedEpoch));
  Serial.print("heure recalée : ");
  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  Serial.println(rtc.getEpoch());
  Serial.print("now : ");
  Serial.println(now);


  if (oledPresent) {
    //display.clear//display();
    //display.setTextSize(2);
    //display.setCursor(0, 0);
    //display.println("Load H flash");
    //display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
    //safe//display();
    delay(500);
  }
}

void EnregistrementHeureFlash() {
  //mettre wifi
  
  now = rtc.getEpoch();  // Récupère le timestamp Unix actuel


  if (now == 0) {
    Serial.println("Erreur : impossible de récupérer l'heure actuelle");
    return;
  }

  preferences.begin("rtc", false);
  preferences.putULong("epoch", now);
  if (!preferences.putULong("epoch", now)) {
    Serial.println("Erreur : impossible d'enregistrer l'heure actuelle");
    preferences.end();
    return;
  }
  Serial.println("heure (epoch) enregistrée : ");
  Serial.println(preferences.getULong("epoch", 0));
  Serial.println("heure (now) actuelle : ");
  Serial.println(now);
  Serial.print("heure lisible : ");
  char* ctimeStr = ctime(&now);
  if (ctimeStr == nullptr) {
    Serial.println("Erreur : impossible de convertir l'heure en chaine de caractères");
    return;
  }
  Serial.println(ctimeStr); // Affiche "Wed Jun  5 14:32:10 2024" par exemple
  preferences.end();

}

// Fonction robuste de reconnexion Wi-Fi
bool wifiReconnect(uint32_t timeout_ms = 5000) {
  Serial.println("Tentative de reconnexion Wi-Fi...");

  // Relance la tentative de connexion avec les infos mémorisées
  WiFi.reconnect();

  // Attend jusqu'à 'timeout_ms' pour vérifier la connexion
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) {
    delay(100);
    Serial.print(".");
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWi-Fi reconnecté !");
    return true;
  } else {
    Serial.println("\nÉchec de reconnexion Wi-Fi.");
    connectToWiFi(); // Essaye de se reconnecter avec la fonction de connexion initiale
    if (WiFi.status() == WL_CONNECTED) {
      Serial.println("Reconnexion réussie après tentative initiale.");
      return true;
    }
    return false;
  }
}

void goToLightSleep() {
  datatobdd();
  Serial.flush();
  last_sleep_time_us = esp_timer_get_time();
  EnregistrementHeureFlash();
  saveConfig();
  Serial.print("Epoch sauvegardé avant deep sleep : ");
  preferences.begin("rtc", true);
  Serial.println(preferences.getULong("epoch", 0));
  preferences.end();
  esp_sleep_enable_timer_wakeup(TIME_TO_STOP * uS_TO_S_FACTOR);
  //esp_sleep_enable_touchpad_wakeup();
  if (oledPresent) {
    //display.println(" Going to sleep now");
    Serial.println("Going to sleep now");
    //safe//display();
    delay(1000);
  }
WiFi.disconnect();  // Déconnecte le Wi-Fi avant de dormir
digitalWrite(POMPEAQUA, LOW);
  Serial.println("Déconnexion Wi-Fi avant le sommeil"); 

  esp_light_sleep_start();
  digitalWrite(POMPEAQUA, HIGH);
    wifiReconnect();
      // Démarrage des capteurs
  dht.begin();         Serial.println("dht begin");
  sensors.begin();     sensors.setResolution(10); Serial.println("sensor begin");

}



void sommeilmaree() {

    if (AWL>AWLmax){
        AWLmax=AWL;
    }
        diffMaree=AWLmax-AWL;
    
     if (diffMaree>1){
      if (oledPresent) {

        //display.clear//display();
          //display.setTextSize(2);
          //display.setCursor(0, 0);
          //display.print("descend : ");
          //display.println(diffMaree);
          delay(500);
      }
          diffMaree = 0;
          AWLmax=0;
          if (WakeUp == 0) {
            goToLightSleep();
          }
    } 
    Serial.println("sommeil marée");
  
}

// --- FFP3 fonctions et logique (sans OLED) ---
// (toutes les fonctions utilitaires, gestion capteurs, actionneurs, WiFi, OTA, etc.)
// ...copie ici toutes les fonctions de ffp3.cpp sauf celles qui touchent à l'OLED...
// //safe//display, OLEDprincipal, OLEDvariables, affichageOLED, etc. sont supprimées.
// Les fonctions setup() et loop() de ffp3 seront fusionnées plus bas.

// Exemples de fonctions à inclure (liste non exhaustive) :
// - httpRequest, saveConfig, loadConfig, sendEmailNotification
// - print_wakeup_reason, httpGETRequest, datatobdd, nourrissagePoissons, nourrissageGros, nourrissagePetits
// - dataJSONtoesp, bouffeManuelle, variablestoesp, connectToWiFi, printWiFiStatusReason
// - saveBouffeFlags, notifMail, readUltrasonic, mesuresUltrasons, mesureTeau, mesureDHT, mesureLum, mesuresCapteurs
// - gestionRemplissageAquarium, gestionBouffe, gestionChauffage, gestionAntiDebordement, secuPompeAqua
// - automatismes, bouffes, HeureSansWifi, EnregistrementHeureFlash, wifiReconnect, goToLightSleep, sommeilmaree



/*******************************************************************************
 * LVGL Widgets
 * This is a widgets demo for LVGL - Light and Versatile Graphics Library
 * import from: https://github.com/lvgl/lv_demos.git
 *
 * Dependent libraries:
 * LVGL: https://github.com/lvgl/lvgl.git
 *
 * LVGL Configuration file:
 * Copy your_arduino_path/libraries/lvgl/lv_conf_template.h
 * to your_arduino_path/libraries/lv_conf.h
 *
 * In lv_conf.h around line 15, enable config file:
 * #if 1 // Set it to "1" to enable content
 *
 * Then find and set:
 * #define LV_COLOR_DEPTH     16
 * #define LV_TICK_CUSTOM     1
 *
 * For SPI/parallel 8 //display set color swap can be faster, parallel 16/RGB screen don't swap!
 * #define LV_COLOR_16_SWAP   1 // for SPI and parallel 8
 * #define LV_COLOR_16_SWAP   0 // for parallel 16 and RGB
 *
 * Enable LVGL Demo Widgets
 * #define LV_USE_DEMO_WIDGETS 1
 ******************************************************************************/
#include "lv_demo_widgets.h"
// #define DIRECT_MODE // Uncomment to enable full frame buffer

/*******************************************************************************
 * Start of Arduino_GFX setting
 *
 * Arduino_GFX try to find the settings depends on selected board in Arduino IDE
 * Or you can define the //display dev kit not in the board list
 * Defalult pin list for non //display dev kit:
 * Arduino Nano, Micro and more: CS:  9, DC:  8, RST:  7, BL:  6, SCK: 13, MOSI: 11, MISO: 12
 * ESP32 various dev board     : CS:  5, DC: 27, RST: 33, BL: 22, SCK: 18, MOSI: 23, MISO: nil
 * ESP32-C3 various dev board  : CS:  7, DC:  2, RST:  1, BL:  3, SCK:  4, MOSI:  6, MISO: nil
 * ESP32-S2 various dev board  : CS: 34, DC: 38, RST: 33, BL: 21, SCK: 36, MOSI: 35, MISO: nil
 * ESP32-S3 various dev board  : CS: 40, DC: 41, RST: 42, BL: 48, SCK: 36, MOSI: 35, MISO: nil
 * ESP8266 various dev board   : CS: 15, DC:  4, RST:  2, BL:  5, SCK: 14, MOSI: 13, MISO: 12
 * Raspberry Pi Pico dev board : CS: 17, DC: 27, RST: 26, BL: 28, SCK: 18, MOSI: 19, MISO: 16
 * RTL8720 BW16 old patch core : CS: 18, DC: 17, RST:  2, BL: 23, SCK: 19, MOSI: 21, MISO: 20
 * RTL8720_BW16 Official core  : CS:  9, DC:  8, RST:  6, BL:  3, SCK: 10, MOSI: 12, MISO: 11
 * RTL8722 dev board           : CS: 18, DC: 17, RST: 22, BL: 23, SCK: 13, MOSI: 11, MISO: 12
 * RTL8722_mini dev board      : CS: 12, DC: 14, RST: 15, BL: 13, SCK: 11, MOSI:  9, MISO: 10
 * Seeeduino XIAO dev board    : CS:  3, DC:  2, RST:  1, BL:  0, SCK:  8, MOSI: 10, MISO:  9
 * Teensy 4.1 dev board        : CS: 39, DC: 41, RST: 40, BL: 22, SCK: 13, MOSI: 11, MISO: 12
 ******************************************************************************/
#include <Arduino_GFX_Library.h>

//#define GFX_BL DF_GFX_BL // default backlight pin, you may replace DF_GFX_BL to actual backlight pin

/* More dev device declaration: https://github.com/moononournation/Arduino_GFX/wiki/Dev-Device-Declaration */
#if defined(DISPLAY_DEV_KIT)
Arduino_GFX *gfx = create_default_Arduino_GFX();
#else /* !defined(DISPLAY_DEV_KIT) */


#define GFX_BL 1
Arduino_DataBus *bus = new Arduino_ESP32QSPI(
    45 /* cs */, 47 /* sck */, 21 /* d0 */, 48 /* d1 */, 40 /* d2 */, 39 /* d3 */);
Arduino_GFX *g = new Arduino_NV3041A(bus, GFX_NOT_DEFINED /* RST */, 0 /* rotation */, true /* IPS */);
Arduino_GFX *gfx = new Arduino_Canvas(480 /* width */, 272 /* height */, g);
#define CANVAS


#endif /* !defined(DISPLAY_DEV_KIT) */
/*******************************************************************************
 * End of Arduino_GFX setting
 ******************************************************************************/

/*******************************************************************************
 * Please config the touch panel in touch.h
 ******************************************************************************/
#include "touch.h"

/* Change to your screen resolution */
static uint32_t screenWidth;
static uint32_t screenHeight;
static uint32_t bufSize;
static lv_disp_draw_buf_t draw_buf;
static lv_color_t *disp_draw_buf;
static lv_disp_drv_t disp_drv;

/* //display flushing */
void my_disp_flush(lv_disp_drv_t *disp, const lv_area_t *area, lv_color_t *color_p)
{
#ifndef DIRECT_MODE
  uint32_t w = (area->x2 - area->x1 + 1);
  uint32_t h = (area->y2 - area->y1 + 1);

#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#else
  gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t *)&color_p->full, w, h);
#endif
#endif // #ifndef DIRECT_MODE

  lv_disp_flush_ready(disp);
}

void my_touchpad_read(lv_indev_drv_t *indev_driver, lv_indev_data_t *data)
{
  if (touch_has_signal())
  {
    if (touch_touched())
    {
      data->state = LV_INDEV_STATE_PR;

      /*Set the coordinates*/
      data->point.x = touch_last_x;
      data->point.y = touch_last_y;
    }
    else if (touch_released())
    {
      data->state = LV_INDEV_STATE_REL;
    }
  }
  else
  {
    data->state = LV_INDEV_STATE_REL;
  }
}

void setup()
{
  // --- Initialisation NVS (avant tout Preferences ou WiFi) ---
  esp_err_t nvs_err = nvs_flash_init();
  if (nvs_err != ESP_OK && nvs_err != ESP_ERR_NVS_NO_FREE_PAGES && nvs_err != ESP_ERR_NVS_NEW_VERSION_FOUND) {
    Serial.printf("Erreur NVS init: %d\n", nvs_err);
  } else if (nvs_err == ESP_ERR_NVS_NO_FREE_PAGES || nvs_err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    nvs_flash_erase();
    nvs_err = nvs_flash_init();
    if (nvs_err != ESP_OK) Serial.printf("Erreur NVS re-init: %d\n", nvs_err);
  }

  // --- Initialisation LVGL et écran tactile (déjà présent) ---
  Serial.begin(115200);
  Serial.println("Arduino_GFX LVGL Widgets example");
#ifdef GFX_EXTRA_PRE_INIT
  GFX_EXTRA_PRE_INIT();
#endif
  if (!gfx->begin()) {
    Serial.println("gfx->begin() failed!");
  }
  gfx->fillScreen(BLACK);
#ifdef GFX_BL
  pinMode(GFX_BL, OUTPUT);
  digitalWrite(GFX_BL, HIGH);
#endif
  touch_init(gfx->width(), gfx->height(), gfx->getRotation());
  lv_init();
  screenWidth = gfx->width();
  screenHeight = gfx->height();
#ifdef DIRECT_MODE
  bufSize = screenWidth * screenHeight;
#else
  bufSize = screenWidth * 40;
#endif
#ifdef ESP32
  disp_draw_buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * bufSize, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
  if (!disp_draw_buf) {
    disp_draw_buf = (lv_color_t *)heap_caps_malloc(sizeof(lv_color_t) * bufSize, MALLOC_CAP_8BIT);
  }
#else
  disp_draw_buf = (lv_color_t *)malloc(sizeof(lv_color_t) * bufSize);
#endif
  if (!disp_draw_buf) {
    Serial.println("LVGL disp_draw_buf allocate failed!");
  } else {
    lv_disp_draw_buf_init(&draw_buf, disp_draw_buf, NULL, bufSize);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = screenWidth;
    disp_drv.ver_res = screenHeight;
    disp_drv.flush_cb = my_disp_flush;
    disp_drv.draw_buf = &draw_buf;
#ifdef DIRECT_MODE
    disp_drv.direct_mode = true;
#endif
    lv_disp_drv_register(&disp_drv);
    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = my_touchpad_read;
    lv_indev_drv_register(&indev_drv);
    // --- Initialisation FFP3 (sans OLED) ---
    bool littlefsOk = LittleFS.begin(true);
    if (!littlefsOk) {
      Serial.println("Erreur LittleFS");
    } else {
      loadConfig();
      Serial.println("LittleFS OK");
    }
    HeureSansWifi();
    WiFi.onEvent([](WiFiEvent_t event) {
      Serial.printf("[WiFi-event] event: %d\n", event);
    });
    WiFi.mode(WIFI_MODE_STA);
    if (!connectToWiFi()) {
      Serial.println("Erreur : Connexion Wi-Fi échouée.");
    }
    pinMode(EAUPOTA, INPUT);
    pinMode(LUMINOSITE, INPUT);
    pinMode(POMPEAQUA, OUTPUT);  digitalWrite(POMPEAQUA, HIGH);
    pinMode(POMPERESERV, OUTPUT); digitalWrite(POMPERESERV, LOW);
    pinMode(RADIATEURS, OUTPUT);  digitalWrite(RADIATEURS, HIGH);
    pinMode(LUMIERE, OUTPUT);     digitalWrite(LUMIERE, LOW);
    pinMode(BOUFFEGROS, OUTPUT);
    pinMode(BOUFFEPETITS, OUTPUT);
    // Log pins Servo avant attach
    Serial.printf("Attaching myservobig on pin %d\n", BOUFFEGROS);
    if (BOUFFEGROS == 255 || BOUFFEGROS == -1) Serial.println("ERREUR: Pin Servo GROS invalide!");
    myservobig.attach(BOUFFEGROS);      myservobig.write(92);
    delay(50);
    Serial.printf("Attaching myservosmall on pin %d\n", BOUFFEPETITS);
    if (BOUFFEPETITS == 255 || BOUFFEPETITS == -1) Serial.println("ERREUR: Pin Servo PETITS invalide!");
    myservosmall.attach(BOUFFEPETITS);  myservosmall.write(88);
    delay(50);
    dht.begin();
    sensors.begin(); sensors.setResolution(10);
    variablestoesp();
    preferences.begin("bouffe", true);
    bouffeMatinOk = preferences.getBool("bouffeMatinOk", 0);
    bouffeMidiOk = preferences.getBool("bouffeMidiOk", 0);
    bouffeSoirOk = preferences.getBool("bouffeSoirOk", 0);
    lastJourBouf = preferences.getInt("lastJourBouf", -1);
    pompeAquaLocked = preferences.getBool("pompeAquaLocked", false);
    preferences.end();
    mesuresCapteurs();
    datatobdd();
    lv_demo_widgets(); // Affichage de la démo LVGL par défaut
    Serial.println("Setup done");
  }
}

void loop()
{
  // --- FFP3 logique principale (sans OLED) ---
  lv_timer_handler(); /* let the GUI do its work */

#ifdef DIRECT_MODE
#if (LV_COLOR_16_SWAP != 0)
  gfx->draw16bitBeRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
#else
  gfx->draw16bitRGBBitmap(0, 0, (uint16_t *)disp_draw_buf, screenWidth, screenHeight);
#endif
#endif // #ifdef DIRECT_MODE

#ifdef CANVAS
  gfx->flush();
#endif

  // --- Ajout de la logique FFP3 (sans affichage OLED) ---
  if (!pompeAquaLocked) {
    digitalWrite(POMPEAQUA, HIGH); // ou LOW selon ta logique
  }

  static unsigned long lastNtpSync = 0;
  const unsigned long ntpSyncInterval = 3600000UL; // 1h
  if (WiFi.status() == WL_CONNECTED && millis() - lastNtpSync > ntpSyncInterval) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
          rtc.setTimeStruct(timeinfo);
          time_t ntpEpoch = mktime(&timeinfo);
          time_t rtcEpoch = rtc.getEpoch();
          Serial.printf("Dérive détectée (secondes) : %ld\n", ntpEpoch - rtcEpoch );
          EnregistrementHeureFlash();
      }
      lastNtpSync = millis();
  }

  printWiFiStatusReason();

  if (WiFi.status() != WL_CONNECTED) {
    if (!connectToWiFi()) {
        Serial.println("Erreur : Connexion Wi-Fi échouée.");
        wifiWasConnected = false;
    }
  } else if (!wifiWasConnected) {
      configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
          rtc.setTimeStruct(timeinfo);
          Serial.println("Heure synchronisée NTP après reconnexion WiFi");
      }
      wifiWasConnected = true;
  }

  variablestoesp();
  Serial.print("heure restaurée (après correction) : ");
  Serial.print(" => ");
  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  bouffePetits = "0";
  bouffeGros = "0";
  tempsRemplissage = config.tempsRemplissageSec * 1000;
  mesuresCapteurs();
  automatismes();
  notifMail();
  digitalWrite(POMPERESERV, LOW);
  bouffes();
  if (resetMode == 1) {
    resetMode = 0;
    datatobdd();
    ESP.restart();
  }
  }
  sommeilmaree();
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
    if (WiFi.status() != WL_CONNECTED) {
      Serial.println("Erreur : Connexion Wi-Fi échouée.");
    }
    if (WakeUp == 0) {
      goToLightSleep();
    }
  }
  delay(5);
}

