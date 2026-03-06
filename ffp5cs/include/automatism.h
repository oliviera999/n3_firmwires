#pragma once
#include <cstring>
#include <atomic>
#include "system_sensors.h"
#include "system_actuators.h"
#include "web_client.h"
#include "display_view.h"
#include "power.h"
#include "mailer.h"
#include "config.h"
#include "config_manager.h"
#include "automatism/automatism_feeding_schedule.h"
#include "automatism/automatism_sync.h"
#include "automatism/automatism_sleep.h"
#include "task_monitor.h"
#include <esp_sleep.h>
#include <ArduinoJson.h>

// Contexte d'exécution passé aux méthodes internes (handleRefill, handleAlerts, updateDisplayInternal)
struct AutomatismRuntimeContext {
  SensorReadings readings;
  uint32_t nowMs;
  AutomatismRuntimeContext() : nowMs(0) {}
  AutomatismRuntimeContext(const SensorReadings& r, uint32_t now) : readings(r), nowMs(now) {}
};

class Automatism {
 public:
  Automatism(SystemSensors& sensors, SystemActuators& acts, 
             WebClient& web, DisplayView& disp, 
             PowerManager& power, Mailer& mail, ConfigManager& config);
  void begin();
  void update();               // collecte interne des capteurs
  void update(const SensorReadings& r); // usage dans tâche dédiée
  void updateDisplay();        // mise à jour de l'affichage OLED (tâche dédiée)
  // Mise à jour affichage avec readings déjà lus (évite double lecture capteurs, ex. depuis automationTask)
  void updateDisplayWithReadings(const SensorReadings& r);

  // Getters pour accéder aux états depuis l'extérieur
  bool isPumpAquaLocked() const { return _config.getPompeAquaLocked(); }
  bool isTankPumpLocked() const { return tankPumpLocked; }
  time_t getCurrentTime() { return _power.getCurrentEpoch(); }
  void getCurrentTimeString(char* buffer, size_t bufferSize) {
    _power.getCurrentTimeString(buffer, bufferSize);
  }
  uint32_t getPumpStartTime() const { return _pumpStartMs; }  // Pour SystemActuators
  bool isTankPumpRunning() const { return _pumpStartMs > 0; }
  bool fetchRemoteState(ArduinoJson::JsonDocument& doc);
  // Traite un doc déjà récupéré (normalise, enqueue sauvegarde NVS). Utilisé par netTask au boot.
  bool processFetchedRemoteConfig(ArduinoJson::JsonDocument& doc);
  /// Draine la file de sauvegarde NVS différée (à appeler depuis automation task uniquement).
  void processDeferredRemoteVarsSave();
  /// Met à jour l’état de bord nourrissage distant depuis un doc (chemin fallback = même état que chemin principal).

  // --- Accesseurs exposés pour le serveur Web local ---
  // v11.172: Source de vérité = AutomatismSync (_network)
  bool isEmailEnabled() const { return _network.isEmailEnabled(); }
  void toggleEmailNotifications();
  const char* getEmailAddress() const { return _network.getEmailAddress(); }
  uint16_t getFeedBigDur() const { return tempsGros; }
  uint16_t getFeedSmallDur() const { return tempsPetits; }
  uint8_t getBouffeMatin() const { return bouffeMatin; }
  uint8_t getBouffeMidi() const { return bouffeMidi; }
  uint8_t getBouffeSoir() const { return bouffeSoir; }
  uint16_t getTempsGros() const { return tempsGros; }
  uint16_t getTempsPetits() const { return tempsPetits; }
  uint32_t getRefillDurationSec() const { return refillDurationMs / 1000UL; }
  uint16_t getLimFlood() const { return _network.getLimFlood(); }
  const char* getBouffePetitsFlag() const { return bouffePetits; }
  const char* getBouffeGrosFlag() const { return bouffeGros; }
  uint16_t getAqThresholdCm() const { return _network.getAqThresholdCm(); }
  uint16_t getTankThresholdCm() const { return _network.getTankThresholdCm(); }
  float getHeaterThresholdC() const { return _network.getHeaterThresholdC(); }
  void setBouffePetitsFlag(const char* val) {
    strncpy(bouffePetits, val ? val : "0", sizeof(bouffePetits) - 1);
    bouffePetits[sizeof(bouffePetits) - 1] = '\0';
  }
  void setBouffeGrosFlag(const char* val) {
    strncpy(bouffeGros, val ? val : "0", sizeof(bouffeGros) - 1);
    bouffeGros[sizeof(bouffeGros) - 1] = '\0';
  }
  int computeDiffMaree(uint16_t currentAqua);
  bool isFeedingInProgress() const { return _currentFeedingPhase != FeedingPhase::NONE; }
  uint32_t getCountdownEndMs() const { return _countdownEnd; }
  uint16_t getFreqWakeSec() const { return _network.getFreqWakeSec(); }
  int16_t getTideTriggerCm() const { return tideTriggerCm; }

