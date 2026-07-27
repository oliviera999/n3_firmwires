#ifndef CONFIG_H
#define CONFIG_H

#include "credentials.h"
#ifndef API_SIG_SECRET
#define API_SIG_SECRET ""
#endif

/* HMAC device (en-têtes X-Sig-*) : désactivé par défaut — auth = API_KEY seule.
 * Opt-in : #define CAM_DEVICE_HMAC 1 (et API_SIG_SECRET non vide dans credentials.h).
 * Côté serveur : fallback api_key si HMAC absent/invalide (sauf GALLERY_HMAC_STRICT). */
#ifndef CAM_DEVICE_HMAC
#define CAM_DEVICE_HMAC 0
#endif
#if CAM_DEVICE_HMAC
#  define CAM_DEVICE_SIG_SECRET API_SIG_SECRET
#else
#  define CAM_DEVICE_SIG_SECRET ""
#endif

/* ========== Commun ========== */
#define FIRMWARE_VERSION "2.74"
#define SERVER_NAME     "iot.olution.info"

/* Canal galerie / upload : HTTPS par défaut (USE_HTTPS_ENDPOINTS dans platformio.ini).
 * WiFiClientSecure + setInsecure() ; OTA reste HTTP (/ota/cam/...). Rollback HTTP :
 * retirer -DUSE_HTTPS_ENDPOINTS de cam-base dans platformio.ini. Cf. docs/HTTPS_MIGRATION.md.
 *
 * ⚠ Caveat heap (U1, à valider on-device) : le handshake TLS + les buffers
 * WiFiClientSecure consomment plusieurs dizaines de Ko de RAM. Sur un module
 * ESP32-CAM SANS PSRAM, garder HTTPS activé par défaut peut réduire la marge de
 * heap pendant la capture/upload d'une image et provoquer des échecs d'alloc
 * intermittents. On NE change PAS le défaut ici, mais un noeud non-PSAM doit être
 * validé en conditions réelles (et rollback HTTP si la marge est trop juste).
 */
#if defined(USE_HTTPS_ENDPOINTS)
#  define SERVER_SCHEME  "https://"
#else
#  define SERVER_SCHEME  "http://"
#endif

/* NTP — heure locale Maroc (UTC+1). Le creneau HOUR_START/HOUR_END et les horodatages
 * X-Captured-At sont interpretes en heure murale marocaine.
 *
 * ⚠ La newlib de l'ESP32 n'embarque PAS la base IANA : un nom comme "Africa/Casablanca" est
 * interprete comme un fuseau a offset nul (=> horloge UTC, decalage 1 h). On utilise donc :
 *   - GMT_OFFSET_SEC = 3600 (UTC+1) pour la synchro NTP ;
 *   - NTP_TZ_STRING au format POSIX "<+01>-1" (offset 1 h a l'est, sans DST) applique a localtime.
 * Le Maroc reste a UTC+1 la majeure partie de l'annee (l'exception Ramadan n'est pas exprimable en
 * POSIX et est ignoree volontairement). Cf. camera_time.cpp (A3, audit 2026-07-05). */
#define NTP_SERVER           "pool.ntp.org"
#define GMT_OFFSET_SEC       3600
#define DAYLIGHT_OFFSET_SEC  0
#define NTP_TZ_STRING        "<+01>-1"
#define NTP_SYNC_TIMEOUT_MS  5000

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

/* Warm-up / convergence AEC caméra (A8, audit 2026-06/2026-07). Valeurs raccourcies validées en
 * lumière stable. ⚠ En cas de dégradation d'exposition en transition (aube/crépuscule), revenir
 * aux marges historiques : CAM_WARMUP_FRAME_COUNT=3, CAM_WARMUP_FRAME_DELAY_MS=1000 (3×1000 ms) et
 * CAM_AEC_SETTLE_MS=10000. Ces durées sont ici pour ajustement terrain sans toucher au code. */
#define CAM_WARMUP_FRAME_COUNT   2      /* trames jetées après init (historique : 3) */
#define CAM_WARMUP_FRAME_DELAY_MS 150   /* pause entre trames de warm-up (historique : 1000) */
#define CAM_AEC_PRELOCK_MS       500    /* fenêtre AEC/AGC figés avant réactivation auto */
#define CAM_AEC_SETTLE_MS        3000   /* convergence AEC/AWB en lumière stable (historique : 10000) */

