#include <Arduino.h>
#include <ESP32Time.h>
#include <WiFi.h>
#include <time.h>

#include "config.h"
#include "pgl_audio.h"
#include "pgl_counter.h"
#include "pgl_detection.h"
#include "pgl_display.h"
#include "pgl_log.h"
#include "pgl_network.h"
#include "pgl_sleep.h"
#include "pgl_types.h"
#include "n3_battery.h"

#ifndef PGL_DEBUG_NO_SLEEP
#define PGL_DEBUG_NO_SLEEP 0
#endif

namespace {
PglDetection gDetection;
PglCounter gCounter;
PglAudio gAudio;
PglDisplay gDisplay;
PglNetwork gNetwork;
PglSleep gSleep;

uint32_t gLastUploadMs = 0;
uint32_t gLastActivityMs = 0;
uint32_t gLastUiIdleMs = 0;
uint32_t gLastHeartbeatMs = 0;
uint32_t gLastServerHeartbeatMs = 0;
uint32_t gLastIdleWarnMs = 0;

RTC_DATA_ATTR uint32_t gBootCount = 0;

void onAudioDisplayNotify(const char* reason, const char* mp3Path, bool started, void* userData) {
  auto* display = static_cast<PglDisplay*>(userData);
  if (!display) return;
  if (started) {
    display->showAudioPlaying(reason, mp3Path);
  } else {
    display->showAudioIdle();
  }
}

void refreshDisplayWifi(PglDisplay& display) {
  if (WiFi.status() == WL_CONNECTED) {
    display.setWifiInfo(WiFi.SSID().c_str(), WL_CONNECTED, WiFi.RSSI());
  } else {
    display.setWifiInfo(nullptr, WiFi.status(), -127);
  }
}

void refreshDisplayUltrason(PglDisplay& display, PglDetection& detection) {
  display.setUltrasonDistance(detection.getUltrasonDistanceCm(), detection.hasUltrason());
}

void refreshDisplayServer(PglDisplay& display, PglNetwork& network, PglCounter& counter) {
  display.setServerStatus(network.getServerStatus(), counter.getPendingCount());
}

void logIrStatus(const PglDetection& detection) {
  if (detection.hasIr()) {
    const bool obstacle = detection.readIrObstacle();
    PGL_LOG("IR GPIO%d: capteur OK — %s (niveau=%d)",
            PGL_IR_PIN,
            obstacle ? "OBSTACLE" : "libre",
            obstacle ? 0 : 1);
  } else {
    PGL_LOG("IR GPIO%d: capteur ABSENT (non detecte au boot)", PGL_IR_PIN);
  }
}

void refreshHardwareStatus(PglDisplay& display, PglDetection& detection) {
#if !PGL_HEADLESS
  display.setHardwareStatus(display.isReady(), detection.hasIr(), detection.readIrObstacle());
#endif
}
}

static uint32_t getCurrentEpochSafe() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return static_cast<uint32_t>(millis() / 1000UL);
  }
  return static_cast<uint32_t>(now);
}

static int16_t readBatteryMilliVolt() {
#if PGL_BATTERY_ADC_ENABLED
  N3BatteryConfig cfg = {
      static_cast<uint8_t>(PGL_BATTERY_PIN),
      PGL_BATTERY_R1,
      PGL_BATTERY_R2,
      PGL_BATTERY_VREF,
      PGL_BATTERY_SAMPLES,
  };
  N3BatteryResult res = n3BatteryRead(cfg, nullptr, nullptr, nullptr);
  return static_cast<int16_t>(res.batteryVoltage * 1000.0f);
#else
  return 0;
#endif
}

// Timer de réveil adaptatif : la nuit (23h-6h, heure NTP valide uniquement)
// et sur batterie faible, on espace les réveils timer. L'EXT0 (IR) continue
// de réveiller instantanément sur détection.
static uint32_t computeSleepTimerS() {
  uint32_t timerS = PGL_TIMER_WAKEUP_S;

  const int16_t battMv = readBatteryMilliVolt();
#if PGL_BATTERY_ADC_ENABLED
  if (battMv > 500 && battMv < PGL_LOWBATT_MILLIVOLT) {
    timerS = PGL_TIMER_WAKEUP_LOWBATT_S;
    PGL_LOG("Sleep adaptatif: batterie faible (%d mV) -> timer %lus",
            battMv, static_cast<unsigned long>(timerS));
  }
#endif

  const time_t now = time(nullptr);
  if (now >= 1700000000) {  // heure NTP valide
    struct tm tmNow = {};
    if (localtime_r(&now, &tmNow)) {
      const int h = tmNow.tm_hour;
      const bool night = (h >= PGL_NIGHT_START_HOUR) || (h < PGL_NIGHT_END_HOUR);
      if (night && timerS < PGL_TIMER_WAKEUP_NIGHT_S) {
        timerS = PGL_TIMER_WAKEUP_NIGHT_S;
        PGL_LOG("Sleep adaptatif: nuit (%dh) -> timer %lus",
                h, static_cast<unsigned long>(timerS));
      }
    }
  }
  return timerS;
}

