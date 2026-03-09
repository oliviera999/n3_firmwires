/* MeteoStationPrototype (msp1)
 * Station meteo + tracker solaire — salle aeree n3
 * Credentials externalises dans credentials.h
 * OTA HTTP distant via n3_common
 */

#define FIRMWARE_VERSION "2.11"

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <Arduino.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <HTTPClient.h>
#include <ESP_Mail_Client.h>
#include <Arduino_JSON.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <esp_sleep.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <ESP32Time.h>
#include <Preferences.h>
#include "credentials.h"
#include "n3_ota.h"
#include "n3_wifi.h"


#define RELAIS 13  // Définition du pin du relais pour contrôler l'alimentation

const unsigned int oneWireBus = 2;    // GPIO sur lequel la sonde DS18B20 est connectée
OneWire oneWire(oneWireBus);          // Instance pour la communication avec la sonde DS18B20
DallasTemperature sensors(&oneWire);  // Instance Dallas pour lire le capteur de température
float temperatureSol;                 // Température du sol (sonde DS18B20)

#define LUMINOSITEc 35  // Pins pour les capteurs de luminosité
#define LUMINOSITEd 39
#define LUMINOSITEa 33
#define LUMINOSITEb 34

const int numReadings = 10;                                                                      // Nombre de lectures pour filtrer le bruit
int readings1[numReadings];                                                                      // Tableau pour stocker les lectures du capteur 1
int readings2[numReadings];                                                                      // Tableau pour stocker les lectures du capteur 2
int readings3[numReadings];                                                                      // Tableau pour stocker les lectures du capteur 3
int readings4[numReadings];                                                                      // Tableau pour stocker les lectures du capteur 4
int readIndex = 0;                                                                               // Index du tableau pour tous les capteurs
int total1 = 0, total2 = 0, total3 = 0, total4 = 0;                                              // Totaux pour chaque capteur
int average1 = 0, average2 = 0, average3 = 0, average4 = 0;                                      // Moyennes
int photocellReadingA = 0, photocellReadingB = 0, photocellReadingC = 0, photocellReadingD = 0;  // Valeurs max de luminosité
int photocellReadingMoy = 0;

#define SERVOGD 25  // Pin pour le servomoteur gauche-droite (GD)
#define SERVOHB 14
Servo servogd;                                                       // Instance pour le servomoteur gauche-droite (GD)
Servo servohb;                                                       // Instance pour le servomoteur haut-bas (HB)
int posLumMax1 = 0, posLumMax2 = 0, posLumMax3 = 0, posLumMax4 = 0;  // Positions max
int AngleServoGD;
int AngleServoHB;
const int minAngleServoGD = 1;  // Plage de mouvement pour le premier servomoteur
const int maxAngleServoGD = 179;
const int minAngleServoHB = 40;  // Plage de mouvement pour le second servomoteur
const int maxAngleServoHB = 145;

#define DHTPININT 26                // Pin connectée au DHT intérieur
#define DHTTYPEINT DHT11            // Type de capteur DHT intérieur
DHT dhtint(DHTPININT, DHTTYPEINT);  // Instance DHT intérieur
//variables T et H pour les DHT
float tempAirInt;
float humidAirInt;

#define DHTPINEXT 15                // Pin connectée au DHT extérieur
#define DHTTYPEEXT DHT11            // Type de capteur DHT extérieur
DHT dhtext(DHTPINEXT, DHTTYPEEXT);  // Instance DHT extérieur
float tempAirExt;
float humidAirExt;

#define uS_TO_S_FACTOR 1000000ULL  // Facteur de conversion de microsecondes en secondes
#define TIME_TO_SLEEP FreqWakeUp   /* Durée du sommeil en secondes avant réveil */
bool WakeUp = 0;                   //variable de mise en endormissement ou pas
int FreqWakeUp = 3;               // Durée du sommeil en secondes (valeur par défaut 3 s)
//bool ArrosageManu = 0;

