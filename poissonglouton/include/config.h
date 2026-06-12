#pragma once

#include <Arduino.h>

// Version firmware
static constexpr const char* PGL_FIRMWARE_VERSION = "0.1.2";
static constexpr const char* PGL_SENSOR_NAME = "poissonglouton";
static constexpr const char* PGL_SENSOR_LOCATION = "n3-recyclage";

// Broches capteurs
static constexpr int PGL_IR_PIN = 4;          // RTC GPIO pour réveil ext0
static constexpr int PGL_US_TRIG_PIN = 6;
static constexpr int PGL_US_ECHO_PIN = 7;

// Broches audio DFPlayer (UART2)
static constexpr int PGL_DFPLAYER_RX = 18;    // ESP RX <- DF TX
static constexpr int PGL_DFPLAYER_TX = 17;    // ESP TX -> DF RX
static constexpr int PGL_DFPLAYER_BUSY = 16;  // optionnel

// Batterie (pont diviseur)
static constexpr int PGL_BATTERY_PIN = 2;
static constexpr uint32_t PGL_BATTERY_R1 = 2200;
static constexpr uint32_t PGL_BATTERY_R2 = 2200;
static constexpr float PGL_BATTERY_VREF = 3.30f;
static constexpr uint8_t PGL_BATTERY_SAMPLES = 8;

// Détection
static constexpr uint16_t PGL_ULTRASON_TRIGGER_CM = 25;
static constexpr uint16_t PGL_ULTRASON_MAX_VALID_CM = 120;
// 1500 -> 600 ms : l'US est désormais déclenché sur front (l'objet doit
// sortir du champ avant un nouveau comptage) et l'IR l'était déjà — le
// debounce global long ne protégeait plus que contre le double-comptage
// immédiat. 600 ms permet de compter des bouteilles rapprochées.
static constexpr uint32_t PGL_DEBOUNCE_MS = 600;
static constexpr uint32_t PGL_TANDEM_WINDOW_MS = 300;
// Filtre anti-faux-positifs US : nombre de polls consécutifs sous le seuil
// requis pour valider une présence (1 poll ~ 10-35 ms de boucle).
static constexpr uint8_t PGL_US_CONSECUTIVE_POLLS = 2;
static constexpr uint32_t PGL_IDLE_SLEEP_MS = 12000;

// Persistance NVS de la file d'événements (lazy) : écrire à chaque détection
// usait la flash et bloquait la boucle 50-200 ms. On persiste tous les
// N événements ou au plus tard T ms après le premier non persisté, et
// toujours avant le deep sleep.
static constexpr uint8_t PGL_PERSIST_EVERY_EVENTS = 4;
static constexpr uint32_t PGL_PERSIST_MAX_DELAY_MS = 30000;

// Réseau / synchro
static constexpr uint32_t PGL_UPLOAD_EVERY_MS = 20000;
static constexpr size_t PGL_BATCH_SIZE = 12;
static constexpr size_t PGL_FORCE_UPLOAD_QUEUE_SIZE = 24;
static constexpr uint32_t PGL_WIFI_TIMEOUT_MS = 5000;

// Énergie
static constexpr uint32_t PGL_TIMER_WAKEUP_S = 2;      // fallback si IR absent
// Timer adaptatif : la nuit (trafic nul) et sur batterie faible, on espace
// les réveils timer. L'EXT0 (IR) continue de réveiller instantanément.
// NB : detectIrAtBoot() ne sait pas distinguer un capteur IR réellement
// absent (pullup) — d'où des paliers conservateurs pour ne pas pénaliser
// une installation US-seul la journée.
static constexpr uint32_t PGL_TIMER_WAKEUP_LOWBATT_S = 10;   // batterie < seuil
static constexpr uint32_t PGL_TIMER_WAKEUP_NIGHT_S = 1800;   // 23h-6h : sieste 30 min
static constexpr uint8_t PGL_NIGHT_START_HOUR = 23;
static constexpr uint8_t PGL_NIGHT_END_HOUR = 6;
static constexpr int16_t PGL_LOWBATT_MILLIVOLT = 3500;
static constexpr uint32_t PGL_BACKLIGHT_TIMEOUT_MS = 20000;

// URL serveur
static constexpr const char* PGL_SERVER_POST_URL = "http://iot.olution.info/pgl/post-data";
static constexpr const char* PGL_SERVER_HEARTBEAT_URL = "http://iot.olution.info/pgl/heartbeat";

// Heartbeat serveur (supervision en ligne) — 0 pour désactiver
#ifndef PGL_ENABLE_SERVER_HEARTBEAT
#define PGL_ENABLE_SERVER_HEARTBEAT 1
#endif
static constexpr uint32_t PGL_HEARTBEAT_INTERVAL_MS = 120000;