/* Caméra : seuils SPIRAM pour tenter SXGA + 2 buffers JPEG. Sinon CIF + 1 buffer d’emblée ;
   si l’init haute résolution échoue quand même, repli automatique CIF (voir main.cpp). */
#define CAM_SPIRAM_MIN_FREE_BYTES    (1536 * 1024)
#define CAM_SPIRAM_MIN_LARGEST_BLOCK (1024 * 1024)

/* Séquence matérielle OV2640 avant SCCB / esp_camera_init (nappe lente, alim faible). */
#define CAM_PWDN_POWERDOWN_MS   10
#define CAM_PWDN_WAKEUP_MS     120
#define CAM_XCLK_SETTLE_MS     150
#define CAM_DEINIT_SETTLE_MS   150
#define CAM_SCCB_RETRY_COUNT     4
#define CAM_SCCB_RETRY_BASE_MS  50
#define CAM_SCCB_CLOCK_HZ   100000
#define CAM_SCCB_CLOCK_SLOW_HZ 50000

/* OTA distant (metadata.json) — toutes cibles : vérif périodique toutes les 2h.
 * HTTPS par défaut (A5, audit 2026-07-05) : ferme la fenêtre de downgrade MITM sur les métadonnées
 * (le binaire reste vérifié sha256 + ECDSA P-256 côté n3_ota). Rollback HTTP documenté : définir
 * -DOTA_METADATA_HTTP_ROLLBACK dans platformio.ini pour repasser en http:// clair. */
#if defined(OTA_METADATA_HTTP_ROLLBACK)
#  define OTA_METADATA_URL         "http://iot.olution.info/ota/cam/metadata.json"
#else
#  define OTA_METADATA_URL         "https://iot.olution.info/ota/cam/metadata.json"
#endif

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
#define WIFI_PRE_SCAN_DELAY_MS   500
#define WIFI_SCAN_MAX            10

/* HTTP upload — chunks */
#define UPLOAD_CHUNK_SIZE 4096
/* Monitoring local */
#define MONITORING_HEAP_WARN_BYTES 60000
/* Drapeaux de diagnostic série (0/1) — PAS des niveaux de log. Renommés CAM_DIAG_*
 * pour lever la collision de noms avec les macros/niveaux de shared/n3_log
 * (N3_LOGx, N3_LOG_LEVEL, N3LOG_*). CAM_DIAG_DEBUG pilote Serial.setDebugOutput ;
 * CAM_DIAG_VERBOSE active le dump complet du corps HTTP distant. */
#define CAM_DIAG_DEBUG           0
#define CAM_DIAG_VERBOSE         1

/* Controle distant camera */
#define REMOTE_SLEEP_MIN_SECONDS 10
#define REMOTE_SLEEP_MAX_SECONDS 86400

/* Attente réponse HTTP après POST (dérogation conventions-firmwares 5s : traitement serveur image) */
#define HTTP_RESPONSE_TIMEOUT_MS 15000
#define UPLOAD_CONNECT_RETRIES   2
#define UPLOAD_RETRY_DELAY_MS    1000

/* Synchronisation hors-ligne du backlog SD (stratégie hybride, cf. camera_sync) */
#define SYNC_MAX_UPLOADS_PER_WAKE 10   /* drain incrémental : photos max envoyées par réveil */
#define SYNC_FULL_DRAIN_THRESHOLD 25   /* backlog au-delà duquel on vide tout ce réveil (rattrapage) */
/* Borne mémoire du scan backlog (A1/M2, audit 2026-07-05) : chaque SyncEntry ≈ 76 octets. On charge
   au plus SYNC_MAX_BACKLOG_SCAN entrées (les plus anciennes) sur le tas interne. Abaissé de 256 à 64
   (≈4,8 Ko au lieu de ~19,5 Ko) : bien au-dessus du réel drainable ~16/réveil (cf. plafond ci-dessous),
   limite la pression/fragmentation DRAM pendant WiFi+TLS. */