#define pontdiv 36  // Pin pour la lecture du diviseur de tension
int PontDiv;        //valeur
int avgPontDiv;
float batt;
float measuredVoltage;
float batteryVoltage;
int SeuilPontDiv = 1700;  // Seuil pour considérer la batterie faible
#define R1 2200           // valeur de la résistances R1 en ohm du pont diviseur
#define R2 2180           // valeur de la résistances R2 en ohm du pont diviseur
// Constantes pour la conversion de la mesure analogique en tension réelle
const float ADC_MAX_VALUE = 4095.0;  // Plage de l'ADC de l'ESP32 (12 bits)
const float V_REF = 3.33;            // Tension de référence de l'ESP32
const float calibration = 0.06;      //facteur de calibration
#define NUM_SAMPLES 10               // Nombre d'échantillons à utiliser pour le filtrage
// Tableau pour stocker les échantillons
int samples[NUM_SAMPLES];
int sampleIndex = 0;
int sampleTotal = 0;

int SeuilSec = 5000;  // Seuil pour considérer que le sol est sec

bool resetMode = 0;   // Mode de réinitialisation
bool etatRelais = 0;  // État du relais

int Oled = 0;

//définitions pour les capteurs analogiques de pluie et humidité
#define HumiditeSol 32                  // Pin pour le capteur d'humidité du sol
#define PLUIE 27                        // Pin pour le capteur de détection de pluie
int HumidSol;                           // Variable pour stocker l'humidité du sol
int Pluie;                              // Variable pour détecter la présence de pluie
unsigned long previousMillisDatas = 0;  // Variable pour suivre le temps écoulé depuis la dernière mesure
const long intervalDatas = 120000;      // Intervalle entre deux mesures

bool emailHumidSent = 0;  // Indicateur pour savoir si un email d'alerte a été envoyé pour l'humidité
//bool emailPontDivSent = 0; // Indicateur pour savoir si un email d'alerte a été envoyé pour la batterie

RTC_DATA_ATTR int bootCount = 0;  // Compteur du nombre de démarrages

#define emailSubject "Information MSP1"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";

String emailMessage;

/* Session SMTP globale pour l'envoi des emails (ESP Mail Client) */
SMTPSession smtp;

#ifdef TEST_MODE
const char* serverNamePostData = "http://iot.olution.info/msp1-test/msp1datas/post-msp1-data.php";
const char* serverNameOutput = "http://iot.olution.info/msp1-test/msp1control/msp1-outputs-action.php?action=outputs_state&board=2";
#else
const char* serverNamePostData = "http://iot.olution.info/msp1/msp1datas/post-msp1-data.php";
const char* serverNameOutput = "http://iot.olution.info/msp1/msp1control/msp1-outputs-action.php?action=outputs_state&board=2";
#endif

// Code de réponse HTTP (requêtes GET/POST)
unsigned int httpResponseCode;

String version = FIRMWARE_VERSION;

String apiKeyValue = API_KEY;
String sensorName = "msp1";                // Nom du capteur
String sensorLocation = "T06";             // Location du capteur

#define SCREEN_WIDTH 128                                           // Largeur de l'affichage OLED
#define SCREEN_HEIGHT 64                                           // Hauteur de l'affichage OLED
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // Instance pour gérer l'affichage OLED

const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;

String Wifiactif;  // Indicateur pour savoir quel réseau WiFi est actif

AsyncWebServer server(80);  // Création d'un serveur web asynchrone qui écoute sur le port 80

WiFiUDP wifiUdp;  // UDP pour NTP
// Instance NTP client pour obtenir l'heure via NTP
//NTPClient timeClient(wifiUdp, "pool.ntp.org", 3600, 60000); // Ajustement pour votre location

String outputsState;  // Variable pour gérer l'état des sorties à distance


//variables en lien avec le temps rtc et ntp
const char* ntpServer = "pool.ntp.org";  // Adresse du serveur NTP
const long gmtOffset_sec = 3600;         // Décalage GMT en secondes
const int daylightOffset_sec = 3600;     // Décalage pour l'heure d'été en secondes
ESP32Time rtc;                           //initialisation du contrôle RTC via la bibliothèque ESP32time
Preferences preferences;                 // Stockage de l'heure dans la mémoire flash (NVS)
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;

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