static bool shouldUploadNow() {
  if (gCounter.getPendingCount() >= PGL_FORCE_UPLOAD_QUEUE_SIZE) return true;
  return (millis() - gLastUploadMs) >= PGL_UPLOAD_EVERY_MS;
}

static void tryUploadBatch() {
  if (!shouldUploadNow()) return;
  if (gCounter.getPendingCount() == 0) {
    gLastUploadMs = millis();
    return;
  }

  PGL_LOG("Upload: tentative lot (pending=%u)", gCounter.getPendingCount());
  PglStoredEvent batch[PGL_BATCH_SIZE] = {};
  const size_t batchCount = gCounter.peekBatch(batch, PGL_BATCH_SIZE);
  if (batchCount == 0) return;

  const bool uploaded = gNetwork.uploadBatch(batch, batchCount, gCounter.getTotalCount(), gCounter.getTodayCount());
  refreshDisplayServer(gDisplay, gNetwork, gCounter);
  if (uploaded) {
    PGL_LOG("Upload: OK, %u evenement(s) acquittes", static_cast<unsigned int>(batchCount));
    gCounter.popBatch(batchCount);
#if PGL_ENABLE_SERVER_HEARTBEAT
    if (gNetwork.sendHeartbeat(gBootCount)) {
      gLastServerHeartbeatMs = millis();
    }
    refreshDisplayServer(gDisplay, gNetwork, gCounter);
#endif
  } else {
    PGL_LOG("Upload: ECHEC, evenements conserves (pending=%u)", gCounter.getPendingCount());
  }
  gLastUploadMs = millis();
}

void setup() {
  Serial.begin(115200);
  const uint32_t serialWaitStart = millis();
  while (!Serial && (millis() - serialWaitStart) < 2000) {
    delay(10);
  }
  delay(300);

  pglLogBootBanner();
  pglLogWakeupCause();
  pglLogMemory();

  ++gBootCount;
  PGL_LOG("Boot count: %lu", static_cast<unsigned long>(gBootCount));

  PGL_LOG("Init reseau...");
  WiFi.mode(WIFI_STA);
  gNetwork.begin();
  PGL_LOG_V("WiFi mode STA, status=%s", pglWifiStatusName(WiFi.status()));

  PGL_LOG("Init compteur NVS...");
  gCounter.begin();

  PGL_LOG("Init affichage...");
  gDisplay.begin();
  gDisplay.setCounter(gCounter.getTotalCount(), gCounter.getTodayCount());
#if !PGL_HEADLESS
  PGL_LOG("Display: etat apres init — %s", gDisplay.isReady() ? "FONCTIONNEL" : "ECHEC");
#endif

  PGL_LOG("Init detection capteurs...");
  gDetection.begin();
  logIrStatus(gDetection);
  refreshHardwareStatus(gDisplay, gDetection);
  PGL_LOG("Affichage pret");

  PGL_LOG("Mode capteur actif: %u (0=aucun 1=IR 2=US 3=tandem)",
          static_cast<unsigned int>(gDetection.getActiveMode()));

  PGL_LOG("Init audio I2S (JC4827W543 speak)...");
  gAudio.setNotifyCallback(onAudioDisplayNotify, &gDisplay);
  gAudio.begin();
  gAudio.playStartup();

  const int16_t battMv = readBatteryMilliVolt();
#if PGL_BATTERY_ADC_ENABLED
  PGL_LOG("Batterie: %d mV (pin %d)", battMv, PGL_BATTERY_PIN);
#else
  PGL_LOG("Batterie: ADC desactive (GPIO2 = I2S_LRCK sur JC4827W543)");
#endif

  configTime(3600, 0, "pool.ntp.org");
  PGL_LOG("NTP: pool.ntp.org (offset +3600s)");

  gLastActivityMs = millis();
  gLastUiIdleMs = millis();
  gLastHeartbeatMs = millis();
  gLastServerHeartbeatMs = 0;
  PGL_LOG("Setup termine, entree loop");
  pglLogMemory();

#if !PGL_HEADLESS
  PGL_LOG("WiFi: connexion pour affichage reseau...");
  gNetwork.connectWifi();
  refreshDisplayWifi(gDisplay);
  refreshDisplayServer(gDisplay, gNetwork, gCounter);
#elif PGL_VERBOSE_LOG
  PGL_LOG("WiFi: test connexion apres init (scan plus fiable)...");
  gNetwork.connectWifi();
#endif
}

