#pragma once

// ── Version firmware ────────────────────────────────────────────────
#define FIRMWARE_VERSION "2.0"

// ── Serveur commun ──────────────────────────────────────────────────
#define SERVER_NAME "iot.olution.info"
#define SERVER_PORT 80
#define BOUNDARY    "RandomNerdTutorials"

// ── Camera AI-Thinker (broches communes) ────────────────────────────
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

// ── LED statut ──────────────────────────────────────────────────────
#define LED_STATUS_PIN    33

// ── NTP ─────────────────────────────────────────────────────────────
#define NTP_SERVER "pool.ntp.org"
#define GMT_OFFSET 0
#define DST_OFFSET 3600

// ── Configuration par cible ─────────────────────────────────────────

#if defined(TARGET_MSP1)
    #define OTA_TARGET_KEY     "msp1"
    #define SERVER_PATH        "/msp1/msp1gallery/upload.php"
    #define OTA_METADATA_URL   "http://iot.olution.info/ota/cam/metadata.json"
    #define USE_DEEP_SLEEP     0
    #define USE_SD             0
    #define TIMER_INTERVAL_MS  1800000   // 30 min
    #define HOUR_START         6
    #define HOUR_END           20
    #define FRAMESIZE_DEFAULT  FRAMESIZE_SXGA
    #define JPEG_QUALITY       4
    #define XCLK_FREQ          20000000
    #define HTTP_CHUNK_SIZE    1024
    #define HTTP_TIMEOUT_MS    30000

#elif defined(TARGET_N3PP)
    #define OTA_TARGET_KEY     "n3pp"
    #define SERVER_PATH        "/n3ppgallery/upload.php"
    #define OTA_METADATA_URL   "http://iot.olution.info/ota/cam/metadata.json"
    #define USE_DEEP_SLEEP     1
    #define USE_SD             1
    #define SLEEP_DURATION_S   600       // 10 min deep sleep
    #define HOUR_START         6
    #define HOUR_END           22
    #define FRAMESIZE_DEFAULT  FRAMESIZE_SXGA
    #define JPEG_QUALITY       3
    #define XCLK_FREQ          5000000
    #define HTTP_CHUNK_SIZE    4096
    #define HTTP_TIMEOUT_MS    20000
    #define OTA_CHECK_EVERY_N  6         // verifier OTA toutes les 6 boots (~1h)

#elif defined(TARGET_FFP3)
    #define OTA_TARGET_KEY     "ffp3"
    #define SERVER_PATH        "/ffp3/ffp3gallery/upload.php"
    #define OTA_METADATA_URL   "http://iot.olution.info/ota/cam/metadata.json"
    #define USE_DEEP_SLEEP     0
    #define USE_SD             0
    #define TIMER_INTERVAL_MS  600000    // 10 min
    #define HOUR_START         6
    #define HOUR_END           22
    #define FRAMESIZE_DEFAULT  FRAMESIZE_SVGA
    #define JPEG_QUALITY       7
    #define XCLK_FREQ          20000000
    #define HTTP_CHUNK_SIZE    1024
    #define HTTP_TIMEOUT_MS    30000

#else
    #error "Definir TARGET_MSP1, TARGET_N3PP ou TARGET_FFP3 dans platformio.ini"
#endif