  // Déclenche le clignotement de l'icône "courrier" sur l'OLED
  void triggerMailBlink() { armMailBlink(); }
  void armMailBlink(uint32_t durationMs = 3000) { mailBlinkUntil = millis() + durationMs; }
  // Notifier une activité sur le serveur web local (consultation, action, etc.)
  // Cette activité doit maintenir forceWakeUp à true tant qu'elle est récente.
  void notifyLocalWebActivity();
  // Commandes manuelles de la pompe réservoir via le serveur local
  void startTankPumpManual();
  void stopTankPumpManual();
  // Commandes manuelles de la pompe aquarium via le serveur local
  void startAquaPumpManualLocal();
  void stopAquaPumpManualLocal();
  // Commandes manuelles du chauffage via le serveur local
  void startHeaterManualLocal();
  void stopHeaterManualLocal();
  // Commandes manuelles de la lumière via le serveur local
  void startLightManualLocal();
  void stopLightManualLocal();
  
  // Force Wakeup (toggle manuel)
  void toggleForceWakeup();
  bool getForceWakeUp() const { return forceWakeUp; }
  bool isRemoteSendEnabled() const { return _config.isRemoteSendEnabled(); }
  void waitForNetworkReady();

  // Veille légère : couper aqua/chauffage/lumière avant sleep, restaurer au réveil (snapshot NVS)
  void prepareActuatorsForSleep(SystemActuators& acts);
  void restoreActuatorsAfterWake(SystemActuators& acts);

  // Observabilité sync POST (exposé dans /api/status)
  uint32_t getSyncPostOkCount() const { return _network.getPostOkCount(); }
  uint32_t getSyncPostFailCount() const { return _network.getPostFailCount(); }
  uint32_t getSyncLastPostDurationMs() const { return _network.getLastPostDurationMs(); }
  unsigned long getLastSendMs() const { return _network.getLastSendMs(); }
  int getLastDataSkipReason() const { return _network.getLastDataSkipReason(); }
  int8_t getSendState() const { return _network.getSendState(); }

  // Méthodes publiques pour le serveur web
  bool sendFullUpdate(const SensorReadings& readings, const char* extraPairs = nullptr);
  void manualFeedSmall();
  void manualFeedBig();
  /// Notifie le sync après nourrissage distant déclenché par GPIOParser (ack, reset, email)
  void notifyRemoteFeedExecuted(bool isSmall) { _network.onRemoteFeedExecuted(isSmall, *this); }
  /// Marque le créneau horaire courant (matin/midi/soir) comme déjà nourri (évite auto après feed distant).
  void markCurrentFeedingSlotAsDone();
  size_t createFeedingMessage(char* buffer,
                              size_t bufferSize,
                              const char* type,
                              uint16_t bigDur,
                              uint16_t smallDur);
  // Applique des variables de configuration depuis un document JSON local (NVS)
  void applyConfigFromJson(const ArduinoJson::JsonDocument& doc);
  
  // Accessor pour modules (permet l'accès contrôlé à _sensors)
  SensorReadings readSensors() const { return _sensors.read(); }
  
  // Méthodes pour envoi email (exposées pour AutomatismSleep et AutomatismSync)
  bool sendSleepMail(const char* reason, uint32_t sleepDurationSeconds, const SensorReadings& readings) {
    const char* to = _network.getEmailAddress();
    return _mailer.sendSleepMail(reason, sleepDurationSeconds, readings,
                                 (to && strlen(to) > 0) ? to : EmailConfig::DEFAULT_RECIPIENT);
  }
  bool sendWakeMail(const char* reason, uint32_t actualSleepSeconds, const SensorReadings& readings) {
    const char* to = _network.getEmailAddress();
    return _mailer.sendWakeMail(reason, actualSleepSeconds, readings,
                                (to && strlen(to) > 0) ? to : EmailConfig::DEFAULT_RECIPIENT);
  }
  bool sendEmail(const char* subject, const char* message, const char* toName, const char* toEmail) {
    return _mailer.send(subject, message, toName, toEmail);
  }
  
