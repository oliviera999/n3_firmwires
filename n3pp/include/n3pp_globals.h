#pragma once

#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP32Time.h>
#include <Preferences.h>
#include <ESP_Mail_Client.h>

#include "n3pp_config.h"

//variable et témoin issues et pour la bdd
extern int HeureArrosage;
extern int SeuilSec;
extern bool WakeUp;
extern int FreqWakeUp;
extern bool ArrosageManu;
extern bool resetMode;
// Interrupteur serveur (GPIO virtuel 112) : autorise ou non la mise en veille
// infinie (sommeil GPIO-only) quand la batterie passe sous SeuilPontDiv.
// 1 = veille infinie active (comportement historique), 0 = desactivee.
extern bool VeilleInfinie;

extern float temperatureAir;
extern float h;

extern int HumidMoy;
extern int photocellReading;
extern bool etatPompe;
extern bool etatRelais;

extern int tempsArrosageMill;
extern int tempsArrosageSec;
extern int tempsArrosage;

extern int Humid1;
extern int Humid2;
extern int Humid3;
extern int Humid4;
// Nombre de capteurs d'humidite sol valides (non debranches) lors de la
// derniere lecture : 0 = aucune mesure fiable, l'arrosage auto est bloque.
extern int soilValidCount;

// Intervalle entre deux lectures capteurs (en ms)
extern unsigned long previousMillisDatas;
extern const long intervalDatas;

// Indicateur : email d'alerte déjà envoyé ou non (évite spam)
extern bool postOkThisWake;   // Phase 3 : POST de ce reveil OK -> serveur primaire
extern uint8_t failoverMailsSent;  // Phase 3 : budget mails failover (episode hors-ligne)
extern bool emailHumidSent;
extern bool emailPontDivSent;
extern bool emailPompeSent;
extern bool arrosageFait;

//wakeUp touch
extern int bootCount;

extern String inputMessageMailAd;
extern String enableEmailChecked;

extern String emailMessage;

/* Session SMTP désormais locale à n3_mail (plus de global). */

extern int PontDiv;
extern int avgPontDiv;
extern float measuredVoltage;
extern float batteryVoltage;
extern int SeuilPontDiv;
// Constantes pour la conversion de la mesure analogique en tension réelle
extern const float ADC_MAX_VALUE;
extern const float V_REF;
extern const float calibration;
// Tableau pour stocker les échantillons
extern int samples[NUM_SAMPLES];
extern int sampleIndex;
extern int sampleTotal;

// Définition des URLs serveur (base de données olution / iot.olution.info)
extern const char* serverNamePostData;
extern const char* serverNameOutput;
extern const char* serverNameHeartbeat;

extern String version;
extern String apiKeyValue;
extern String sensorName;

// Affichage SSD1306 connecté en I2C (broches SDA, SCL)
extern Adafruit_SSD1306 display;
extern bool displayOk;

extern const char* ssid;
extern const char* password;
extern const char* ssid2;
extern const char* password2;
extern const char* ssid3;
extern const char* password3;

extern String Wifiactif;

// NTP : ajuster offset/fuseau dans gmtOffset_sec et daylightOffset_sec selon la localisation

extern String outputsState;

//variables en lien avec le temps rtc et ntp
extern const char* ntpServer;
extern const long gmtOffset_sec;
extern const int daylightOffset_sec;
extern ESP32Time rtc;
extern Preferences preferences;
extern int seconde;
extern int minute;
extern int heure;
extern int jour;
extern int mois;
extern int annee;

// Code de réponse HTTP (requêtes GET/POST)
extern unsigned int httpResponseCode;

extern DHT dht;
