#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <time.h>

#include "config.h"
#include "pgl_audio.h"
#include "pgl_counter.h"
#include "pgl_detection.h"
#include "pgl_display.h"
#include "pgl_event_journal.h"
#include "pgl_log.h"
#include "pgl_network.h"
#include "pgl_sd_storage.h"
#include "pgl_sleep.h"
#include "pgl_types.h"
#include "n3_battery.h"
#if PGL_ENABLE_OTA
#include "n3_ota.h"
#endif

#ifndef PGL_DEBUG_NO_SLEEP
#define PGL_DEBUG_NO_SLEEP 0
#endif

namespace {
PglDetection gDetection;
PglEventJournal gJournal;
PglCounter gCounter;
PglAudio gAudio;
PglDisplay gDisplay;
PglNetwork gNetwork;
[[maybe_unused]] PglSleep gSleep;

uint32_t gLastUploadMs = 0;
uint32_t gLastActivityMs = 0;
uint32_t gLastUiIdleMs = 0;
uint32_t gLastHeartbeatMs = 0;
uint32_t gLastServerHeartbeatMs = 0;
uint32_t gLastIdleWarnMs = 0;
// Etat de l'extinction du retroeclairage par inactivite (independant du deep sleep).
// Evite de redeclencher la transition a chaque tour de loop().
bool gBacklightDimmed = false;

RTC_DATA_ATTR uint32_t gBootCount = 0;
bool gEpochBackfillDone = false;

#if PGL_ENABLE_OTA
// Etat de lecture audio, alimente par le callback de notification : sert a
// differer une verification OTA (qui bloque la loop pendant le telechargement)
// tant qu'une piste joue, pour ne pas couper un remerciement en cours.
bool gAudioPlaying = false;
// Horodatage du dernier check OTA (millis). Initialise a 0 ; le 1er check est
// donc autorise des que WiFi + NTP sont prets (calque "check au boot" de
// msp/n3pp, mais sans deep sleep ici : noeud allume en continu).
uint32_t gLastOtaCheckMs = 0;
#endif

// Horloge: dernier epoch NTP valide persiste en NVS, pour amorcer une horloge
// provisoire au power-on a froid (avant la 1ere synchro NTP) et eviter les
// epoch=0. Namespace dedie "pgltime", cle "lastEpoch".
constexpr const char* kTimeNvsNamespace = "pgltime";
constexpr const char* kTimeNvsKey = "lastEpoch";
// Cadence d'ecriture NVS du lastKnownEpoch (usure flash) : au plus une ecriture
// toutes les 5 minutes tant que l'horloge est valide.
constexpr uint32_t kLastEpochSaveIntervalMs = 5UL * 60UL * 1000UL;
uint32_t gLastEpochSaveMs = 0;

void onAudioDisplayNotify(const char* reason, const char* mp3Path, bool started, void* userData) {
#if PGL_ENABLE_OTA
  gAudioPlaying = started;
#endif
  auto* display = static_cast<PglDisplay*>(userData);
  if (!display) return;
  if (started) {
    display->showAudioPlaying(reason, mp3Path);
  } else {
    display->showAudioIdle();
  }
}

void refreshDisplayWifi(PglDisplay& display, PglNetwork& network) {
  if (network.isWifiConnected()) {
    display.setWifiInfo(WiFi.SSID().c_str(), WL_CONNECTED, WiFi.RSSI());
  } else if (network.isWifiConnecting()) {
    display.setWifiConnecting();
  } else if (network.isWifiOffline()) {
    display.setWifiOffline();
  } else {
    display.setWifiSearching();
  }
}

void refreshDisplayUltrason(PglDisplay& display, PglDetection& detection) {
  display.setUltrasonDistance(detection.getUltrasonDistanceCm(), detection.hasUltrason());
}

void refreshDisplayServer(PglDisplay& display, PglNetwork& network, PglCounter& counter) {
  display.setServerStatus(network.getServerStatus(), counter.getPendingCount());
  PGL_LOG_V("Display srv: pending=%u journal=%lu nvs=%u",
            counter.getPendingCount(),
            static_cast<unsigned long>(counter.getJournalPendingCount()),
            counter.getNvsPendingCount());
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

static bool isNtpReady() {
  return time(nullptr) >= 1700000000;
}

static uint32_t getCurrentEpochSafe() {
  time_t now = time(nullptr);
  if (now < 1700000000) {
    return 0;
  }
  return static_cast<uint32_t>(now);
}

#if PGL_ENABLE_OTA
// Callbacks OTA (logs uniquement, calque msp/n3pp). Le telechargement, la verif
// sha256/signature et le redemarrage sur succes sont geres par n3OtaCheck().
static void otaOnUpdateStart(const char* currentVersion, const char* remoteVersion,
                             const char* firmwareUrl, void* userData) {
  (void)userData;
  PGL_LOG("OTA: MAJ disponible %s -> %s, telechargement depuis %s",
          currentVersion ? currentVersion : PGL_FIRMWARE_VERSION,
          remoteVersion ? remoteVersion : "?",
          firmwareUrl ? firmwareUrl : "?");
}

static void otaOnUpdateEnd(bool success, const char* details, void* userData) {
  (void)userData;
  if (success) {
    PGL_LOG("OTA: verifiee et flashee, redemarrage imminent");
  } else {
    PGL_LOG("OTA: pas de MAJ appliquee — %s", details ? details : "raison inconnue");
  }
}

// Verification OTA periodique (cadence PGL_OTA_CHECK_INTERVAL_MS, 2 h comme
// msp/n3pp). Conditions de securite : WiFi connecte, horloge NTP prete, et
// aucune lecture audio en cours (le telechargement bloque la loop). Sur succes,
// n3OtaCheck() redemarre la carte ; sinon le firmware reprend normalement.
static void maybeRunPeriodicOtaCheck() {
  if (gLastOtaCheckMs != 0 &&
      (millis() - gLastOtaCheckMs) < PGL_OTA_CHECK_INTERVAL_MS) {
    return;
  }
  if (WiFi.status() != WL_CONNECTED) {
    return;  // pas de log spam : ressaye au prochain tour quand le WiFi revient
  }
  if (!isNtpReady()) {
    PGL_LOG_V("OTA: differee — NTP non synchronise");
    return;
  }
  if (gAudioPlaying) {
    PGL_LOG_V("OTA: differee — lecture audio en cours");
    return;
  }

  static const N3OtaConfig otaConfig = {
      PGL_OTA_METADATA_URL,
      PGL_FIRMWARE_VERSION,
      -1,        // pas de LED de feedback dediee
      nullptr,   // objet metadata unique {version,url,sha256,signature}
      otaOnUpdateStart,
      otaOnUpdateEnd,
      nullptr,
      nullptr,   // pas de callback de progression (logs n3_ota suffisent)
  };

  PGL_LOG("OTA: verification periodique (locale %s, %s)",
          PGL_FIRMWARE_VERSION, PGL_OTA_METADATA_URL);
  n3OtaCheck(otaConfig);  // redemarre si une MAJ verifiee est appliquee
  gLastOtaCheckMs = millis();
}
#endif  // PGL_ENABLE_OTA

// Lit le dernier epoch NTP valide persiste en NVS (0 si absent/invalide).
static uint32_t loadLastKnownEpoch() {
  Preferences p;
  if (!p.begin(kTimeNvsNamespace, true)) {
    return 0;
  }
  const uint32_t epoch = p.getUInt(kTimeNvsKey, 0);
  p.end();
  return epoch >= 1700000000 ? epoch : 0;
}

// Persiste l'epoch courant en NVS (seulement s'il avance, pour limiter l'usure).
static void saveLastKnownEpoch(uint32_t epoch) {
  if (epoch < 1700000000) return;
  Preferences p;
  if (!p.begin(kTimeNvsNamespace, false)) return;
  const uint32_t prev = p.getUInt(kTimeNvsKey, 0);
  if (epoch > prev) {
    p.putUInt(kTimeNvsKey, epoch);
  }
  p.end();
}

// Amorce l'horloge systeme avec le dernier epoch NVS si l'horloge est invalide.
// A appeler au boot apres configTime, avant tout horodatage d'evenement.
// L'horloge RTC interne persiste a travers le deep sleep : ce cas ne survient
// donc qu'au power-on a froid. NTP corrigera ensuite vers le haut.
static void seedClockFromNvs() {
  if (time(nullptr) >= 1700000000) {
    return;  // horloge deja valide (RTC conservee en deep sleep)
  }
  const uint32_t lastEpoch = loadLastKnownEpoch();
  if (lastEpoch == 0) {
    PGL_LOG("Horloge: aucun epoch NVS, demarrage sans amorcage (NTP en attente)");
    return;
  }
  struct timeval tv = {};
  tv.tv_sec = static_cast<time_t>(lastEpoch);
  tv.tv_usec = 0;
  settimeofday(&tv, nullptr);
  PGL_LOG("Horloge: amorcage provisoire epoch=%lu (NVS, NTP en attente)",
          static_cast<unsigned long>(lastEpoch));
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

#if PGL_ENABLE_SLEEP && !PGL_DEBUG_NO_SLEEP
// Timer de réveil adaptatif. La base depend du mode de detection :
//  - IR present (EXT0) : l'IR reveille instantanement a la detection, le timer
//    ne sert qu'au housekeeping -> base LONGUE (PGL_TIMER_WAKEUP_IR_S, 300 s).
//  - pas d'IR (US seul / aucun capteur) : la detection se fait par
//    echantillonnage en etant eveille -> base courte (PGL_TIMER_WAKEUP_S, 30 s).
// Surcharges (nuit 23h-6h, batterie faible) : appliquees uniquement si elles
// ALLONGENT le timer courant, pour ne jamais raccourcir une base deja longue
// (ex. la base IR de 300 s ne doit pas retomber a 10 s sur batterie faible).
static uint32_t computeSleepTimerS(bool irPresent) {
  uint32_t timerS = irPresent ? PGL_TIMER_WAKEUP_IR_S : PGL_TIMER_WAKEUP_S;

  const int16_t battMv = readBatteryMilliVolt();
#if PGL_BATTERY_ADC_ENABLED
  if (battMv > 500 && battMv < PGL_LOWBATT_MILLIVOLT &&
      timerS < PGL_TIMER_WAKEUP_LOWBATT_S) {
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
#endif  // PGL_ENABLE_SLEEP && !PGL_DEBUG_NO_SLEEP

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
  if (!isNtpReady()) {
    PGL_LOG_V("Upload: differe — NTP non synchronise");
    return;
  }

  PGL_LOG("Upload: pending=%u journal=%lu nvs=%u",
          gCounter.getPendingCount(),
          static_cast<unsigned long>(gCounter.getJournalPendingCount()),
          gCounter.getNvsPendingCount());

  const uint32_t budgetStart = millis();
  bool anySuccess = false;

  if (gCounter.hasJournalPending()) {
    while (gCounter.getJournalPendingCount() > 0) {
      if ((millis() - budgetStart) >= PGL_CATCHUP_BUDGET_MS) {
        PGL_LOG("Upload SD: budget temps epuise (%u ms), reprise au prochain cycle",
                static_cast<unsigned int>(PGL_CATCHUP_BUDGET_MS));
        break;
      }

      PglStoredEvent batch[PGL_JOURNAL_BATCH_SIZE] = {};
      uint32_t firstId = 0;
      uint32_t lastId = 0;
      const size_t batchCount = gCounter.peekJournalBatch(
          batch, PGL_JOURNAL_BATCH_SIZE, firstId, lastId);
      if (batchCount == 0) break;

      PGL_LOG("Upload SD: lot=%u evt | firstId=%lu lastId=%lu",
              static_cast<unsigned int>(batchCount),
              static_cast<unsigned long>(firstId),
              static_cast<unsigned long>(lastId));

      const PglUploadResult res = gNetwork.uploadBatch(
          batch, batchCount, gCounter.getTotalCount(), gCounter.getTodayCount());
      refreshDisplayServer(gDisplay, gNetwork, gCounter);

      if (res.ok) {
        anySuccess = true;
        const uint32_t ackId = (res.lastAckedEventId != 0) ? res.lastAckedEventId : lastId;
        PGL_LOG("Upload SD: OK — ack id=%lu", static_cast<unsigned long>(ackId));
        gCounter.commitJournalAck(ackId, batch, batchCount);
        gDisplay.setCounter(gCounter.getTotalCount(), gCounter.getTodayCount());
      } else {
        PGL_LOG("Upload SD: ECHEC — conservation des evenements (pending=%u)",
                gCounter.getPendingCount());
        break;
      }
    }
  }

  if (gCounter.getNvsPendingCount() > 0) {
    PglStoredEvent batch[PGL_BATCH_SIZE] = {};
    const size_t batchCount = gCounter.peekBatch(batch, PGL_BATCH_SIZE);
    if (batchCount > 0) {
      const PglUploadResult res = gNetwork.uploadBatch(
          batch, batchCount, gCounter.getTotalCount(), gCounter.getTodayCount());
      refreshDisplayServer(gDisplay, gNetwork, gCounter);
      if (res.ok) {
        anySuccess = true;
        if (res.lastAckedEventId != 0) {
          gCounter.popBatchByAckId(res.lastAckedEventId, batch, batchCount);
          PGL_LOG("Upload NVS: OK — ack id=%lu",
                  static_cast<unsigned long>(res.lastAckedEventId));
        } else {
          gCounter.popBatch(batchCount);
          PGL_LOG("Upload NVS: OK, %u evenement(s) acquittes",
                  static_cast<unsigned int>(batchCount));
        }
      } else {
        PGL_LOG("Upload NVS: ECHEC, evenements conserves (pending=%u)",
                gCounter.getPendingCount());
      }
    }
  }

#if PGL_ENABLE_SERVER_HEARTBEAT
  if (anySuccess) {
    if (gNetwork.sendHeartbeat(gBootCount)) {
      gLastServerHeartbeatMs = millis();
    }
    refreshDisplayServer(gDisplay, gNetwork, gCounter);
  }
#else
  (void)anySuccess;
#endif

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

  // Reveil par EXT0/timer = sortie de deep sleep ; UNDEFINED = vrai power-on.
  const bool coldBoot = (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_UNDEFINED);

#if PGL_ENABLE_OTA
  // Aligne la partition de boot sur la partition courante (evite que flash USB
  // et OTA ne se trompent de partition). A faire tot, comme msp/n3pp.
  n3OtaSyncBootPartition();
#endif

  PGL_LOG("Init reseau...");
  gNetwork.begin();
  gNetwork.startBackgroundWifi();
  PGL_LOG_V("WiFi mode STA, status=%s", pglWifiStatusName(WiFi.status()));

  PGL_LOG("Init carte SD...");
#if !PGL_HEADLESS
  pglSdStorageBegin();
#endif

  PGL_LOG("Init journal SD offline...");
  gJournal.begin();

  PGL_LOG("Init compteur NVS...");
  gCounter.begin(&gJournal);

  PGL_LOG("Init affichage...");
  gDisplay.begin();
  gDisplay.setCounter(gCounter.getTotalCount(), gCounter.getTodayCount());
#if !PGL_HEADLESS
  gDisplay.setWifiSearching();
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
  // Jingle de demarrage uniquement au vrai power-on : sinon il serait rejoue
  // a chaque sortie de deep sleep (timer/IR).
  if (coldBoot) {
    gAudio.playStartup();
  } else {
    PGL_LOG_V("Audio: reveil deep sleep — jingle de demarrage ignore");
  }

  const int16_t battMv = readBatteryMilliVolt();
#if PGL_BATTERY_ADC_ENABLED
  PGL_LOG("Batterie: %d mV (pin %d)", battMv, PGL_BATTERY_PIN);
#else
  PGL_LOG("Batterie: ADC desactive (GPIO2 = I2S_LRCK sur JC4827W543)");
#endif

  setenv("TZ", "Africa/Casablanca", 1);
  tzset();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  PGL_LOG("NTP: pool.ntp.org TZ=Africa/Casablanca");

  // Amorcage horloge provisoire depuis NVS (cas power-on a froid avant NTP) :
  // donne un epoch plausible a getCurrentEpochSafe() / dayKey des le boot.
  seedClockFromNvs();

  gLastActivityMs = millis();
  gLastUiIdleMs = millis();
  gLastHeartbeatMs = millis();
  gLastServerHeartbeatMs = 0;
  PGL_LOG("Setup termine, entree loop");
  pglLogMemory();
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
    // Detection = activite : rallumer le retroeclairage s'il avait ete eteint
    // par inactivite (no-op en headless ou si deja allume).
    if (gBacklightDimmed) {
      gDisplay.wakeBacklight();
      gBacklightDimmed = false;
      PGL_LOG_V("Backlight ON (detection apres inactivite)");
    }
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

  if (gNetwork.pollWifi(PGL_WIFI_LOOP_BUDGET_MS)) {
    refreshDisplayWifi(gDisplay, gNetwork);
  }

  if (isNtpReady() && !gEpochBackfillDone) {
    gCounter.fixInvalidEpochs(getCurrentEpochSafe());
    gEpochBackfillDone = true;
  }

  tryUploadBatch();
  gCounter.flushIfDue();

  if ((millis() - gLastHeartbeatMs) > 5000) {
    const uint32_t idleMs = millis() - gLastActivityMs;
    refreshDisplayWifi(gDisplay, gNetwork);
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
    // Persistance parcimonieuse du dernier epoch NTP valide (horloge provisoire
    // au prochain power-on a froid). Throttle a kLastEpochSaveIntervalMs.
    if (isNtpReady() &&
        (gLastEpochSaveMs == 0 ||
         (millis() - gLastEpochSaveMs) >= kLastEpochSaveIntervalMs)) {
      saveLastKnownEpoch(getCurrentEpochSafe());
      gLastEpochSaveMs = millis();
    }

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

#if PGL_ENABLE_OTA
  // Verification OTA periodique (2 h) : telechargement + verif sha256/signature
  // ECDSA + redemarrage geres par n3OtaCheck() (lib partagee n3_common).
  maybeRunPeriodicOtaCheck();
#endif

  // Extinction du retroeclairage par inactivite, independante du deep sleep.
  // Utile surtout quand la veille est desactivee (PGL_ENABLE_SLEEP=0, cas alpha) :
  // la borne reste allumee en continu, donc on coupe la dalle apres
  // PGL_BACKLIGHT_TIMEOUT_MS (20 s) d'inactivite pour limiter conso et usure.
  // Quand la veille est active, le chemin de veille ci-dessous coupe deja le
  // retroeclairage avant de dormir a PGL_IDLE_SLEEP_MS (12 s), donc ce seuil de
  // 20 s n'est en pratique jamais atteint avant le sommeil : pas de conflit.
  // On reutilise gLastActivityMs (reinitialise a chaque detection) sans timer
  // divergent, et gBacklightDimmed garantit une seule transition.
  if (!gBacklightDimmed && (millis() - gLastActivityMs) > PGL_BACKLIGHT_TIMEOUT_MS) {
    gDisplay.sleepBacklight();
    gBacklightDimmed = true;
    PGL_LOG_V("Backlight OFF (inactivite %lums >= %u)",
              static_cast<unsigned long>(millis() - gLastActivityMs),
              static_cast<unsigned int>(PGL_BACKLIGHT_TIMEOUT_MS));
  }

  if ((millis() - gLastActivityMs) > PGL_IDLE_SLEEP_MS) {
#if !PGL_ENABLE_SLEEP || PGL_DEBUG_NO_SLEEP
    if ((millis() - gLastIdleWarnMs) > 10000) {
      PGL_LOG("Inactivite %lums — veille desactivee (PGL_ENABLE_SLEEP=%d, PGL_DEBUG_NO_SLEEP=%d)",
              static_cast<unsigned long>(millis() - gLastActivityMs),
              PGL_ENABLE_SLEEP, PGL_DEBUG_NO_SLEEP);
      gLastIdleWarnMs = millis();
    }
    // NB : on ne reinitialise plus gLastActivityMs ici. Le throttle du log
    // d'inactivite repose sur gLastIdleWarnMs ; laisser l'inactivite continuer
    // a s'accumuler est necessaire pour atteindre le seuil d'extinction du
    // retroeclairage (PGL_BACKLIGHT_TIMEOUT_MS) quand la veille est desactivee.
#else
    PGL_LOG("Inactivite %lums >= %u — preparation veille",
            static_cast<unsigned long>(millis() - gLastActivityMs),
            static_cast<unsigned int>(PGL_IDLE_SLEEP_MS));
    tryUploadBatch();
    gCounter.flush();  // la RAM est perdue en deep sleep : persister la file
    saveLastKnownEpoch(getCurrentEpochSafe());  // borne basse pour le prochain power-on
    gDisplay.sleepBacklight();
    PGL_LOG("Backlight OFF");
    const bool useIrWakeup = gDetection.hasIr();
    const uint32_t timerS = computeSleepTimerS(useIrWakeup);
    gSleep.configure(useIrWakeup, timerS);
    PGL_LOG("Veille: IR_wakeup=%d timer=%lus", useIrWakeup ? 1 : 0,
            static_cast<unsigned long>(timerS));
    gSleep.start();
#endif
  }

  delay(10);
}