void loop() {
  gDisplay.update();
  gCounter.resetDailyIfNeeded(getCurrentEpochSafe());

  const PglDetectionEvent event = gDetection.poll();
  refreshDisplayUltrason(gDisplay, gDetection);
  refreshHardwareStatus(gDisplay, gDetection);
  if (event.detected) {
    PglStoredEvent stored = {};
    stored.epoch = getCurrentEpochSafe();
    stored.countDelta = 1;
    stored.sensorMode = static_cast<uint8_t>(event.mode);
    stored.tandemValidated = event.tandemValidated ? 1 : 0;
    stored.batteryMilliVolt = readBatteryMilliVolt();
    stored.rssi = static_cast<int16_t>(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127);

    gCounter.addEvent(stored);
    gDisplay.onBottleCount(gCounter.getTotalCount(), gCounter.getTodayCount());
    refreshDisplayServer(gDisplay, gNetwork, gCounter);
    gAudio.playThanks();
    gLastActivityMs = millis();
    gLastUiIdleMs = millis();
    PGL_LOG("DETECTION +1 total=%lu today=%lu pending=%u mode=%u tandem=%u batt=%dmV",
            static_cast<unsigned long>(gCounter.getTotalCount()),
            static_cast<unsigned long>(gCounter.getTodayCount()),
            gCounter.getPendingCount(),
            static_cast<unsigned int>(stored.sensorMode),
            static_cast<unsigned int>(stored.tandemValidated),
            static_cast<int>(stored.batteryMilliVolt));
  } else if ((millis() - gLastUiIdleMs) > 2500) {
    gDisplay.showIdle();
    gLastUiIdleMs = millis();
  }

  tryUploadBatch();
  gCounter.flushIfDue();

  if ((millis() - gLastHeartbeatMs) > 5000) {
    const uint32_t idleMs = millis() - gLastActivityMs;
    refreshDisplayWifi(gDisplay);
    refreshDisplayServer(gDisplay, gNetwork, gCounter);
    logIrStatus(gDetection);
#if !PGL_HEADLESS
    PGL_LOG("Display: %s", gDisplay.isReady() ? "fonctionnel" : "ECHEC");
#endif
    PGL_LOG("heartbeat total=%lu today=%lu pending=%u wifi=%s rssi=%d heap=%lu idle=%lums",
            static_cast<unsigned long>(gCounter.getTotalCount()),
            static_cast<unsigned long>(gCounter.getTodayCount()),
            gCounter.getPendingCount(),
            pglWifiStatusName(WiFi.status()),
            static_cast<int>(WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : -127),
            static_cast<unsigned long>(ESP.getFreeHeap()),
            static_cast<unsigned long>(idleMs));
#if PGL_VERBOSE_LOG
    if (WiFi.status() == WL_CONNECTED) {
      PGL_LOG_V("heartbeat wifi: ssid=%s ip=%s ch=%d gw=%s",
                WiFi.SSID().c_str(),
                WiFi.localIP().toString().c_str(),
                WiFi.channel(),
                WiFi.gatewayIP().toString().c_str());
    }
#endif
    gLastHeartbeatMs = millis();
  }

  gAudio.poll();

#if PGL_ENABLE_SERVER_HEARTBEAT
  if ((millis() - gLastServerHeartbeatMs) >= PGL_HEARTBEAT_INTERVAL_MS) {
    if (gNetwork.sendHeartbeat(gBootCount)) {
      gLastServerHeartbeatMs = millis();
    }
    refreshDisplayServer(gDisplay, gNetwork, gCounter);
  }
#endif

  if ((millis() - gLastActivityMs) > PGL_IDLE_SLEEP_MS) {
#if PGL_DEBUG_NO_SLEEP
    if ((millis() - gLastIdleWarnMs) > 10000) {
      PGL_LOG("[TEST] Inactivite %lums — veille ignoree (PGL_DEBUG_NO_SLEEP)",
              static_cast<unsigned long>(millis() - gLastActivityMs));
      gLastIdleWarnMs = millis();
    }
    gLastActivityMs = millis();
#else
    PGL_LOG("Inactivite %lums >= %u — preparation veille",
            static_cast<unsigned long>(millis() - gLastActivityMs),
            static_cast<unsigned int>(PGL_IDLE_SLEEP_MS));
    tryUploadBatch();
    gCounter.flush();  // la RAM est perdue en deep sleep : persister la file
    gDisplay.sleepBacklight();
    PGL_LOG("Backlight OFF");
    const bool useIrWakeup = gDetection.hasIr();
    const uint32_t timerS = computeSleepTimerS();
    gSleep.configure(useIrWakeup, timerS);
    PGL_LOG("Veille: IR_wakeup=%d timer=%lus", useIrWakeup ? 1 : 0,
            static_cast<unsigned long>(timerS));
    gSleep.start();
#endif
  }

  delay(10);
}
