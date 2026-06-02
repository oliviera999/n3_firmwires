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
static constexpr uint32_t PGL_DEBOUNCE_MS = 1500;
static constexpr uint32_t PGL_TANDEM_WINDOW_MS = 300;
static constexpr uint32_t PGL_IDLE_SLEEP_MS = 12000;

// Réseau / synchro
static constexpr uint32_t PGL_UPLOAD_EVERY_MS = 20000;
static constexpr size_t PGL_BATCH_SIZE = 12;
static constexpr size_t PGL_FORCE_UPLOAD_QUEUE_SIZE = 24;
static constexpr uint32_t PGL_WIFI_TIMEOUT_MS = 5000;

// Énergie
static constexpr uint32_t PGL_TIMER_WAKEUP_S = 2;      // fallback si IR absent
static constexpr uint32_t PGL_BACKLIGHT_TIMEOUT_MS = 20000;

// URL serveur
static constexpr const char* PGL_SERVER_POST_URL = "http://iot.olution.info/pgl/post-data";
static constexpr const char* PGL_SERVER_HEARTBEAT_URL = "http://iot.olution.info/pgl/heartbeat";

// Heartbeat serveur (supervision en ligne) — 0 pour désactiver
#ifndef PGL_ENABLE_SERVER_HEARTBEAT
#define PGL_ENABLE_SERVER_HEARTBEAT 1
#endif
static constexpr uint32_t PGL_HEARTBEAT_INTERVAL_MS = 120000;