  // v11.59: Indicateurs de mode pour OLED (A=Automatique, M=Manuel)
  bool isFeedingInManualMode() const;
  bool isRefillingInManualMode() const;
  
  // Méthodes pour mettre à jour les variables depuis le serveur distant
  // v11.172: Délègue à AutomatismSync (source de vérité)
  void setRefillDurationMs(uint32_t durationMs) { refillDurationMs = durationMs; }
  void setAqThresholdCm(uint16_t threshold) { _network.setAqThresholdCm(threshold); }
  void setTankThresholdCm(uint16_t threshold) { _network.setTankThresholdCm(threshold); }
  void setLimFlood(uint16_t lim) { _network.setLimFlood(lim); }
  void setHeaterThresholdC(float threshold) { _network.setHeaterThresholdC(threshold); }
  void setTempsGros(uint16_t duration) { tempsGros = duration; }
  void setTempsPetits(uint16_t duration) { tempsPetits = duration; }
  void setBouffeMatin(uint8_t hour) { bouffeMatin = hour; }
  void setBouffeMidi(uint8_t hour) { bouffeMidi = hour; }
  void setBouffeSoir(uint8_t hour) { bouffeSoir = hour; }
  void setFreqWakeSec(uint16_t sec) { _network.setFreqWakeSec(sec); }
  void setEmailAddress(const char* addr) { _network.setEmailAddress(addr); }
  void setEmailEnabled(bool enabled) { _network.setEmailEnabled(enabled); }
  
  // Méthodes de persistance (fusionnées depuis AutomatismPersistence, publiques pour web_server.cpp)
  void saveCurrentActuatorState(const char* actuator, bool state);
  bool loadCurrentActuatorState(const char* actuator, bool defaultValue = false);
  uint32_t getLastLocalActionTime();
  bool hasRecentLocalAction(uint32_t timeoutMs = 5000);
  void markPendingSync(const char* actuator, bool state);
  void markConfigPendingSync();
  void clearPendingSync(const char* actuator);
  void clearConfigPendingSync();
  bool hasPendingSync();
  uint8_t getPendingSyncCount();
  uint32_t getLastPendingSyncTime();
  
 private:

  void initializeNetworkModule();
  void attachFeedingCallbacks();
  void restorePersistentForceWakeup();
  void initializeRuntimeState();
  // v11.178: restoreActuatorState() supprimé (code mort - audit dead-code)
  bool restoreRemoteConfigFromCache();
  void syncForceWakeupWithServer();
  
  // v11.158: Méthodes privées pour refactoriser update()
  void updateFeedingAndDisplay(const SensorReadings& r, uint32_t nowMs);
  void updateNetworkSync(const SensorReadings& r, uint32_t nowMs);
  void updateBusinessLogic(const SensorReadings& r, uint32_t nowMs);

  SystemSensors& _sensors;
  SystemActuators& _acts;
  WebClient& _web;
  DisplayView& _disp;
  PowerManager& _power;
  Mailer& _mailer;
  ConfigManager& _config;
  
  // === MODULES (Composition) ===
  AutomatismFeedingSchedule _feedingSchedule;
  AutomatismSync _network;
  AutomatismSleep _sleep;

  // state flags - v11.176: Atomic pour accès multi-tâches (audit race conditions)
  std::atomic<bool> tankPumpLocked{false};
  bool serverOk = false;
  int8_t sendState = 0; // -1 erreur, 0 en cours/idle, 1 OK dernier transfert
  int8_t recvState = 0; // idem pour réception

  bool emailFlooding = false;
  // Gestion blocage pompe réservoir
  uint8_t tankPumpRetries = 0;            // essais consécutifs
  static constexpr uint8_t MAX_PUMP_RETRIES = 5; // après 5 essais on abandonne (augmenté de 3 à 5)
  uint16_t _levelAtPumpStart = 0;         // niveau aquarium au démarrage de la pompe
  bool emailTankSent = false;
  bool emailTankStartSent = false;  // Pour éviter les mails de démarrage en double
  bool emailTankStopSent = false;   // Pour éviter les mails d'arrêt en double
  bool heaterPrevState = false;
  bool forceWakeUp = false;
  unsigned long mailBlinkUntil = 0;
  unsigned long _countdownEnd = 0;
  char _countdownLabel[64];
  // v11.172: freqWakeSec supprimé - source de vérité = _network.getFreqWakeSec()
  
