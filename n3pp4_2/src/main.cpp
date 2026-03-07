/* N3PhasmesProto (n3pp) v4.5
 * Serre / aquaponie — salle aeree n3
 * Credentials externalises dans credentials.h
 * OTA HTTP distant via n3_common
 */

#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
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
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"

#include <ESP32Time.h>          //gestion de l'heure et de la date
#include <Preferences.h>
#include "credentials.h"
#include "n3_ota.h"

//définitions des pins pour les actionneurs
#define RELAIS 13
#define POMPE 12

//définitions des pins pour les capteurs
#define pontdiv 36

#define humidite1 33
#define humidite2 32
#define humidite3 35
#define humidite4 34

//définitions des pins pour les capteurs
#define LUMINOSITE 39

//configuration DHT
#define DHTPIN 18      // Digital pin connected to the DHT sensor (température et humidité)
#define DHTTYPE DHT11  // DHT 22 (AM2302)
DHT dht(DHTPIN, DHTTYPE);

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */
#define TIME_TO_SLEEP FreqWakeUp  /* Time ESP32 will go to sleep (in seconds) */

//variable et témoin issues et pour la bdd
int HeureArrosage = 6;
int SeuilSec = 5000;
//int SeuilPontDiv = 1100;
bool WakeUp = 0;
int FreqWakeUp = 3000;
bool ArrosageManu = 0;
bool resetMode = 0;

float temperatureAir;
float h;

int HumidMoy;
int photocellReading;
/*float Rsensor;
#define VIN 3.3 // V power voltage
#define R 10000 //ohm resistance value
int lux; //Lux value*/
bool etatPompe = 0;
bool etatRelais = 0;

int tempsArrosageMill = 1000;
int tempsArrosageSec = 4;
int tempsArrosage = tempsArrosageSec * tempsArrosageMill;

int Humid1;
int Humid2;
int Humid3;
int Humid4;

 // int PontDiv; 

// Interval between sensor readings. Learn more about timers: https://RandomNerdTutorials.com/esp32-pir-motion-sensor-interrupts-timers/
unsigned long previousMillisDatas = 0;
const long intervalDatas = 120000;

// Flag variable to keep track if email notification was sent or not
bool emailHumidSent = 0;
bool emailPontDivSent = 0;
RTC_DATA_ATTR bool arrosageFait = 1;

//wakeUp touch
RTC_DATA_ATTR int bootCount = 0;
//touch_pad_t touchPin;
bool WakeUpButton = 0;

#define emailSubject "Information N3PP"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

RTC_DATA_ATTR String inputMessageMailAd = SMTP_DEST;
RTC_DATA_ATTR String enableEmailChecked = "checked";

String emailMessage;

/* Declare the global used SMTPSession object for SMTP transport */
SMTPSession smtp;

//#define pontdiv 36  // Pin pour la lecture du diviseur de tension
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

// détinition des variables pour écrire dans la base de données d'olution
#ifdef TEST_MODE
const char* serverNamePostData = "http://iot.olution.info/n3pp-test/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = "http://iot.olution.info/n3pp-test/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#else
const char* serverNamePostData = "http://iot.olution.info/n3pp/n3ppdatas/post-n3pp-data.php";
const char* serverNameOutput = "http://iot.olution.info/n3pp/n3ppcontrol/n3pp-outputs-action.php?action=outputs_state&board=3";
#endif

String version = "4.5";

String apiKeyValue = API_KEY;
String sensorName = "n3pp";
String sensorLocation = "T06";

//définition de la résolution de l'écran
#define SCREEN_WIDTH 128  // OLED display width, in pixels
#define SCREEN_HEIGHT 64  // OLED display height, in pixels

// Declaration for an SSD1306 display connected to I2C (SDA, SCL pins)
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

