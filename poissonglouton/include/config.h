#pragma once



#include <Arduino.h>



// Version firmware

static constexpr const char* PGL_FIRMWARE_VERSION = "0.2.6";

static constexpr const char* PGL_SENSOR_NAME = "poissonglouton";

static constexpr const char* PGL_SENSOR_LOCATION = "n3-recyclage";



#ifndef PGL_HEADLESS

#define PGL_HEADLESS 0

#endif



// Veille profonde (deep sleep). Desactivee par defaut POUR L'INSTANT (version
// alpha) : le couple timer/idle actuel rend le deep sleep peu rentable et
// perturbe l'ecran + le son a chaque reveil. A reactiver (PGL_ENABLE_SLEEP=1)
// une fois le couple timer/idle re-calibre pour les noeuds batterie/headless.
#ifndef PGL_ENABLE_SLEEP

#define PGL_ENABLE_SLEEP 0

#endif



// Broches capteurs — IR par defaut GPIO7 (RTC/ext0) ; surcharge via -DPGL_IR_PIN=N
#ifndef PGL_IR_PIN
#define PGL_IR_PIN 7
#endif
static constexpr int PGL_US_PIN = 6;   // HC-SR04 trig/echo sur une seule broche



// Volume audio (0…21, ESP32-audioI2S) — display ; inutilise en headless
static constexpr uint8_t PGL_AUDIO_VOLUME = 15;

// Audio I2S JC4827W543 (ampli NS4168, sortie speak) — display uniquement

#if !PGL_HEADLESS

static constexpr int PGL_I2S_BCLK = 42;

static constexpr int PGL_I2S_LRCK = 2;

static constexpr int PGL_I2S_DOUT = 41;

static constexpr int PGL_SD_CS = 10;

static constexpr int PGL_SD_SCK = 12;

static constexpr int PGL_SD_MOSI = 11;

static constexpr int PGL_SD_MISO = 13;

static constexpr uint8_t PGL_I2S_VOLUME = PGL_AUDIO_VOLUME;

#endif



// Nombre max de pistes dans mp3/ (001.mp3, 002.mp3, …) pour le scan au boot

static constexpr uint16_t PGL_AUDIO_MAX_TRACKS = 99;



// Batterie (pont diviseur) — display : GPIO2 = I2S_LRCK, ADC désactivé

#if PGL_HEADLESS

static constexpr bool PGL_BATTERY_ADC_ENABLED = true;

static constexpr int PGL_BATTERY_PIN = 2;

#else

static constexpr bool PGL_BATTERY_ADC_ENABLED = false;

static constexpr int PGL_BATTERY_PIN = -1;

#endif

static constexpr uint32_t PGL_BATTERY_R1 = 2200;

static constexpr uint32_t PGL_BATTERY_R2 = 2200;

static constexpr float PGL_BATTERY_VREF = 3.30f;

static constexpr uint8_t PGL_BATTERY_SAMPLES = 8;



// Détection

static constexpr uint16_t PGL_ULTRASON_TRIGGER_CM = 25;

static constexpr uint16_t PGL_ULTRASON_MAX_VALID_CM = 120;

static constexpr uint32_t PGL_DEBOUNCE_MS = 600;

static constexpr uint32_t PGL_TANDEM_WINDOW_MS = 300;

static constexpr uint8_t PGL_US_CONSECUTIVE_POLLS = 2;

// Inactivite avant de declencher la veille. Avec un timer de reveil long en
// mode IR (cf. PGL_TIMER_WAKEUP_IR_S), 12 s d'eveil par cycle de housekeeping
// donne un duty-cycle largement domine par le sommeil (~12 s eveille / 300 s
// dormant ≈ 4 %). On garde donc 12 s : c'est confortable pour finir un upload /
// heartbeat avant de dormir, sans peser sur la conso une fois la veille active.
static constexpr uint32_t PGL_IDLE_SLEEP_MS = 12000;



// Persistance NVS de la file d'événements (lazy)

static constexpr uint8_t PGL_PERSIST_EVERY_EVENTS = 4;

static constexpr uint32_t PGL_PERSIST_MAX_DELAY_MS = 30000;

// Intervalle min entre deux reconciliations totaux qui scannent le journal SD.
// Evite de relire tout le journal a chaque tour de loop() quand des evenements
// sont en attente (mode offline). La detection de changement de jour reste, elle,
// verifiee a chaque tour.
static constexpr uint32_t PGL_RECONCILE_INTERVAL_MS = 30000;



// Réseau / synchro

static constexpr uint32_t PGL_UPLOAD_EVERY_MS = 20000;

static constexpr size_t PGL_BATCH_SIZE = 12;

static constexpr size_t PGL_FORCE_UPLOAD_QUEUE_SIZE = 24;

static constexpr uint32_t PGL_WIFI_TIMEOUT_MS = 10000;

static constexpr uint32_t PGL_WIFI_LOOP_BUDGET_MS = 40;

static constexpr uint32_t PGL_WIFI_UPLOAD_BUDGET_MS = 2000;

static constexpr uint32_t PGL_WIFI_RETRY_INTERVAL_MS = 60000;



// Énergie

