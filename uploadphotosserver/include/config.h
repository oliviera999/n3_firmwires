#ifndef CONFIG_H
#define CONFIG_H

/* ========== Commun ========== */
#define FIRMWARE_VERSION "2.35"
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

/* Caméra : seuils SPIRAM pour tenter SXGA + 2 buffers JPEG. Sinon CIF + 1 buffer d’emblée ;
   si l’init haute résolution échoue quand même, repli automatique CIF (voir main.cpp). */
#define CAM_SPIRAM_MIN_FREE_BYTES    (1536 * 1024)
#define CAM_SPIRAM_MIN_LARGEST_BLOCK (1024 * 1024)

/* OTA distant (metadata.json) — toutes cibles : vérif périodique toutes les 2h */
#define OTA_METADATA_URL         "http://iot.olution.info/ota/cam/metadata.json"

/* Notifications mail */
#define MAIL_NOTIFICATIONS_ENABLED 1
#define MAIL_SUBJECT_MAX_LEN      128
#define MAIL_BODY_MAX_LEN         1200
#define MAIL_EXTRA_MAX_LEN        384
#define MAIL_PENDING_EVENING      0x01
#define MAIL_PENDING_MORNING      0x02

/* WiFi */
#define WIFI_CONNECT_TIMEOUT_MS  5000
#define WIFI_DELAY_BETWEEN_MS    250
#define WIFI_PRE_SCAN_DELAY_MS   300
#define WIFI_SCAN_MAX            10
#define WIFI_RECONNECT_INTERVAL_MS 60000

/* HTTP upload — chunks */
#define UPLOAD_CHUNK_SIZE 4096
/* Monitoring local */
#define MONITORING_HEAP_WARN_BYTES 60000

/* Controle distant camera */
#define REMOTE_SLEEP_MIN_SECONDS 10
#define REMOTE_SLEEP_MAX_SECONDS 86400

/* Attente réponse HTTP après POST (dérogation conventions-firmwares 5s : traitement serveur image) */
#define HTTP_RESPONSE_TIMEOUT_MS 15000
#define UPLOAD_CONNECT_RETRIES   2
#define UPLOAD_RETRY_DELAY_MS    1000

/* Série UART0 : si le moniteur PC « ne reçoit rien », mettre 3000–5000 (ms) pour laisser le temps
   d’ouvrir le port après réveil deep sleep ; en prod laisser 0. Voir README firmwires (ESP32-CAM). */
#define SERIAL_BOOT_PAUSE_MS 4000

/* ========== Par cible ========== */
#if defined(TARGET_MSP1)
#  define SERVER_PATH        "/msp1gallery/upload.php"
#  define REMOTE_BOARD_ID    6
#  define REMOTE_OUTPUTS_STATE_URL "http://iot.olution.info/msp1gallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=6"
#  define REMOTE_VERSION_POST_URL  "http://iot.olution.info/msp1gallery/post-uploadphotoserver-version.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      15
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        4

#elif defined(TARGET_N3PP)
#  define SERVER_PATH        "/n3ppgallery/upload.php"
#  define REMOTE_BOARD_ID    7
#  define REMOTE_OUTPUTS_STATE_URL "http://iot.olution.info/n3ppgallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=7"
#  define REMOTE_VERSION_POST_URL  "http://iot.olution.info/n3ppgallery/post-uploadphotoserver-version.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      15
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        4

#elif defined(TARGET_FFP3)
#  define SERVER_PATH        "/ffp3/ffp3gallery/upload.php"
#  define REMOTE_BOARD_ID    5
#  define REMOTE_OUTPUTS_STATE_URL "http://iot.olution.info/ffp3/ffp3gallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=5"
#  define REMOTE_VERSION_POST_URL  "http://iot.olution.info/ffp3/ffp3gallery/post-uploadphotoserver-version.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      15
#  define CAM_XCLK_HZ        5000000
#  define EEPROM_SIZE        4

#else
#  error "Un des TARGET_MSP1, TARGET_N3PP ou TARGET_FFP3 doit être défini (build_flags)."
#endif

#if USE_DEEP_SLEEP
#  define uS_TO_S_FACTOR 1000000
#endif

#endif /* CONFIG_H */