  // Protection contre l'écrasement du forceWakeUp au démarrage
  unsigned long _wakeupProtectionEnd = 0;
  
  // Variables de configuration distantes
  // v11.172: limFlood supprimé - source de vérité = _network.getLimFlood()
  // v11.176: Atomic pour accès multi-tâches (audit race conditions)
  std::atomic<uint32_t> _pumpStartMs{0}; // time when tank pump started (ms)
  uint32_t refillDurationMs = 120000; // default 120s, can be updated from web

  // maree / sleep
  static constexpr uint16_t MAREE_THRESHOLD = 1; // cm (legacy, plus utilisé pour sleep)
  // Nouveau seuil de déclenchement marée montante (diff10s > tideTriggerCm)
  int16_t tideTriggerCm = ::SleepConfig::TIDE_TRIGGER_THRESHOLD_CM; // réglable via config
  // SUPPRIMÉ: autoSleepDelayMs obsolète - remplacé par le système adaptatif
  unsigned long _lastWakeMs{0}; // moment du dernier réveil (millis)

  // Nouvelles variables pour la détection d'activité fine
  unsigned long _lastActivityMs{0}; // dernière activité détectée
  unsigned long _lastSensorActivityMs{0}; // dernière activité des capteurs
  unsigned long _lastWebActivityMs{0}; // dernière activité web
  bool _forceWakeFromWeb{false};       // forceWakeUp maintenu par activité web
  bool _hasRecentErrors{false}; // flag d'erreurs récentes
  uint8_t _consecutiveWakeupFailures{0}; // échecs de réveil consécutifs
  
  // Configuration du sleep adaptatif (utilise les constantes de project_config.h)
  // IMPORTANT: Ces valeurs contrôlent les DÉLAIS D'INACTIVITÉ, pas la durée de sommeil
  struct SleepConfig {
    // 5 min minimum d'inactivité
    uint32_t minSleepTime = ::SleepConfig::MIN_INACTIVITY_DELAY_SEC;
    uint32_t maxSleepTime = ::SleepConfig::MAX_INACTIVITY_DELAY_SEC;     // 1h maximum d'inactivité
    // 5 min normal d'inactivité
    uint32_t normalSleepTime = ::SleepConfig::NORMAL_INACTIVITY_DELAY_SEC;
    uint32_t errorSleepTime = ::SleepConfig::ERROR_INACTIVITY_DELAY_SEC;    // 1.5 min si erreurs
    uint32_t nightSleepTime = ::SleepConfig::NIGHT_INACTIVITY_DELAY_SEC;   // 5 min la nuit (19h-6h)
    bool adaptiveSleep = ::SleepConfig::ADAPTIVE_SLEEP_ENABLED;        // sleep adaptatif activé
  } _sleepConfig;

  // feeding
  uint8_t bouffeMatin = 8;
  uint8_t bouffeMidi  = 12;
  uint8_t bouffeSoir  = 19;
  char bouffePetits[8] = "0";
  char bouffeGros[8] = "0";
  int  lastFeedDay = -1;
  // Indique si le cycle de nourrissage en cours a été déclenché manuellement
  bool _manualFeedingActive{false};

  unsigned long lastSend{0};
  const unsigned long sendInterval = 120000;

  // remote fetch timing
  unsigned long _lastRemoteFetch{0};
  // Intervalle entre deux téléchargements de l'état distant
  // v11.145: Augmenté à 60s pour réduire la charge réseau et améliorer la stabilité offline
  const unsigned long remoteFetchInterval = 60000; // 60 s - réduit la dépendance réseau

  // v11.172: thresholds supprimés - source de vérité = _network.getXxxThreshold()
  // aqThresholdCm, tankThresholdCm, heaterThresholdC maintenant dans AutomatismSync

  // durées nourrissage (s) - synchronisées avec BDD distante par défaut
  uint16_t tempsGros = ActuatorConfig::Default::FEED_BIG_DURATION_SEC;
  uint16_t tempsPetits = ActuatorConfig::Default::FEED_SMALL_DURATION_SEC;