/*
// Fonction pour synchroniser l'heure avec NTP
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) { // Obtention du temps local
    return;
  }

  heure = timeinfo.tm_hour; // Mappage des informations de temps
  minute = timeinfo.tm_min;
  seconde = timeinfo.tm_sec;
  jour = timeinfo.tm_mday;
  mois = timeinfo.tm_mon + 1;
  annee = timeinfo.tm_year + 1900;
}*/

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
/*
// Callback pour l'envoi des emails
void sendCallback(SendStatus msg) {
  // Affiche le statut actuel
}*/

//fonction permettant l'enregistrement des variables de temps dans la mémoire flash
void EnregistrementHeureFlash() {
  rtc.getTime("%H:%M:%S %d/%m/%Y");
  seconde = rtc.getTime("%S").toInt();
  minute = rtc.getTime("%M").toInt();
  heure = rtc.getTime("%H").toInt();
  jour = rtc.getTime("%d").toInt();
  mois = rtc.getTime("%m").toInt();
  annee = rtc.getTime("%Y").toInt();
  preferences.begin("rtc", false);
  preferences.putInt("heure", heure);
  preferences.putInt("minute", minute);
  preferences.putInt("seconde", seconde);
  preferences.putInt("jour", jour);
  preferences.putInt("mois", mois);
  preferences.putInt("annee", annee);
  preferences.end();
}

// Fonction pour obtenir la raison du réveil de l'ESP32
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Reveil : signal externe (RTC_IO)"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Reveil : signal externe (RTC_CNTL)"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Reveil : timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Reveil : touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Reveil : programme ULP"); break;
    default:
      Serial.printf("Reveil non cause par deep sleep : %d\n", wakeup_reason);
      preferences.begin("rtc", true);           // Ouverture session NVS (lecture seule)
      heure = preferences.getInt("heure", 12);   // Récupération heure sauvegardée (défaut 12h)
      minute = preferences.getInt("minute", 0);
      seconde = preferences.getInt("seconde", 0);
      jour = preferences.getInt("jour", 1);
      mois = preferences.getInt("mois", 1);
      annee = preferences.getInt("annee", 2023);
      preferences.end();                                       // Fermeture de la session NVS
      rtc.setTime(seconde, minute, heure, jour, mois, annee);  // définition RTC de l'heure sans synchro NTP
      break;
  }
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

  /* Variante : connexion sans login, puis authentification séparée
     if (!smtp.connect(&config, false)) return;
     if (!smtp.loginWithPassword(AUTHOR_EMAIL, AUTHOR_PASSWORD)) return;
  */

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

