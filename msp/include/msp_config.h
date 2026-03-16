/* MeteoStationPrototype (msp1) — Configuration
 * Constantes, pins, URLs serveur, version
 */
#pragma once

#include "credentials.h"
#include "n3_defaults.h"

#define FIRMWARE_VERSION "2.15"

// --- Pins ---
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

// --- Servos ---
const int minAngleServoGD = 1;
const int maxAngleServoGD = 179;
const int minAngleServoHB = 40;
const int maxAngleServoHB = 145;

// --- Capteurs ---
const unsigned int oneWireBus = 2;
const int numReadings = 10;

// --- Batterie / pont diviseur ---
#define R1 N3_BATTERY_R1
#define R2 N3_BATTERY_R2
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
#ifdef TEST_MODE
#define MSP_URL_POST_DATA "http://iot.olution.info/msp1-test/msp1datas/post-msp1-data.php"
#define MSP_URL_OUTPUT "http://iot.olution.info/msp1-test/msp1control/msp1-outputs-action.php?action=outputs_state&board=2"
#else
#define MSP_URL_POST_DATA "http://iot.olution.info/msp1/msp1datas/post-msp1-data.php"
#define MSP_URL_OUTPUT "http://iot.olution.info/msp1/msp1control/msp1-outputs-action.php?action=outputs_state&board=2"
#endif

// --- NTP ---
#define MSP_NTP_SERVER N3_NTP_SERVER
#define MSP_GMT_OFFSET_SEC N3_GMT_OFFSET
#define MSP_DAYLIGHT_OFFSET_SEC N3_DAYLIGHT_OFFSET

// --- Email ---
#define emailSubject "Information MSP1"