  // suivi marée pour affichage
  int _lastDiffMaree{-1};
  
  // États pour les alertes (anciennement dans AutomatismAlertController)
  bool _lowAquaSent{false};
  bool _highAquaSent{false};
  bool _lowTankSent{false};
  
  // v11.162: Délai avant alertes au démarrage (évite saturation queue mail)
  static constexpr uint32_t STARTUP_ALERT_DELAY_MS = 30000;  // 30 secondes
  uint32_t _startupMs{0};  // Timestamp du démarrage
  
  // États pour l'affichage (anciennement dans AutomatismDisplayController)
  unsigned long _splashStartTime{0};

  // Dernières mesures capteurs reçues (mise à jour par update(r))
  SensorReadings _lastReadings{};

  // Gestion des phases de nourrissage
  enum class FeedingPhase {
    NONE,
    FEEDING_FORWARD,    // Position de nourrissage (140°)
    FEEDING_BACKWARD    // Position intermédiaire (45°)
  };
  // v11.176: Atomic pour accès multi-tâches (audit race conditions)
  std::atomic<FeedingPhase> _currentFeedingPhase{FeedingPhase::NONE};
  std::atomic<unsigned long> _feedingPhaseEnd{0};  // Fin de la phase actuelle
  unsigned long _feedingTotalEnd = 0;  // Fin totale du cycle de nourrissage
  // Type de nourrissage actuel pour l'affichage OLED ("Gros" ou "Petits")
  // v11.176: Atomic pointer pour accès multi-tâches
  std::atomic<const char*> _currentFeedingType{nullptr};

  // v11.172: email config supprimé - source de vérité = _network.getEmailAddress() / isEmailEnabled()

  // Anti-spam e-mail inondation (trop plein)
  bool     inFlood{false};                 // état courant « en trop plein »
  // délai min entre 2 mails (minutes)
  uint32_t floodCooldownMin{::SleepConfig::FLOOD_COOLDOWN_MIN};
  // temps minimal sous seuil avant de considérer le trop-plein (minutes)
  uint32_t floodDebounceMin{::SleepConfig::FLOOD_DEBOUNCE_MIN};
  // hystérésis de sortie (cm au-dessus de limFlood)
  uint16_t floodHystCm{::SleepConfig::FLOOD_HYST_CM};
  // durée au-dessus du seuil+hyst avant réarmement (minutes)
  uint32_t floodResetStableMin{::SleepConfig::FLOOD_RESET_STABLE_MIN};
  uint32_t lastFloodEmailEpoch{0};         // dernier envoi d'email (epoch secondes)
  uint32_t floodEnterSinceEpoch{0};        // depuis quand on est sous limFlood (epoch)
  // depuis quand on est au-dessus de limFlood+hyst (epoch)
  uint32_t aboveResetSinceEpoch{0};

  // Permet de basculer entre écran principal et écran Vars.
  // true  => écran principal
  // false => écran Vars
  bool _oledToggle = true; // commence par écran principal
  unsigned long _lastOled{0};
  // Chronomètre de changement d'écran (intervalle: DisplayConfig::SCREEN_SWITCH_INTERVAL_MS)
  unsigned long _lastScreenSwitch{0};
  // Intervalle de rafraîchissement OLED : DisplayConfig::OLED_INTERVAL_MS / OLED_COUNTDOWN_INTERVAL_MS (config.h)

  // Suivi des changements d'état critiques
  bool _prevPumpTankState{false};
  bool _prevPumpAquaState{false};
  bool _prevBouffeMatin{false};
  bool _prevBouffeMidi{false};
  bool _prevBouffeSoir{false};

  // Indique si le cycle en cours (ou le dernier) de la pompe réservoir a été
  // déclenché manuellement (via serveur ou BDD). Sert à personnaliser les
  // notifications e-mail ON/OFF.
  bool _lastPumpManual{false};

  // NEW: indique qu'une activation manuelle de la pompe réservoir est en cours
  // v11.176: Atomic pour accès multi-tâches (audit race conditions)
  std::atomic<bool> _manualTankOverride{false};