void Light_val() {
  photocellReadingMoy = ((analogRead(LUMINOSITEa) + analogRead(LUMINOSITEb) + analogRead(LUMINOSITEc) + analogRead(LUMINOSITEd)) / 4);
  if (photocellReadingMoy > 50) {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("Scan");
    display.print("LumMoy = ");
    display.println(photocellReadingMoy);

    display.display();
    delay(750);
    // Initialisation des tableaux de lectures
    for (int i = 0; i < numReadings; i++) {
      readings1[i] = 0;
      readings2[i] = 0;
      readings3[i] = 0;
      readings4[i] = 0;
    }

    // Initialisation des variables
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;


    // Balayage des positions et mesure de la luminosité pour les quatre capteurs

    // Balayage du premier servomoteur
    for (int pos = minAngleServoGD; pos <= maxAngleServoGD; pos++) {
      servogd.write(pos);
      delay(30);  // Attendre que le servomoteur se positionne

      // Lecture et filtrage pour les capteurs associés au premier servomoteur
      int currentReading1 = analogRead(LUMINOSITEa);
      total1 = total1 - readings1[readIndex];
      readings1[readIndex] = currentReading1;
      total1 = total1 + readings1[readIndex];
      average1 = total1 / numReadings;

      int currentReading2 = analogRead(LUMINOSITEb);
      total2 = total2 - readings2[readIndex];
      readings2[readIndex] = currentReading2;
      total2 = total2 + readings2[readIndex];
      average2 = total2 / numReadings;

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("Moy 1 ");
      display.println(average1);
      display.print("Moy 2 ");
      display.println(average2);
      display.display();
      delay(50);

      // Enregistrement de la valeur maximale pour les capteurs 1 et 2
      if (average1 > photocellReadingA) {
        photocellReadingA = average1;
        posLumMax1 = pos;
        Serial.print("position max a : ");
        Serial.println(posLumMax1);
      }
      if (average2 > photocellReadingB) {
        photocellReadingB = average2;
        posLumMax2 = pos;
        Serial.print("position max b : ");
        Serial.println(posLumMax2);
      }
    }

    AngleServoGD = (posLumMax1 + posLumMax2) / 2;  // Calcul des positions finales pour servomoteur gauche droite
    servogd.write(AngleServoGD);                   // Positionnement final du servomoteur gauche droite
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(posLumMax1);
    display.print(" ");
    display.print(posLumMax2);
    display.print("AngleM = ");
    display.println(AngleServoGD);
    display.display();
    delay(750);

    // Balayage du second servomoteur
    for (int pos = minAngleServoHB; pos <= maxAngleServoHB; pos++) {
      servohb.write(pos);
      delay(30);  // Attendre que le servomoteur se positionne

      // Lecture et filtrage pour les capteurs associés au second servomoteur
      int currentReading3 = analogRead(LUMINOSITEc);
      total3 = total3 - readings3[readIndex];
      readings3[readIndex] = currentReading3;
      total3 = total3 + readings3[readIndex];
      average3 = total3 / numReadings;

      int currentReading4 = analogRead(LUMINOSITEd);
      total4 = total4 - readings4[readIndex];
      readings4[readIndex] = currentReading4;
      total4 = total4 + readings4[readIndex];
      average4 = total4 / numReadings;

      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.print("Moy 3 ");
      display.println(average3);
      display.print("Moy 4 ");
      display.println(average4);
      display.display();
      delay(50);

      // Enregistrement de la valeur maximale pour les capteurs 3 et 4
      if (average3 > photocellReadingC) {
        photocellReadingC = average3;
        posLumMax3 = pos;
        Serial.print("position max c : ");
        Serial.println(posLumMax3);
      }
      if (average4 > photocellReadingD) {
        photocellReadingD = average4;
        posLumMax4 = pos;
        Serial.print("position max d : ");
        Serial.println(posLumMax4);
      }
    }

    AngleServoHB = (posLumMax3 + posLumMax4) / 2;  // Calcul des positions finales pour servomoteur haut bas
    if (AngleServoHB > maxAngleServoHB) {
      AngleServoHB = maxAngleServoHB;
    } else if (AngleServoHB < minAngleServoHB) {
      AngleServoHB = minAngleServoHB;
    }
    servohb.write(AngleServoHB);  // Positionnement final du servomoteur haut bas
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print(posLumMax3);
    display.print(" ");
    display.println(posLumMax4);
    display.print("AngleM = ");
    display.println(AngleServoHB);
    display.display();
    delay(750);

    // Affichage des positions finales
    Serial.print("Capteur A : ");
    Serial.println(photocellReadingA);
    Serial.print("Capteur B : ");
    Serial.println(photocellReadingB);
    Serial.print("Capteur C : ");
    Serial.println(photocellReadingC);
    Serial.print("Capteur D : ");
    Serial.println(photocellReadingD);
    // Affichage des positions finales
    Serial.print("Position de luminosité max pour Servo 1 : ");
    Serial.println(AngleServoGD);
    Serial.print("Position de luminosité max pour Servo 2 : ");
    Serial.println(AngleServoHB);

    photocellReadingMoy = (photocellReadingA + photocellReadingB + photocellReadingC + photocellReadingD) / 4;
    Serial.print("photocellReadingMoy :");
    Serial.println(photocellReadingMoy);
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.print("LumMoy = ");
    display.println(photocellReadingMoy);
    display.print("AngleGD = ");
    display.println(AngleServoGD);
    display.print("AngleHB = ");
    display.println(AngleServoHB);
    display.display();
    delay(750);
  } else {
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0, 0);
    display.println("Pas de scan");
    display.println("LumMoy = ");
    display.println(photocellReadingMoy);
    display.display();
    delay(750);
  }
}