const char* ssid = WIFI_SSID1;
const char* password = WIFI_PASS1;
const char* ssid2 = WIFI_SSID2;
const char* password2 = WIFI_PASS2;
const char* ssid3 = WIFI_SSID3;
const char* password3 = WIFI_PASS3;

String Wifiactif;

// Create AsyncWebServer object on port 80
AsyncWebServer server(80);

//temps NTP
WiFiUDP wifiUdp;
//NTPClient timeClient(wifiUdp, "pool.ntp.org", 3600, 60000);  // Ajust for your location

String outputsState;  // Variable pour gérer l'état des sorties à distance

//variables en lien avec le temps rtc et ntp
const char* ntpServer = "pool.ntp.org";  // Adresse du serveur NTP
const long gmtOffset_sec = 3600;         // Décalage GMT en secondes
const int daylightOffset_sec = 3600;     // Décalage pour l'heure d'été en secondes
ESP32Time rtc;                           //initialisation du contrôle RTC via la bibliothèque ESP32time
Preferences preferences;                 //initialisation du stockage dse l'heure dans la mémoire flash
int seconde;
int minute;
int heure;
int jour;
int mois;
int annee;

// Send HTTP POST request
unsigned int httpResponseCode;

void HeureSansWifi() {
  preferences.begin("rtc", true);           //ouverture de la session de lecture-écriture dans la mémoire flash
  heure = preferences.getInt("heure", 12);  //lecture et attribution des variables enregistrées associées à l'heure
  minute = preferences.getInt("minute", 0);
  seconde = preferences.getInt("seconde", 0);
  jour = preferences.getInt("jour", 1);
  mois = preferences.getInt("mois", 1);
  annee = preferences.getInt("annee", 2023);
  preferences.end();                                       //fermeutre de la session de lecture-écriture dans la mémoire flash
  rtc.setTime(seconde, minute, heure, jour, mois, annee);  // définition RTC de l'heure sans synchro NTP
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Load H flash");
  display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  display.display();
  delay(500);
}

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
  //requête publication datas dans bdd état pin dans la base de données
  Serial.println("DATATOBDD!!!");
  display.drawCircle(5, 5, 5, WHITE);
  display.display();
  if (WiFi.status() == WL_CONNECTED) {  //Check WiFi connection status
    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNamePostData);                               // Your Domain name with URL path or IP address with path
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");  // Specify content-type header
    // Prepare your HTTP POST request data
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
    display.fillCircle(5, 5, 5, WHITE);
    display.display();
    Serial.println(httpRequestData);
    int httpResponseCode = http.POST(httpRequestData);
    Serial.println(httpResponseCode);
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
  display.drawCircle(115, 5, 5, WHITE);
  display.display();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("recup info bdd");

    WiFiClient client;
    HTTPClient http;
    http.begin(client, serverNameOutput);  // Your Domain name with URL path or IP address with path
    outputsState = httpGETRequest(serverNameOutput);
    delay(2000);

    Serial.println(outputsState);
    //WebSerial.println(outputsState);
    JSONVar myObject = JSON.parse(outputsState);
    // JSON.typeof(jsonVar) can be used to get the type of the var
    if (JSON.typeof(myObject) == "undefined") {
      Serial.println("Parsing input failed!");
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
  display.fillCircle(115, 5, 5, WHITE);
  display.display();
}

/*
//fonction temps NTP
void printLocalTime() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    Serial.println("Failed to obtain time");
    return;
  }
  Serial.print("temps NTP ");
  Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");

  heure = timeinfo.tm_hour;
  minute = timeinfo.tm_min;
  seconde = timeinfo.tm_sec;
  jour = timeinfo.tm_mday;
  mois = timeinfo.tm_mon + 1;
  annee = timeinfo.tm_year + 1900;

  Serial.print("Print local time");
  Serial.print(heure);
  Serial.print(":");
  Serial.print(minute);
  Serial.print(":");
  Serial.print(seconde);
  Serial.print("  ");
  Serial.print(jour);
  Serial.print("-");
  Serial.print(mois);
  Serial.print("-");
  Serial.println(annee);
}*/

