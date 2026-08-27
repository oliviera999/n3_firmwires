/* MeteoStationPrototype (msp1) — Configuration
 * Constantes, pins, URLs serveur, version
 */
#pragma once

#include "credentials.h"
#include "n3_defaults.h"

#ifndef API_SIG_SECRET
#define API_SIG_SECRET ""
#endif

#define FIRMWARE_VERSION "2.75"

// Schema serveur : HTTPS par defaut (Vague 1 audit 2026-07).
// Rollback HTTP : definir USE_HTTP_ENDPOINTS au build (-DUSE_HTTP_ENDPOINTS).
// Transport TLS : USE_HTTPS_ENDPOINTS (aligne WiFiClientSecure sur l'URL https://).
#if defined(USE_HTTP_ENDPOINTS)
#define MSP_SERVER_SCHEME "http://"
#else
#define MSP_SERVER_SCHEME "https://"
#endif

// --- Pins ---
#if defined(PINMAP_UNIVERSAL)
// Carte porteuse n3-universal (hardware/n3-universal) : nets partages entre les
// 3 firmwares — source de verite pinmap_universel_propose.json, garde anti-derive
// hardware/n3-universal/tools/. GPIO13 (gate +3V3_SW), OneWire (2) et I2C (21/22)
// sont inchanges ; le reste s'aligne sur les nets universels.
#define RELAIS 13        // net GATE (rail capteurs commute)
#define LUMINOSITEa 32   // net ADC_A (partage Humid1 n3pp)
#define LUMINOSITEb 33   // net ADC_B (partage Humid2 n3pp)
#define LUMINOSITEc 34   // net ADC_C (partage Humid3 n3pp)
#define LUMINOSITEd 35   // net ADC_D (partage Humid4 n3pp)
#define SERVOGD 26       // net SERVO1 (partage SERVO_GROS ffp5cs)
#define SERVOHB 27       // net SERVO2 (partage SERVO_PETITS ffp5cs)
#define DHTPININT 15     // net DHT_INT (commun aux 3 firmwares)
#define DHTTYPEINT DHT11
#define DHTPINEXT 5      // net US2 (partage ULTRASON_TANK ffp5cs) — legacy, preferer BME280 0x77
#define DHTTYPEEXT DHT11
#define HumiditeSol 36   // net ADC_E (partage Luminosite n3pp / LDR ffp5cs)
#define PLUIE 4          // net US1 (partage ULTRASON_AQUA ffp5cs)
#define pontdiv 39       // net ADC_VBAT (diviseur commute)
#else
#define RELAIS 13
#define LUMINOSITEa 33
#define LUMINOSITEb 34
#define LUMINOSITEc 35
#define LUMINOSITEd 39
#define SERVOGD 25
#define SERVOHB 14
#define DHTPININT 26
#define DHTTYPEINT DHT11
#define DHTPINEXT 15
#define DHTTYPEEXT DHT11
#define HumiditeSol 32
#define PLUIE 27
#define pontdiv N3_PONTDIV_PIN
#endif

// --- Servos ---
const int minAngleServoGD = 1;
const int maxAngleServoGD = 179;
const int minAngleServoHB = 40;
const int maxAngleServoHB = 145;

// --- Capteurs ---
const unsigned int oneWireBus = 2;

// --- Batterie / pont diviseur ---
// R1/R2 non definis ici pour eviter conflit avec struct N3BatteryConfig (n3_battery.h) ; msp_sensors utilise N3_BATTERY_R1/R2
const float ADC_MAX_VALUE = 4095.0;
const float V_REF = N3_BATTERY_VREF;
const float calibration = 0.06;
#define NUM_SAMPLES N3_BATTERY_NUM_SAMPLES

// --- Deep sleep ---
#define uS_TO_S_FACTOR N3_US_TO_S_FACTOR
#define TIME_TO_SLEEP FreqWakeUp

// --- Intervalles ---
const long intervalDatas = N3_DATA_INTERVAL_MS;

// --- OLED ---
#define SCREEN_WIDTH N3_OLED_WIDTH
#define SCREEN_HEIGHT N3_OLED_HEIGHT

// --- URLs serveur ---
// Source de verite unique : serverNamePostData / serverNameOutput / serverNameHeartbeat
// (definies dans main.cpp avec MSP_SERVER_SCHEME de msp_config.h).

// --- NTP ---
#define MSP_NTP_SERVER N3_NTP_SERVER
#define MSP_GMT_OFFSET_SEC N3_GMT_OFFSET
#define MSP_DAYLIGHT_OFFSET_SEC N3_DAYLIGHT_OFFSET

// --- Email ---
#define emailSubject "Information MSP1"