void batterie() {
  // Lire la valeur brute de l'ADC
  PontDiv = analogRead(pontdiv);
  Serial.println(PontDiv);

  // Calculer la moyenne mobile en ajoutant la nouvelle valeur et en retirant l'ancienne
  sampleTotal -= samples[sampleIndex];            // Soustraire l'ancienne valeur
  samples[sampleIndex] = analogRead(pontdiv);     // Ajouter la nouvelle valeur
  sampleTotal += analogRead(pontdiv);             // Mettre à jour le total
  sampleIndex = (sampleIndex + 1) % NUM_SAMPLES;  // Passer à l'échantillon suivant

  // Calculer la moyenne des échantillons
  avgPontDiv = sampleTotal / NUM_SAMPLES;
  Serial.print("Valeur  : ");
  Serial.print(avgPontDiv);

  // Calculer la tension réelle de la batterie
  //batt = avgPontDiv * (R1 + R2) / R2;


  // Convertir la valeur ADC moyenne en tension (sur une plage de 0 à 3,3V)
  measuredVoltage = (avgPontDiv / ADC_MAX_VALUE) * V_REF;
  //measuredVoltage = (measuredVoltage * (1 + calibration));
  // Afficher la tension mesurée
  Serial.print("Tension brute mesurée : ");
  Serial.print(measuredVoltage);
  Serial.println(" V");

  // Calculer la tension réelle de la batterie
  batteryVoltage = measuredVoltage * ((R1 + R2) / R2);

  int battPercent = 100 - ((2100 - avgPontDiv) * 0.2);
  int batteryVoltage2 = avgPontDiv * 4.2 / 2100;

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Read = ");
  display.println(avgPontDiv);
  display.print("Brut = ");
  display.println(measuredVoltage);
  display.print("Batt = ");
  display.println(batteryVoltage);
  display.print("Percent = ");
  display.println(battPercent);
  display.display();
  delay(2000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("Read = ");
  display.println(avgPontDiv);
  display.println(" ");
  display.print("Batt = ");
  display.println(batteryVoltage2);
  display.print("Percent = ");
  display.println(battPercent);
  display.display();
  delay(2000);
  //batteryVoltage =

  // Afficher la tension mesurée
  Serial.print("Tension mesurée (filtrée) : ");
  Serial.print(batteryVoltage);
  Serial.println(" V");
}

void sommeil() {
  if (WakeUp == 0) {
    if ((PontDiv < SeuilPontDiv) && enableEmailChecked == "true") {
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      sendEmailNotification();
      datatobdd();
      display.clearDisplay();
      photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
      posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
      delay(100);  // Pause entre chaque balayage
      display.setTextSize(1);
      display.setCursor(0, 0);
      display.println(" ");
      display.println("   DODO");
      display.display();
      delay(1000);
      EnregistrementHeureFlash();
      Serial.flush();
      esp_sleep_enable_timer_wakeup(0);
      esp_deep_sleep_start();
    }

    display.clearDisplay();
    datatobdd();
    photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
    posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
    delay(100);  // Pause entre chaque balayage
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println(" ");
    display.println("   SOMMEIL");
    display.display();
    delay(1000);
    EnregistrementHeureFlash();
    Serial.flush();
    esp_deep_sleep_start();
  }
}

void LectureCapteurs() {
  // Lire l'humidité du sol
  HumidSol = analogRead(HumiditeSol);
  if (HumidSol == 0) {
    HumidSol = 1;
  }
  delay(100);
  Serial.println(HumidSol);


  // Lire la détection de pluie
  Pluie = analogRead(27);
  if (Pluie == 0) {
    Pluie = 1;
  }
  delay(100);
  Serial.println(Pluie);


  /*
  // Agrégation des valeurs du diviseur de tension
  int sumPontDiv = 0;
  int PontDiv;
  for (int i = 0; i < numReadings; i++) {
    sumPontDiv += analogRead(pontdiv);
    delay(30);
  }
  PontDiv = sumPontDiv / numReadings;
  delay(100);*/

  // Lire la température et l'humidité de l'air intérieur
  tempAirInt = dhtint.readTemperature();
  delay(100);
  humidAirInt = dhtint.readHumidity();
  delay(100);
  if (isnan(humidAirInt) || isnan(tempAirInt)) {
    // Echec de la lecture DHT pour l'intérieur
  }

  // Lire la température et l'humidité de l'air extérieur
  tempAirExt = dhtext.readTemperature();
  delay(100);
  humidAirExt = dhtext.readHumidity();
  delay(100);
  if (isnan(humidAirExt) || isnan(tempAirExt)) {
    // Echec de la lecture DHT pour l'extérieur
  }

  // Obtenir la température du sol
  sensors.requestTemperatures();
  temperatureSol = sensors.getTempCByIndex(0);
  if (temperatureSol == -127.00) {
    temperatureSol = sensors.getTempCByIndex(0);
    delay(200);
  }
  if (temperatureSol == 25.00) {
    temperatureSol = sensors.getTempCByIndex(0);
    delay(200);
  }
}

void affichageOLED() {
  // Mise à jour de l'affichage OLED
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("    msp1 ");  // Affichage version et réseau WiFi
    display.print(" v");
    display.println(version);
    display.println(WiFi.SSID());
    display.println(WiFi.localIP());
    display.print("La:");
    display.print(photocellReadingA);
    display.print(" Lb:");
    display.println(photocellReadingB);
    display.print("Lc:");
    display.print(photocellReadingC);
    display.print(" Ld:");
    display.println(photocellReadingD);
    display.print("Lm:");
    display.println(photocellReadingMoy);
    display.print("pd:");
    display.print(batteryVoltage);
    display.println("V");
    display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
    display.display();
    delay(1000);
  }
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.print("HS:");
    display.println(analogRead(HumiditeSol));
    display.print("P:");
    display.println(analogRead(27));
    display.print("TS:");
    display.print(temperatureSol, 1);
    display.cp437(true);
    display.write(167);
    display.println("C");
    display.print("t:");
    display.print(tempAirInt, 1);
    display.cp437(true);
    display.write(167);
    display.print("C");
    display.print(" H:");
    display.print(humidAirInt, 1);
    display.println("% ");
    display.print(tempAirExt, 1);
    display.cp437(true);
    display.write(167);
    display.print("C");
    display.print(" H:");
    display.print(humidAirExt, 1);
    display.println("% ");
    display.print(" pd:");
    display.println(batteryVoltage);
    display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
    display.display();
    delay(1000);
  }
  for (int i = 0; i < 6; i++) {
    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(0, 0);
    display.println(" variables");
    display.print(" SSec:");
    display.print(SeuilSec);
    display.print(" Spd:");
    display.println(SeuilPontDiv);
    display.print("WakeUp:");
    display.print(WakeUp);
    display.print(" FWakeUp:");
    display.println(FreqWakeUp);
    display.print(" sHB:");
    display.print(AngleServoHB);
    display.print(" sGD:");
    display.println(AngleServoGD);
    display.print(" eRelais:");
    display.println(etatRelais);
    display.print("resetM: ");
    display.println(resetMode);
    display.print("pd:");
    display.print(batteryVoltage);
    display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
    display.display();
    delay(1000);
  }
}