void Wificonnect() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  display.println("Wifi");
  display.println("CONNECTING");
  display.display();

  struct { const char* ssid; const char* pass; } networks[] = {
    {ssid, password}, {ssid2, password2}, {ssid3, password3}
  };

  for (int n = 0; n < 3; n++) {
    WiFi.disconnect(true);
    delay(100);
    WiFi.begin(networks[n].ssid, networks[n].pass);
    Serial.printf("Connexion a %s...\n", networks[n].ssid);

    unsigned long start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < 10000) {
      delay(500);
      Serial.print(".");
    }
    Serial.println();

    if (WiFi.status() == WL_CONNECTED) {
      Wifiactif = networks[n].ssid;
      break;
    }
    Serial.printf("Echec %s\n", networks[n].ssid);
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Failed! Tous les reseaux echoues");
    HeureSansWifi();
    display.println("FAILED");
    display.display();
  }

  delay(100);
  Serial.print("Reseau wifi: ");
  Serial.println(Wifiactif);
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
  display.println(Wifiactif);
  display.println("END");
  display.display();
}


//fonction permettant l'enregistrement des variables de temps dans la mémoire flash
void EnregistrementHeureFlash() {
  rtc.getTime("%H:%M:%S %d/%m/%Y");
  seconde=rtc.getTime("%S").toInt();
  minute=rtc.getTime("%M").toInt();
  heure=rtc.getTime("%H").toInt();
  jour=rtc.getTime("%d").toInt();
  mois=rtc.getTime("%m").toInt();
  annee=rtc.getTime("%Y").toInt();
  preferences.begin("rtc", false);
  preferences.putInt("heure", heure);
  preferences.putInt("minute", minute);
  preferences.putInt("seconde", seconde);
  preferences.putInt("jour", jour);
  preferences.putInt("mois", mois);
  preferences.putInt("annee", annee);
  preferences.end();
}

/*
Method to print the touchpad by which ESP32
has been awaken from sleep
*/
/*
 void print_wakeup_touchpad(){
  touchPin = esp_sleep_get_touchpad_wakeup_status();

  switch(touchPin)
  {
    case 0  : Serial.println("Touch detected on GPIO 4"); break;
    case 1  : Serial.println("Touch detected on GPIO 0"); break;
    case 2  : Serial.println("Touch detected on GPIO 2"); break;
    case 3  : Serial.println("Touch detected on GPIO 15"); break;
    case 4  : Serial.println("Touch detected on GPIO 13"); break;
    case 5  : Serial.println("Touch detected on GPIO 12"); break;
    case 6  : Serial.println("Touch detected on GPIO 14"); break;
    case 7  : Serial.println("Touch detected on GPIO 27"); break;
    case 8  : Serial.println("Touch detected on GPIO 33"); break;
    case 9  : Serial.println("Touch detected on GPIO 32"); break;
    default : Serial.println("Wakeup not by touchpad"); break;
  }
}*/

