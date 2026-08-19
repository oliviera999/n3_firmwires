/* MeteoStationPrototype (msp1) — Variables globales
 * Déclarations extern de toutes les variables partagées entre modules.
 * Les définitions sont dans src/msp_globals.cpp (extraites de main.cpp,
 * préalable T6 lot 0 du chantier core shared — même découpe que n3pp 4.38).
 */
#pragma once

#include "msp_config.h"
#include "credentials.h"
#include <Arduino.h>
#include <DHT.h>
#include <n3_bme280.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <ESP32Servo.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ESP_Mail_Client.h>
#include <ESP32Time.h>
#include <Preferences.h>

// --- Capteurs température sol (DS18B20) ---
extern OneWire oneWire;
extern DallasTemperature sensors;
extern float temperatureSol;

// --- Luminosité ---
extern int photocellReadingA, photocellReadingB, photocellReadingC, photocellReadingD;
extern int photocellReadingMoy;

// --- Servos (tracker solaire) ---
extern Servo servogd;
extern Servo servohb;
extern int posLumMax1, posLumMax2, posLumMax3, posLumMax4;
extern int AngleServoGD;
extern int AngleServoHB;
extern bool servoModeAuto;
extern bool trackerModeSweep;   // clé serveur 113 : 1 = balayage classique, 0 = différentiel (défaut)
extern int ldrCalibCommand;     // clé serveur 114 : 1 = calibrer les LDR, 2 = gains neutres (front)
extern int rtcAngleServoGD;     // dernière position appliquée, persistée en RTC RAM (deep sleep)
extern int rtcAngleServoHB;

// --- DHT intérieur / extérieur ---
extern DHT dhtint;
extern DHT dhtext;
extern N3Bme280 bmeInt;
extern N3Bme280 bmeExt;
extern float pressionAirInt;
extern float tempAirInt;
extern float humidAirInt;
extern float tempAirExt;
extern float humidAirExt;

// --- Deep sleep ---
extern bool WakeUp;
extern int FreqWakeUp;
// Interrupteur serveur (GPIO virtuel 112) : autorise ou non la mise en veille
// infinie (sommeil GPIO-only) quand la batterie passe sous SeuilPontDiv.
// 1 = veille infinie active (comportement historique), 0 = desactivee.
extern bool VeilleInfinie;

// --- Batterie / pont diviseur ---
extern int PontDiv;
extern int avgPontDiv;
extern float measuredVoltage;
extern float batteryVoltage;
extern int SeuilPontDiv;
extern int samples[NUM_SAMPLES];
extern int sampleIndex;
extern int sampleTotal;

// --- Seuils / états ---
extern int SeuilSec;
extern bool resetMode;
extern bool etatRelais;

// --- Capteurs analogiques ---
extern int HumidSol;
extern int Pluie;

// --- Email ---
extern int bootCount;
extern bool postOkThisWake;       // Phase 3 : POST de ce reveil OK -> serveur primaire
extern uint8_t failoverMailsSent; // Phase 3 : budget mails failover (episode hors-ligne)
extern String inputMessageMailAd;
extern String enableEmailChecked;
extern String emailMessage;
/* Session SMTP désormais locale à n3_mail (plus de global). */

// --- Réseau ---
extern unsigned int httpResponseCode;
extern String version;
extern String apiKeyValue;
extern String sensorName;
extern const char* serverNamePostData;
extern const char* serverNameOutput;
extern const char* serverNameHeartbeat;
extern String Wifiactif;
extern String outputsState;

// --- WiFi ---
extern const char* ssid;
extern const char* password;
extern const char* ssid2;
extern const char* password2;
extern const char* ssid3;
extern const char* password3;

// --- Affichage OLED ---
extern Adafruit_SSD1306 display;
extern bool displayOk;

// --- Temps RTC / NTP ---
extern ESP32Time rtc;
extern Preferences preferences;
extern int seconde;
extern int minute;
extern int heure;
extern int jour;
extern int mois;
extern int annee;