  // NEW: raison de verrouillage de la pompe de réserve
  enum class TankPumpLockReason {
    NONE,
    INEFFICIENT,        // n'a pas amélioré le niveau après max tentatives
    RESERVOIR_LOW,      // réserve trop basse (distance grande) → anti-marche à sec
    AQUARIUM_OVERFILL   // aquarium trop plein (distance petite) → interdiction de remplissage
  };
  TankPumpLockReason _tankPumpLockReason{TankPumpLockReason::NONE};

  // Raison d'arrêt de la pompe réservoir (pour emails)
  enum class TankPumpStopReason {
    UNKNOWN,
    MAX_DURATION,
    MANUAL,
    OVERFLOW_SECURITY,
    SLEEP
  };
  TankPumpStopReason _lastTankStopReason{TankPumpStopReason::UNKNOWN};

  // Raison d'arrêt de la pompe aquarium (pour emails)
  enum class AquaPumpStopReason {
    UNKNOWN,
    MANUAL,
    SLEEP
  };
  AquaPumpStopReason _lastAquaStopReason{AquaPumpStopReason::UNKNOWN};

  // Origine des commandes manuelles (pour emails)
  enum class ManualOrigin { NONE, REMOTE_SERVER, LOCAL_SERVER };
  ManualOrigin _lastTankManualOrigin{ManualOrigin::NONE};
  ManualOrigin _lastAquaManualOrigin{ManualOrigin::NONE};

  // v11.179: Déclarations orphelines supprimées (audit code mort)
  // handleAutoSleep/shouldEnterSleepEarly → délégués à AutomatismSleep
  // handleMaree → legacy, jamais implémenté
  // handleAlerts/handleRefill(SensorReadings&) → signatures incorrectes, voir lignes 429/440
  void handleFeeding();
  void finalizeFeedingIfNeeded(uint32_t nowMs);
  void handleRemoteState();
  void checkNewDay();
  void checkCriticalChanges();
  void saveFeedingState();
  void traceFeedingEvent();
  void traceFeedingEventSelective(bool feedSmall, bool feedBig);

  // Nouvelles méthodes pour la détection d'activité fine
  // v11.178: hasSignificantActivity() supprimé (toujours false - audit dead-code)
  void updateActivityTimestamp();
  uint32_t calculateAdaptiveSleepDelay();
  bool isAdaptiveSleepEnabled() const { return _sleepConfig.adaptiveSleep; }
  bool isNightTime();
  bool hasRecentErrors();
  void logActivity(const char* activity);
  
  // Méthodes de vérification post-réveil
  bool verifySystemStateAfterWakeup();
  void detectSleepAnomalies();
  bool validateSystemStateBeforeSleep();

  void logSleepTransitionStart(const char* reason,
                               uint32_t scheduledSeconds,
                               unsigned long awakeSec,
                               bool tideAscending,
                               int diff10s,
                               uint32_t heapAfterCleanup,
                               const TaskMonitor::Snapshot& tasks);

  void logSleepTransitionEnd(uint32_t scheduledSeconds,
                             uint32_t actualSeconds,
                             esp_sleep_wakeup_cause_t wakeCause,
                             const TaskMonitor::Snapshot& tasksBefore,
                             const TaskMonitor::Snapshot& tasksAfter);

  // Méthodes pour remplissage (anciennement dans AutomatismRefillController)
  void handleRefill(const AutomatismRuntimeContext& ctx);
  // Sous-fonctions de handleRefill pour clarté et maintenabilité
  void handleRefillAquariumOverfillSecurity(const SensorReadings& r);
  void handleRefillManualModeCheck();
  bool handleRefillAutomaticStart(const SensorReadings& r);
  void handleRefillManualCycleEnd(const SensorReadings& r);
  void handleRefillMaxDurationStop(const SensorReadings& r);
  void handleRefillReservoirLowSecurity(const SensorReadings& r);
  void handleRefillAutomaticRecovery(const SensorReadings& r);
  
  // Méthodes pour alertes (anciennement dans AutomatismAlertController)
  void handleAlerts(const AutomatismRuntimeContext& ctx);
  
  // Méthodes pour affichage (anciennement dans AutomatismDisplayController)
  void updateDisplayInternal(const AutomatismRuntimeContext& ctx);

  // Persistance NVS: snapshot des actionneurs autour du sleep (fusionné depuis AutomatismPersistence)
  void saveActuatorSnapshotToNVS(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn);
  bool loadActuatorSnapshotFromNVS(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn);
  void clearActuatorSnapshotInNVS();

  void storeEmailAddress(const char* address);
}; 