//configuration des paramètres pour l'envoi d'un mail
void sendEmailNotification() {
  /* Declare the Session_Config for user defined session credentials */
  Session_Config config;

  /* Set the session config */
  config.server.host_name = SMTP_HOST;
  config.server.port = SMTP_PORT;
  config.login.email = AUTHOR_EMAIL;
  config.login.password = AUTHOR_PASSWORD;
											

  /* Declare the message class */
  SMTP_Message message;

  /* Set the message headers */
  message.sender.name = F("OAL");
  message.sender.email = AUTHOR_EMAIL;

  message.subject = emailSubject;

  message.addRecipient(F("OAL"), inputMessageMailAd);

  message.text.content = emailMessage;

  message.priority = esp_mail_smtp_priority::esp_mail_smtp_priority_low;

  /* Connect to the server */
  if (!smtp.connect(&config)) {
    MailClient.printf("Connection error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());
    return;
  }

  /** Or connect without log in and log in later

     if (!smtp.connect(&config, false))
       return;

     if (!smtp.loginWithPassword(AUTHOR_EMAIL, AUTHOR_PASSWORD))
       return;
  */

  if (!smtp.isLoggedIn()) {
    Serial.println("Not yet logged in.");
  } else {
    if (smtp.isAuthenticated())
      Serial.println("Successfully logged in.");
    else
      Serial.println("Connected with no Auth.");
  }

  /* Start sending Email and close the session */
  if (!MailClient.sendMail(&smtp, &message))
    MailClient.printf("Error, Status Code: %d, Error Code: %d, Reason: %s\n", smtp.statusCode(), smtp.errorCode(), smtp.errorReason().c_str());

  // to clear sending result log
  smtp.sendingResult.clear();
}

void arrosage() {
  digitalWrite(POMPE, 1);
  Serial.println("arrosage en cours");
  delay(tempsArrosage);
  digitalWrite(POMPE, 0);
  Serial.println("arrosage terminé");
  delay(1000);
}

void lectureCapteurs() {
  //variable locale niveau eau potager
  Humid1 = analogRead(humidite1);  // read the analog value from sensor
  Humid1 = Humid1;
  if (Humid1 == 0) {
    Humid1 = 1;
  }
  Serial.print("Capteur humidité 1 : ");
  //WebSerial.print("Eau du potager : ");

  Serial.println(Humid1);
  //WebSerial.println(Humid1);
  delay(100);

  //variable locale niveau eau potager
  Humid2 = analogRead(humidite2);  // read the analog value from sensor
  Humid2 = Humid2;

  if (Humid2 == 0) {
    Humid2 = 1;
  }
  Serial.print("Capteur humidité 2 : ");
  //WebSerial.print("temperature eau : ");
  Serial.println(Humid2);
  //WebSerial.print(Humid2);
  delay(100);

  //variable locale niveau eau potager
  Humid3 = analogRead(humidite3);  // read the analog value from sensor
  if (Humid3 == 0) {
    Humid3 = 1;
  }
  Serial.print("Capteur humidité 3 : ");
  //WebSerial.print("temperature eau : ");
  if (Humid3 == 0) {
    Humid3 = 1;
  }
  Serial.println(Humid3);
  //WebSerial.print(Humid3);
  delay(100);

  //variable locale niveau eau potager
  Humid4 = analogRead(humidite4);  // read the analog value from sensor
  if (Humid4 == 0) {
    Humid4 = 1;
  }
  Serial.print("Capteur humidité 4 : ");
  //WebSerial.print("temperature eau : ");
  Serial.println(Humid4);
  //WebSerial.print(Humid4);
  delay(100);

  HumidMoy = (Humid1 + Humid2 + Humid3 + Humid4) / 4;  // read the analog value from sensor
  Serial.print("Capteur humidité moyenne : ");
  Serial.println(HumidMoy);

  //variable locale niveau eau potager
  PontDiv = analogRead(pontdiv);  // read the analog value from sensor
  Serial.print("pontdiv : ");
  //WebSerial.print("temperature eau : ");
  Serial.print(PontDiv);
  //WebSerial.print(PontDiv);
  delay(100);

  Serial.print("seuilsec2 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage2 : ");
  Serial.println(tempsArrosage);

  //variables locales temperature and humidity
  temperatureAir = dht.readTemperature();
  Serial.println(temperatureAir);
  delay(500);  //fait paniquer le coeur
  h = dht.readHumidity();
  delay(500);
  Serial.println(h);
  if (isnan(h) || isnan(temperatureAir)) {
    Serial.println("Echec de lecture du DHT");
    //WebSerial.println("Echec de lecture du DHT");
  }

  //variable locale capteur de luminosité
  photocellReading = analogRead(LUMINOSITE);  // the analog reading from the analog resistor divider
  if (photocellReading == 0) {
    photocellReading = 1;
  }
  //variable locale transformation variable
  /* Rsensor = ((4096.0 - photocellReading)*10/photocellReading);
  Serial.print("La luminosité brute est de : ");
  //WebSerial.print("La luminosité brute est de : ");
  Serial.println(photocellReading);
  //WebSerial.println(photocellReading);
  Serial.print("La luminosité calculée est de : ");
  //WebSerial.print("La luminosité calculée est de : ");
  Serial.println(Rsensor);//show the ligth intensity on the serial monitor;
  //WebSerial.println(Rsensor);//show the ligth intensity on the serial monitor;
  //delay(500);
  lux=sensorRawToPhys(photocellReading);
  Serial.print(F("Raw value from sensor= "));
  Serial.println(photocellReading); // the analog reading
  Serial.print(F("Physical value from sensor = "));
  Serial.print(lux); // the analog reading
  Serial.println(F(" lumen")); // the analog reading*/
}

void affichageOLED() {
  //display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay();
  //display.setTextColor(0xFFE0);
  display.setTextSize(1);
  display.setCursor(0, 0);
  //affichage version
  display.print("    n3pp ");
  display.print(" v");
  display.println(version);
  //display.setTextColor(0x07FF);
  // nom réseau Wifi
  display.println(WiFi.SSID());
  //IP réseau Wifi
  display.println(WiFi.localIP());
  /*display.print ("IP AP ");
  display.println(WiFi.softAPIP());*/
  // display temperature
  display.print("h1:");
  display.print(Humid1);
  //display eau aquarium
  display.print(" h2:");
  display.println(Humid2);
  //display eau réserve
  display.print("h3:");
  display.print(Humid3);
  //display eau potager
  display.print(" h4:");
  display.println(Humid4);
  //display eau potager
  display.print("hm:");
  display.print(HumidMoy);
  // display Luminosité
  display.print(" L:");
  display.println(photocellReading);
  //display eau potager
  display.print(" pd:");
  display.println(PontDiv);
  //display humidité
  /*  display.print("t:");
  display.print(temperatureAir,1);
  display.cp437(true);
  display.write(167);
  display.print("C");
  display.print(" H:");
  display.print(h,1);
  display.println("% "); */
  //display.display();
  //rtc.adjust(DateTime(__DATE__, __TIME__));
  // display heure et date
  //display.setTextColor(0xF800);
  //DateTime now = rtc.now();
  display.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));
  Serial.print(rtc.getTime("%H:%M:%S %d/%m/%Y"));

  display.display();
  delay(4000);

  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  //affichage variables
  display.println(" variables");
  display.print("HeureArros:");
  display.print(digitalRead(HeureArrosage));
  display.print(" tpsArros:");
  display.println(tempsArrosageSec);
  display.print(" SSec:");
  display.print(digitalRead(SeuilSec));
  display.print(" Spd:");
  display.println(digitalRead(SeuilPontDiv));
  display.print("WakeUp:");
  display.print(digitalRead(WakeUp));
  //Heures de nourrisage
  display.print(" FWakeUp:");
  display.println(FreqWakeUp);

  //Temps de nourrisage
  display.print("Pompe:");
  display.print(etatPompe);
  //Temps de nourrisage
  display.print(" eRelais:");
  display.println(etatRelais);
  display.print("resetM: ");
  display.print(resetMode);
  display.display();
  delay(4000);
}