void setup() {
  pinMode(RELAIS, OUTPUT);  // Initialiser la pin du relais comme sortie
  digitalWrite(RELAIS, 1);  // Activer le relais par défaut

  Serial.begin(115200);  // Initialiser la communication série

  n3OtaSyncBootPartition();

  // Configurer le module ESP en mode station WiFi
  WiFi.mode(WIFI_MODE_STA);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Initialiser l'affichage OLED
    for (;;)
      ;
  }
  delay(600);

  // Message initial sur l'affichage
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println(" Demarrage");
  display.println(" ");
  display.println("  msp1");
  display.print("  v:");
  display.println(version);
  display.display();

  pinMode(HumiditeSol, INPUT);  // Configurer le pin de l'humidité du sol
  pinMode(27, INPUT);           // Configurer le pin de l'humidité du sol
  pinMode(pontdiv, INPUT);      // Configurer le pin du pont diviseur
  pinMode(LUMINOSITEa, INPUT);  // Configurer les pins de luminosité
  pinMode(LUMINOSITEb, INPUT);
  pinMode(LUMINOSITEc, INPUT);
  pinMode(LUMINOSITEd, INPUT);
  pinMode(SERVOGD, OUTPUT);  // Configurer les pins pour servos
  pinMode(SERVOHB, OUTPUT);

  servogd.attach(SERVOGD);  // Attacher servos aux pins respectifs
  servohb.attach(SERVOHB);

  pinMode(DHTPININT, INPUT);  // Pin pour le DHT interne
  pinMode(DHTPINEXT, INPUT);  // Pin pour le DHT externe
  // Initialiser les capteurs DHT
  dhtint.begin();
  dhtext.begin();
  Serial.println("dht ok");


  //initialiser capteur température sol
  sensors.begin();            // Initialiser les capteurs Dallas
  sensors.setResolution(10);  // Définir la résolution du capteur à 10 bits (précision de 0,25 °C)
  Serial.println("t terre ok");

  // Initialiser le tableau d'échantillons
  for (int i = 0; i < NUM_SAMPLES; i++) {
    samples[i] = 0;
  }
  Serial.println("lum ok");

  Wificonnect();
  Serial.println("wifi ok");

  // OTA distant : verification MAJ a chaque boot/reveil (prod vs test selon build)
