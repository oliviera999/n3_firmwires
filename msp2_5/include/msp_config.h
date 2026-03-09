/* MeteoStationPrototype (msp1) — Configuration
 * Constantes, pins, URLs serveur, version
 */
#pragma once

#define FIRMWARE_VERSION "2.11"

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
#define pontdiv 36

// --- Servos ---
const int minAngleServoGD = 1;
const int maxAngleServoGD = 179;
const int minAngleServoHB = 40;
const int maxAngleServoHB = 145;

// --- Capteurs ---
const unsigned int oneWireBus = 2;
const int numReadings = 10;

// --- Batterie / pont diviseur ---
#define R1 2200
#define R2 2180
const float ADC_MAX_VALUE = 4095.0;
const float V_REF = 3.33;
const float calibration = 0.06;
#define NUM_SAMPLES 10

// --- Deep sleep ---
#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP FreqWakeUp

// --- Intervalles ---
const long intervalDatas = 120000;

// --- OLED ---
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

// --- URLs serveur ---
#ifdef TEST_MODE
#define MSP_URL_POST_DATA "http://iot.olution.info/msp1-test/msp1datas/post-msp1-data.php"
#define MSP_URL_OUTPUT "http://iot.olution.info/msp1-test/msp1control/msp1-outputs-action.php?action=outputs_state&board=2"
#else
#define MSP_URL_POST_DATA "http://iot.olution.info/msp1/msp1datas/post-msp1-data.php"
#define MSP_URL_OUTPUT "http://iot.olution.info/msp1/msp1control/msp1-outputs-action.php?action=outputs_state&board=2"
#endif

// --- NTP ---
#define MSP_NTP_SERVER "pool.ntp.org"
#define MSP_GMT_OFFSET_SEC 3600
#define MSP_DAYLIGHT_OFFSET_SEC 3600

// --- Email ---
#define emailSubject "Information MSP1"