void sommeil() {
  //Go to sleep now
  Serial.println("WakeUp : ");
  Serial.print(WakeUp);
  if (WakeUp == 0) {

    if ((PontDiv < SeuilPontDiv) && enableEmailChecked == "true") {
      emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
      sendEmailNotification();
      datatobdd();
      display.clearDisplay();
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
    //temps de sommeil
    Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) + " Seconds");
    display.setTextSize(1);
    display.setCursor(0, 35);
    display.println(" ");
    display.println("   Going to sleep now");
    Serial.println("Going to sleep now");
    display.display();
    delay(1000);
    EnregistrementHeureFlash();
    Serial.flush();
    esp_deep_sleep_start();
    Serial.println("This will never be printed");
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

  int battPercent = 100-((2100 - avgPontDiv)*0.2);
  int batteryVoltage2 = avgPontDiv*4.2/2100;

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

void automatismes() {
  //remplissage de l'aquarium cas si l'aquarium est trop bas et la réserve assez remplie

  //mail si sécheresse trop forte
  if ((HumidMoy < SeuilSec) && enableEmailChecked == "checked" && !emailHumidSent) {
    emailMessage = String("Le sol est sec. L'humidité moyenne est de ") + String(HumidMoy);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
      emailHumidSent = true;
    
    //variablestoesp();
    datatobdd();
  }

  // mail si le niveau est revenu à la normale
  if ((HumidMoy > SeuilSec) && enableEmailChecked == "checked" && emailHumidSent) {
    String emailMessage = String("L'humidité est remonté. La moyenne est maintenant de ") + String(HumidMoy);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
      emailHumidSent = false;
    //variablestoesp();
    datatobdd();
  }


  Serial.print("seuilsec3 : ");
  Serial.println(SeuilSec);
  Serial.print("tempsArrosage3: ");
  Serial.println(tempsArrosage);
  //mail si tension trop basse

  if ((PontDiv < SeuilPontDiv)) {
    if (enableEmailChecked == "true" && !emailPontDivSent) {
      String emailMessage = String("La batterie est faible. Son niveau est de ") + String(PontDiv);
    sendEmailNotification();
      Serial.println(emailMessage);
      // SerialBT.println(emailMessage);
        emailPontDivSent = true;
      }
    esp_deep_sleep_start();
  }

  //arrosage en cas de sécheresse
  if ((HumidMoy < SeuilSec)) {
    arrosage();
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage auto effectué ");
      Serial.println("arrosage auto");
      sendEmailNotification();
    }
    //variablestoesp();
    datatobdd();
  }

  rtc.getTime("%H:%M:%S %d/%m/%Y");
  heure = rtc.getTime("%H").toInt();

  Serial.print("heure : ");
  Serial.println(heure);

  //arrosage auto
  if (HeureArrosage != heure) {
    arrosageFait = 0;
    Serial.println("arrosage pas à l'heure");
  }

  if ((HeureArrosage == heure) && arrosageFait == 0) {
    arrosage();
    arrosageFait = 1;
    Serial.println("arrosage heure auto effectué");
    Serial.print("bouffe soir 2 :");
    Serial.println(arrosageFait);
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage heure auto effectué ");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
    //variablestoesp();
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }

  //bouffe manuelle
  if (ArrosageManu == 1) {
    datatobdd();
    Serial.println(ArrosageManu);
    arrosage();
    ArrosageManu = 0;
    //arrosageFait = 0;
    if (enableEmailChecked == "checked") {
      String emailMessage = String("arrosage manu effectué ");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
    //variablestoesp();
    etatPompe = 1;
    datatobdd();
    etatPompe = 0;
  }
}

// Fonction pour obtenir la raison du réveil de l'ESP32
void print_wakeup_reason() {
  esp_sleep_wakeup_cause_t wakeup_reason;
  wakeup_reason = esp_sleep_get_wakeup_cause();

  switch (wakeup_reason) {
    case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;
    case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;
    case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;
    case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;
    default:
      Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason);
      preferences.begin("rtc", true);           //ouverture de la session de lecture-écriture dans la mémoire flash
      heure = preferences.getInt("heure", 12);  //lecture et attribution des variables enregistrées associées à l'heure
      minute = preferences.getInt("minute", 0);
      seconde = preferences.getInt("seconde", 0);
      jour = preferences.getInt("jour", 1);
      mois = preferences.getInt("mois", 1);
      annee = preferences.getInt("annee", 2023);
      preferences.end();                                       //fermeutre de la session de lecture-écriture dans la mémoire flash
      rtc.setTime(seconde, minute, heure, jour, mois, annee);  // définition RTC de l'heure sans synchro NTP
      break;
  }
}