#ifdef TEST_MODE
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/msp-test/metadata.json",
      version.c_str(), -1, nullptr
  };
#else
  static const N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/msp/metadata.json",
      version.c_str(), -1, nullptr
  };
#endif
  n3OtaCheck(otaConfig);

  print_wakeup_reason();

  // Configuration et synchronisation temporelles
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  Serial.println("Configuration NTP terminée");

  //printLocalTime();

  // Mettre à jour les informations depuis ESP (définitions)
  variablestoesp();
  Serial.println("variables to esp ok");

  resetMode = 0;  // Réinitialisation du mode

  // Initialiser les servos à des valeurs par défaut
  //servogd.write(AngleServoGD);
  //servohb.write(AngleServoHB);
  ++bootCount;  // Incrémenter le compteur de démarrage

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);  // Configurer temps de sommeil
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, HIGH);                 // Configurer la pin de sortie de sommeil
  print_wakeup_reason();
}

void loop() {
  // Mise à jour du temps NTP
  //printLocalTime();
  digitalWrite(RELAIS, 1);  // Activer le relais

  // Initialisation du serveur Web
  server.begin();

  if (WiFi.status() != WL_CONNECTED) {  // Vérification de la connexion WiFi
    Wificonnect();
  }

  // Reconnexion si nécessaire
  if (WiFi.status() == WL_CONNECTED) {
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // Affichage heure RTC au format indiqué

  //printLocalTime();

  variablestoesp();  // Mise à jour des variables depuis la BDD

  LectureCapteurs();

  batterie();

  affichageOLED();

  Light_val();  // Suivi de la lumière

  sommeil();

  // Envoi régulier des données mesurées
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
  }

  photocellReadingA = photocellReadingB = photocellReadingC = photocellReadingD = 0;
  posLumMax1 = posLumMax2 = posLumMax3 = posLumMax4 = 0;
  delay(100);  // Pause entre chaque balayage
}
