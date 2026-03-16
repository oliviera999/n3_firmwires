/* MeteoStationPrototype (msp1) — Variables globales
 * Déclarations extern de toutes les variables partagées entre modules.
 * Les définitions sont dans main.cpp.
 */
#pragma once

#include "msp_config.h"
#include "credentials.h"
#include <Arduino.h>
#include <DHT.h>
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

// --- Luminosité (filtrage moyenne mobile) ---
extern int readings1[numReadings];
extern int readings2[numReadings];
extern int readings3[numReadings];
extern int readings4[numReadings];
extern int readIndex;
extern int total1, total2, total3, total4;
extern int average1, average2, average3, average4;
extern int photocellReadingA, photocellReadingB, photocellReadingC, photocellReadingD;
extern int photocellReadingMoy;

// --- Servos (tracker solaire) ---
extern Servo servogd;
extern Servo servohb;
extern int posLumMax1, posLumMax2, posLumMax3, posLumMax4;
extern int AngleServoGD;
extern int AngleServoHB;

// --- DHT intérieur / extérieur ---
extern DHT dhtint;
extern DHT dhtext;
extern float tempAirInt;
extern float humidAirInt;
extern float tempAirExt;
extern float humidAirExt;

// --- Deep sleep ---
extern bool WakeUp;
extern int FreqWakeUp;

// --- Batterie / pont diviseur ---
extern int PontDiv;
extern int avgPontDiv;
extern float batt;
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
extern int Oled;

// --- Capteurs analogiques ---
extern int HumidSol;
extern int Pluie;

// --- Email ---
extern bool emailHumidSent;
extern int bootCount;
extern String inputMessageMailAd;
extern String enableEmailChecked;
extern String emailMessage;
extern SMTPSession smtp;

// --- Réseau ---
extern unsigned int httpResponseCode;
extern String version;
extern String apiKeyValue;
extern String sensorName;
extern String sensorLocation;
extern const char* serverNamePostData;
extern const char* serverNameOutput;
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