void setup() {
  // Démarrage du moniteur série
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);

  pinMode(POMPE, OUTPUT);   //lumière
  pinMode(RELAIS, OUTPUT);  //lumière
  digitalWrite(RELAIS, 1);

  Serial.begin(115200);
  delay(500);

  WiFi.mode(WIFI_MODE_STA);

  print_wakeup_reason();

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {  // Address 0x3D for 128x64
    //Serial.println(F("SSD1306 allocation failed"));
    for (;;)
      ;
  }
  delay(600);
  display.clearDisplay();
  display.setTextColor(WHITE);
  // affichage OLED
  //display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0, 0);
  //affichage version
  display.println(" Starting");
  display.println(" ");
  display.println("  n3pp");
  display.print("  v:");
  display.println(version);
  display.display();

  // définition de l'état initial des pins
  pinMode(humidite1, INPUT);
  pinMode(humidite2, INPUT);
  pinMode(humidite3, INPUT);
  pinMode(humidite4, INPUT);
  pinMode(pontdiv, INPUT);
  //pinMode(contact, INPUT);
  pinMode(LUMINOSITE, INPUT);  //lumière


  //  attachInterrupt(LUMIERE, isr, RISING);

  //démarrage du DHT
  dht.begin();
  Serial.println("dht begin");

  /*
  //démarrage de la fonction OTA autohébergé
  AsyncElegantOTA.begin(&server);    // Start ElegantOTA*/
  /*  
  //démarrage du serveur autohébergé
  server.begin();

  //display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  //démarrage OLED
  if(!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) { // Address 0x3D for 128x64
    Serial.println(F("SSD1306 allocation failed"));
    for(;;);
  }
  //delay(500);
  display.clearDisplay();
  display.setTextColor(WHITE);
*/
  //affichage version
  //Serial.print("version : ");
  //Serial.println(version);
  /*
  //OTA basique
    // Port defaults to 3232
    ArduinoOTA.setPort(3232);

  // Hostname defaults to esp3232-[MAC]
   ArduinoOTA.setHostname("n3pp");

  // No authentication by default
  // ArduinoOTA.setPassword("admin");

  // Password can be set with it's md5 value as well
  // MD5(admin) = 21232f297a57a5a743894a0e4a801fc3
  // ArduinoOTA.setPasswordHash("21232f297a57a5a743894a0e4a801fc3");

  ArduinoOTA
    .onStart([]() {
      String type;
      if (ArduinoOTA.getCommand() == U_FLASH)
        type = "sketch";
      else // U_SPIFFS
        type = "filesystem";

      // NOTE: if updating SPIFFS this would be the place to unmount SPIFFS using SPIFFS.end()
      Serial.println("Start updating " + type);
    })
    .onEnd([]() {
      Serial.println("\nEnd");
    })
    .onProgress([](unsigned int progress, unsigned int total) {
      Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
    })
    .onError([](ota_error_t error) {
      Serial.printf("Error[%u]: ", error);
      if (error == OTA_AUTH_ERROR); 
      Serial.println("Auth Failed");
      if (error == OTA_BEGIN_ERROR);
      Serial.println("Begin Failed");
      if (error == OTA_CONNECT_ERROR); 
      Serial.println("Connect Failed");
      if (error == OTA_RECEIVE_ERROR); 
      Serial.println("Receive Failed");
      if (error == OTA_END_ERROR); 
      Serial.println("End Failed");
    });

  //serveur OTA 
  ArduinoOTA.begin();*/

  Wificonnect();

  // OTA distant : verification MAJ a chaque boot/reveil (prod vs test selon build)
