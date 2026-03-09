#ifndef CONFIG_H
#define CONFIG_H

/* ========== Commun ========== */
#define FIRMWARE_VERSION "2.8"
#define SERVER_NAME     "iot.olution.info"
#define SERVER_PORT     80

/* NTP */
#define NTP_SERVER         "pool.ntp.org"
#define GMT_OFFSET_SEC     0
#define DAYLIGHT_OFFSET_SEC 3600

/* Créneau horaire (photos entre 6h et 22h) */
#define HOUR_START  6
#define HOUR_END    22

/* LED statut */
#define LED_GPIO 33

/* Pins caméra — ESP32-CAM AI Thinker, OV2640 */
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

/* OTA distant (metadata.json) — toutes cibles : vérif à chaque réveil */
#define OTA_METADATA_URL         "http://iot.olution.info/ota/cam/metadata.json"
#define OTA_CHECK_EVERY_N_BOOTS  1

/* WiFi */
#define WIFI_CONNECT_TIMEOUT_MS  5000
#define WIFI_DELAY_BETWEEN_MS    250
#define WIFI_PRE_SCAN_DELAY_MS   300
#define WIFI_SCAN_MAX            10
#define WIFI_RECONNECT_INTERVAL_MS 60000

/* HTTP upload — chunks */
#define UPLOAD_CHUNK_SIZE 4096

/* ========== Par cible ========== */
#if defined(TARGET_MSP1)
#  define SERVER_PATH        "/msp1gallery/upload.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      3
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        1

#elif defined(TARGET_N3PP)
#  define SERVER_PATH        "/n3ppgallery/upload.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      3
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        1

#elif defined(TARGET_FFP3)
#  define SERVER_PATH        "/ffp3/ffp3gallery/upload.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      3
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        1

#else
#  error "Un des TARGET_MSP1, TARGET_N3PP ou TARGET_FFP3 doit être défini (build_flags)."
#endif

#if USE_DEEP_SLEEP
#  define uS_TO_S_FACTOR 1000000
#endif

#endif /* CONFIG_H */