// Timer de reveil quand le capteur IR (EXT0) est present : l'IR reveille
// INSTANTANEMENT a la detection, donc le timer ne sert qu'a la maintenance
// periodique (vidage de la file en attente, heartbeat). On le veut LONG pour
// maximiser le sommeil. 300 s = aligne sur l'ordre de grandeur du housekeeping
// (PGL_UPLOAD_EVERY_MS=20 s / PGL_HEARTBEAT_INTERVAL_MS=120 s) : un reveil
// toutes les 5 min suffit a evacuer la file et battre le coeur sans rien rater
// d'une detection (celle-ci passe par EXT0, pas par le timer).
static constexpr uint32_t PGL_TIMER_WAKEUP_IR_S = 300;

// Timer de reveil quand il n'y a PAS d'IR (ultrason seul, ou aucun capteur) :
// la detection ne peut se faire qu'en etant eveille, donc le timer sert a
// echantillonner l'environnement. ATTENTION : la detection ultrason en deep
// sleep est intrinsequement par ECHANTILLONNAGE et donc LOSSY — une bouteille
// dont le passage est plus court que cet intervalle peut etre manquee. 2 s
// etait trop agressif (boot complet + >=12 s d'eveil a chaque cycle => ~86 %
// du temps eveille, deep sleep inutile). 30 s est un compromis raisonnable
// entre conso et probabilite de capter un passage ; a re-evaluer si l'on
// observe des manques sur un noeud US-only.
static constexpr uint32_t PGL_TIMER_WAKEUP_S = 30;

// Sur batterie faible, on espace les reveils pour preserver l'energie. La
// valeur n'est appliquee que si elle ALLONGE le timer courant (cf.
// computeSleepTimerS) : en mode IR la base de 300 s est deja plus longue, donc
// l'effet ne joue que pour la base US courte (30 s -> 600 s).
static constexpr uint32_t PGL_TIMER_WAKEUP_LOWBATT_S = 600;

static constexpr uint32_t PGL_TIMER_WAKEUP_NIGHT_S = 1800;

static constexpr uint8_t PGL_NIGHT_START_HOUR = 23;

static constexpr uint8_t PGL_NIGHT_END_HOUR = 6;

static constexpr int16_t PGL_LOWBATT_MILLIVOLT = 3500;

static constexpr uint32_t PGL_BACKLIGHT_TIMEOUT_MS = 20000;



// Journal SD (offline sync)
// Fichier de journal sur la carte SD (append-only, records binaires + CRC16)
static constexpr const char* PGL_JOURNAL_PATH = "/pgl_journal.bin";
// Fichier temporaire utilisé lors de la compaction du journal
static constexpr const char* PGL_JOURNAL_COMPACT_TMP = "/pgl_journal_tmp.bin";
static constexpr const char* PGL_JOURNAL_BACKUP_PATH = "/pgl_journal.bak";
// Taille d'un lot maximal envoyé pendant le rattrapage offline
static constexpr size_t PGL_JOURNAL_BATCH_SIZE = 20;
// Budget temps max (ms) pour la phase de rattrapage lors d'un cycle d'upload
static constexpr uint32_t PGL_CATCHUP_BUDGET_MS = 8000;
// Seuil d'octets acquittés avant de déclencher une compaction (1 Mo)
static constexpr uint32_t PGL_JOURNAL_COMPACT_THRESHOLD = 1024UL * 1024UL;

// URL serveur
//
// Securite TLS : les requetes HTTPS de la lib partagee n3_data utilisent par
// defaut setInsecure() (chiffre mais SANS validation du certificat serveur).
// L'epinglage du root CA (pinning, protection MITM) est disponible en OPT-IN :
// poser le fichier poissonglouton/include/n3_data_ca_cert.h (cf. le template
// shared/n3_data/n3_data_ca_cert.h.example) definissant N3_DATA_CA_CERT_PEM.
// Detecte via __has_include dans n3_data.cpp ; DESACTIVE par defaut (aucun
// changement de comportement tant que ce fichier n'est pas pose). Le vrai CA
// est gitignore et ne doit jamais etre committe.

static constexpr const char* PGL_SERVER_POST_URL = "https://iot.olution.info/pgl/post-data";

static constexpr const char* PGL_SERVER_HEARTBEAT_URL = "https://iot.olution.info/pgl/heartbeat";



#ifndef PGL_ENABLE_SERVER_HEARTBEAT

#define PGL_ENABLE_SERVER_HEARTBEAT 1

#endif

static constexpr uint32_t PGL_HEARTBEAT_INTERVAL_MS = 120000;



// Mise a jour OTA (calque msp/n3pp : lib partagee n3_common/n3_ota, verif
// sha256 + signature ECDSA P-256, cle publique embarquee n3_ota_pubkey.h).
// L'URL des metadonnees suit le schema OTA unifie /ota/<cible>/metadata.json
// (cf. docs/WIFI_OTA_REFERENCE.md) ; la cible serveur de poissonglouton est
// "pgl" (cf. firmwares.manifest.json, serveurFolder "pgl").
#ifndef PGL_ENABLE_OTA
#define PGL_ENABLE_OTA 1
#endif

static constexpr const char* PGL_OTA_METADATA_URL =
    "http://iot.olution.info/ota/pgl/metadata.json";

// Cadence de verification OTA : 2 h, alignee sur msp/n3pp
// (OTA_PERIODIC_INTERVAL_SECONDS = 2 h). Independant du heartbeat applicatif
// (PGL_HEARTBEAT_INTERVAL_MS) pour ne pas marteler le serveur OTA.
static constexpr uint32_t PGL_OTA_CHECK_INTERVAL_MS = 2UL * 60UL * 60UL * 1000UL;