#ifdef TEST_MODE
  N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp-test/metadata.json",
      version.c_str(), -1
  };
#else
  N3OtaConfig otaConfig = {
      "http://iot.olution.info/ota/n3pp/metadata.json",
      version.c_str(), -1
  };
#endif
  n3OtaCheck(otaConfig);

  /* //init and get the time
  if(WiFi.status()== WL_CONNECTED ){ 
    Serial.println("start NTP");
    timeClient.begin();                                       //
    timeClient.update();                                      //
    //timeClient.forceUpdate();                                 //
    Serial.println(timeClient.getFormattedTime()); // hh mm ss
    Serial.println(timeClient.getFormattedDate()); // dd mm yyyy*/
  /*    if ( rtc.lostPower() ){
      Serial.println("RTC lost power, let's set the time!");
      adjustRTC();
    }
      // Set RTC time to NTP time
  //rtc.adjust(DateTime(timeClient.getEpochTime()));
  RTCdate();
  rtcmodule();
  RTCdate();
  printLocalTime();
  }*/
  //init and get the time
  configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  //printLocalTime();

  variablestoesp();
  resetMode = 0;
  etatPompe = 0;
  etatRelais = 1;
  Serial.print("RESET SETUP : ");
  Serial.println(resetMode);
  //datatobdd();

  //Increment boot number and print it every reboot
  ++bootCount;
  Serial.println("Boot number: " + String(bootCount));



  //Configure GPIO33 as a wake-up source when the voltage is 3.3V
  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_4, HIGH);
  //Print the wakeup reason for ESP32 and touchpad too
  //  print_wakeup_touchpad();


  //Setup interrupt on Touch Pad 0 (GPIO4)
  //touchAttachInterrupt(T0, loop, seuilContact);

  //Configure Touchpad as wakeup source
  //esp_sleep_enable_touchpad_wakeup();
}