#define SYNC_MAX_BACKLOG_SCAN     64
/* Intervalle min entre deux POST upload.php : aligné sur GALLERY_UPLOAD_RATE_LIMIT_SECONDS (10 s/IP) serveur. */
#define SYNC_UPLOAD_MIN_INTERVAL_MS 11000
#define SYNC_RATE_LIMIT_RETRIES     2    /* tentatives supplementaires apres HTTP 429 */
#define SYNC_DRAIN_MAX_DURATION_MS  180000  /* budget temps sync par reveil (3 min) */
/* Plafond réel de photos envoyables par réveil : budget temps / intervalle mini (≈16). `planned` y est
   borné (A1) pour que « vidage complet » reste atteignable et ne remonte plus `aborted` à chaque réveil. */
#define SYNC_DRAIN_MAX_UPLOADS_PER_WAKE (SYNC_DRAIN_MAX_DURATION_MS / SYNC_UPLOAD_MIN_INTERVAL_MS)

/* Série UART0 : si le moniteur PC « ne reçoit rien », mettre 3000–5000 (ms) pour laisser le temps
   d’ouvrir le port après réveil deep sleep ; en prod laisser 0. Voir README firmwires (ESP32-CAM). */
#define SERIAL_BOOT_PAUSE_MS 0

/* ========== Par cible ========== */
#if defined(TARGET_MSP1)
#  define SERVER_PATH        "/msp1gallery/upload.php"
#  define REMOTE_BOARD_ID    6
#  define REMOTE_OUTPUTS_STATE_URL SERVER_SCHEME "iot.olution.info/msp1gallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=6"
#  define REMOTE_VERSION_POST_URL  SERVER_SCHEME "iot.olution.info/msp1gallery/post-uploadphotoserver-version.php"
#  define SYNC_START_URL    SERVER_SCHEME "iot.olution.info/msp1gallery/uploadphotoserver-sync-start.php"
#  define SYNC_FINISH_URL   SERVER_SCHEME "iot.olution.info/msp1gallery/uploadphotoserver-sync-finish.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      600
#  define CAM_XCLK_HZ        5000000

#elif defined(TARGET_N3PP)
#  define SERVER_PATH        "/n3ppgallery/upload.php"
#  define REMOTE_BOARD_ID    7
#  define REMOTE_OUTPUTS_STATE_URL SERVER_SCHEME "iot.olution.info/n3ppgallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=7"
#  define REMOTE_VERSION_POST_URL  SERVER_SCHEME "iot.olution.info/n3ppgallery/post-uploadphotoserver-version.php"
#  define SYNC_START_URL    SERVER_SCHEME "iot.olution.info/n3ppgallery/uploadphotoserver-sync-start.php"
#  define SYNC_FINISH_URL   SERVER_SCHEME "iot.olution.info/n3ppgallery/uploadphotoserver-sync-finish.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      600
#  define CAM_XCLK_HZ        5000000

#elif defined(TARGET_FFP3)
/* Chemins canoniques sans prefixe /ffp3/ : les GET /ffp3/* declenchent un 301 Apache
   (.htaccess o2switch) alors que les POST sont rewrites en interne — le firmware
   GET outputs_state exige HTTP 200 + JSON (cf. serveur/.htaccess). */
#  define SERVER_PATH        "/ffp3gallery/upload.php"
#  define REMOTE_BOARD_ID    5
#  define REMOTE_OUTPUTS_STATE_URL SERVER_SCHEME "iot.olution.info/ffp3gallery/uploadphotoserver-outputs-action.php?action=outputs_state&board=5"
#  define REMOTE_VERSION_POST_URL  SERVER_SCHEME "iot.olution.info/ffp3gallery/post-uploadphotoserver-version.php"
#  define SYNC_START_URL    SERVER_SCHEME "iot.olution.info/ffp3gallery/uploadphotoserver-sync-start.php"
#  define SYNC_FINISH_URL   SERVER_SCHEME "iot.olution.info/ffp3gallery/uploadphotoserver-sync-finish.php"
#  define USE_DEEP_SLEEP     1
#  define USE_SD             1
#  define TIME_TO_SLEEP      600
#  define CAM_XCLK_HZ        5000000

#else
#  error "Un des TARGET_MSP1, TARGET_N3PP ou TARGET_FFP3 doit être défini (build_flags)."
#endif

#if USE_DEEP_SLEEP
#  define uS_TO_S_FACTOR 1000000
#endif

#endif /* CONFIG_H */