void loop() {
  // Update NTP time
  //printLocalTime();

  //print_wakeup_reason();
  digitalWrite(RELAIS, 1);

  //démarrage du serveur autohébergé
  server.begin();

  // Connection à un réseau wifi
  if (WiFi.status() != WL_CONNECTED) {
    Wificonnect();
  }

  // Connection à un réseau wifi
  if (WiFi.status() == WL_CONNECTED) {
    //init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);
  }
  Serial.println("temps 2 : ");
  //printLocalTime();

  Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // (String) returns time with specified format

  etatRelais = 1;
  //récupération des infos de la bdd
  variablestoesp();

  //contrôle de l'état actif de la pompe ou pas
  if (digitalRead(POMPE) == 1) {
    etatPompe = 1;
    datatobdd();
    if (enableEmailChecked == "checked") {
      String emailMessage = String("ATTENTION, arrosage continu en cours !!!");
      //bouffeSoirTemoin = 1;
      sendEmailNotification();
    }
  }

  lectureCapteurs();

  batterie();

  affichageOLED();

  automatismes();

  etatRelais = 1;

  sommeil();

  //envoi régulier des datas mesurées sur iot.olution.info
  unsigned long currentMillisDatas = millis();
  if (currentMillisDatas - previousMillisDatas >= intervalDatas) {
    previousMillisDatas = currentMillisDatas;
    datatobdd();
    EnregistrementHeureFlash();
    if (WiFi.status() != WL_CONNECTED) {
      //Serial.println("WiFi Failed! Test également échoué");
      //Web//Serial.println("WiFi Failed! Test également échoué");
      //  return;
      //display.println("FAILED");
      display.display();
      HeureSansWifi();
      display.clearDisplay();
      display.setTextSize(2);
      display.setCursor(0, 0);
      display.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));
      display.display();
      delay(1000);
    }

    Serial.println(rtc.getTime("%H:%M:%S %d/%m/%Y"));  // (String) returns time with specified format

  }
}
