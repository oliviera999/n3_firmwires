#pragma once
#include "system_sensors.h"
#include "system_actuators.h"
#include "web_client.h"
#include "display_view.h"
#include "power.h"
#include "mailer.h"
#include "config.h"
#include "config_manager.h"
#include "automatism/automatism_feeding_v2.h"
#include "automatism/automatism_feeding_schedule.h"
#include "automatism/automatism_sync.h"
#include "automatism/automatism_sleep.h"
#include "automatism/automatism_state.h"
#include "automatism/automatism_refill_controller.h"
#include "automatism/automatism_alert_controller.h"
#include "automatism/automatism_display_controller.h"
#include "task_monitor.h"
#include <esp_sleep.h>
#include <ArduinoJson.h>

class Automatism {
 public:
  Automatism(SystemSensors& sensors, SystemActuators& acts, WebClient& web, DisplayView& disp, PowerManager& power, Mailer& mail, ConfigManager& config);
  void begin();
  void update();               // collecte interne des capteurs
  void update(const SensorReadings& r); // usage dans tâche dédiée
  void updateDisplay();        // mise à jour de l'affichage OLED (tâche dédiée)
  // Recommande l'intervalle d'update OLED en fonction de l'état courant
  uint32_t getRecommendedDisplayIntervalMs();
  
  // Getters pour accéder aux états depuis l'extérieur
  bool isPumpAquaLocked() const { return _config.getPompeAquaLocked(); }
  bool isTankPumpLocked() const { return tankPumpLocked; }
  time_t getCurrentTime() { return _power.getCurrentEpoch(); }
  String getCurrentTimeString() { return _power.getCurrentTimeString(); }
  uint32_t getPumpStartTime() const { return _pumpStartMs; }  // Pour SystemActuators
  bool isTankPumpRunning() const { return _pumpStartMs > 0; }
  bool fetchRemoteState(ArduinoJson::JsonDocument& doc);

  // --- Accesseurs exposés pour le serveur Web local ---
  bool isEmailEnabled() const { return mailNotif; }
  void toggleEmailNotifications();
  const char* getEmailAddress() const { return _emailAddress; }
  uint16_t getFeedBigDur() const { return _feeding.getBigDuration(); }
  uint16_t getFeedSmallDur() const { return _feeding.getSmallDuration(); }
  uint8_t getBouffeMatin() const { return bouffeMatin; }
  uint8_t getBouffeMidi() const { return bouffeMidi; }
  uint8_t getBouffeSoir() const { return bouffeSoir; }
  uint16_t getTempsGros() const { return tempsGros; }
  uint16_t getTempsPetits() const { return tempsPetits; }
  uint32_t getRefillDurationSec() const { return refillDurationMs / 1000UL; }
  uint16_t getLimFlood() const { return limFlood; }
  const String& getBouffePetitsFlag() const { return bouffePetits; }
  const String& getBouffeGrosFlag() const { return bouffeGros; }
  int computeDiffMaree(uint16_t currentAqua);
  bool isFeedingInProgress() const { return _currentFeedingPhase != FeedingPhase::NONE; }
  uint32_t getCountdownEndMs() const { return _countdownEnd; }
  uint16_t getFreqWakeSec() const { return freqWakeSec; }
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
  
  // Nouvelles commandes pour Force Wakeup et Reset Mode
  void toggleForceWakeup();
  void triggerResetMode();
  bool getForceWakeUp() const { return forceWakeUp; }
  
  // Méthodes publiques pour le serveur web
  bool sendFullUpdate(const SensorReadings& readings, const char* extraPairs = nullptr);
  void manualFeedSmall();
  void manualFeedBig();
  size_t createFeedingMessage(char* buffer,
                              size_t bufferSize,
                              const char* type,
                              uint16_t bigDur,
                              uint16_t smallDur);
  // Applique des variables de configuration depuis un document JSON local (NVS)
  void applyConfigFromJson(const ArduinoJson::JsonDocument& doc);
  
  // Accessor pour modules (permet l'accès contrôlé à _sensors)
  SensorReadings readSensors() const { return _sensors.read(); }
  
  // v11.59: Indicateurs de mode pour OLED (A=Automatique, M=Manuel)
  bool isFeedingInManualMode() const;
  bool isRefillingInManualMode() const;
  
  // Méthodes pour mettre à jour les variables depuis le serveur distant
  void setRefillDurationMs(uint32_t durationMs) { refillDurationMs = durationMs; }
  void setAqThresholdCm(uint16_t threshold) { aqThresholdCm = threshold; }
  void setTankThresholdCm(uint16_t threshold) { tankThresholdCm = threshold; }
  void setLimFlood(uint16_t lim) { limFlood = lim; }
  void setHeaterThresholdC(float threshold) { heaterThresholdC = threshold; }
  void setTempsGros(uint16_t duration) { tempsGros = duration; }
  void setTempsPetits(uint16_t duration) { tempsPetits = duration; }
  void setBouffeMatin(uint8_t hour) { bouffeMatin = hour; }
  void setBouffeMidi(uint8_t hour) { bouffeMidi = hour; }
  void setBouffeSoir(uint8_t hour) { bouffeSoir = hour; }
  
 private:
  friend class AutomatismRefillController;
  friend class AutomatismAlertController;
  friend class AutomatismDisplayController;

  void initializeNetworkModule();
  void attachFeedingCallbacks();
  void restorePersistentForceWakeup();
  void initializeRuntimeState();
  void restoreActuatorState();
  bool restoreRemoteConfigFromCache();
  void syncForceWakeupWithServer();

  SystemSensors& _sensors;
  SystemActuators& _acts;
  WebClient& _web;
  DisplayView& _disp;
  PowerManager& _power;
  Mailer& _mailer;
  ConfigManager& _config;
  
  // === MODULES (Composition) ===
  AutomatismFeeding _feeding;
  AutomatismFeedingSchedule _feedingSchedule;
  AutomatismSync _network;
  AutomatismSleep _sleep;
  AutomatismRefillController _refillController;
  AutomatismAlertController _alertController;
  AutomatismDisplayController _displayController;

  // state flags
  bool tankPumpLocked = false;
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
  String _countdownLabel;
  uint16_t freqWakeSec = 600; // Fréquence de réveil en secondes (10 min par défaut)
  
  // Protection contre l'écrasement du forceWakeUp au démarrage
  unsigned long _wakeupProtectionEnd = 0;
  
  // Variables de configuration distantes
  uint16_t limFlood = 8; // cm threshold, modifiable via JSON
  uint32_t _pumpStartMs = 0; // time when tank pump started (ms)
  uint32_t refillDurationMs = 120000; // default 120s, can be updated from web

  // maree / sleep
  const uint16_t mareeThreshold = 1; // cm (legacy, plus utilisé pour sleep)
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
    uint32_t minSleepTime = ::SleepConfig::MIN_INACTIVITY_DELAY_SEC;      // 5 min minimum d'inactivité
    uint32_t maxSleepTime = ::SleepConfig::MAX_INACTIVITY_DELAY_SEC;     // 1h maximum d'inactivité
    uint32_t normalSleepTime = ::SleepConfig::NORMAL_INACTIVITY_DELAY_SEC;   // 5 min normal d'inactivité
    uint32_t errorSleepTime = ::SleepConfig::ERROR_INACTIVITY_DELAY_SEC;    // 1.5 min si erreurs
    uint32_t nightSleepTime = ::SleepConfig::NIGHT_INACTIVITY_DELAY_SEC;   // 30 min la nuit
    bool adaptiveSleep = ::SleepConfig::ADAPTIVE_SLEEP_ENABLED;        // sleep adaptatif activé
  } _sleepConfig;

  // feeding
  uint8_t bouffeMatin = 8;
  uint8_t bouffeMidi  = 12;
  uint8_t bouffeSoir  = 19;
  String bouffePetits{"0"};
  String bouffeGros{"0"};
  int  lastFeedDay = -1;
  // Indique si le cycle de nourrissage en cours a été déclenché manuellement
  bool _manualFeedingActive{false};

  unsigned long lastSend{0};
  const unsigned long sendInterval = 120000;

  // remote fetch timing
  unsigned long _lastRemoteFetch{0};
  // Intervalle entre deux téléchargements de l'état distant
  // Ajusté à 4s pour cohérence avec la fréquence des capteurs
  const unsigned long remoteFetchInterval = 4000; // 4 s - cohérent avec la fréquence des capteurs

  // thresholds modifiables à distance
  uint16_t aqThresholdCm = ActuatorConfig::Default::AQUA_LEVEL_CM;
  uint16_t tankThresholdCm = ActuatorConfig::Default::TANK_LEVEL_CM;
  float heaterThresholdC  = ActuatorConfig::Default::HEATER_THRESHOLD_C;

  // durées nourrissage (s) - synchronisées avec BDD distante par défaut
  uint16_t tempsGros = ActuatorConfig::Default::FEED_BIG_DURATION_SEC;
  uint16_t tempsPetits = ActuatorConfig::Default::FEED_SMALL_DURATION_SEC;

  // suivi marée pour affichage
  int _lastDiffMaree{-1};

  // Dernières mesures capteurs reçues (mise à jour par update(r))
  SensorReadings _lastReadings{};

  // Gestion des phases de nourrissage
  enum class FeedingPhase {
    NONE,
    FEEDING_FORWARD,    // Position de nourrissage (140°)
    FEEDING_BACKWARD    // Position intermédiaire (45°)
  };
  FeedingPhase _currentFeedingPhase = FeedingPhase::NONE;
  unsigned long _feedingPhaseEnd = 0;  // Fin de la phase actuelle
  unsigned long _feedingTotalEnd = 0;  // Fin totale du cycle de nourrissage
  // Type de nourrissage actuel pour l'affichage OLED ("Gros" ou "Petits")
  const char* _currentFeedingType = nullptr;

  // email config
  char _emailAddress[EmailConfig::MAX_EMAIL_LENGTH];
  bool   mailNotif = true;

  // Anti-spam e-mail inondation (trop plein)
  bool     inFlood{false};                 // état courant « en trop plein »
  uint32_t floodCooldownMin{::SleepConfig::FLOOD_COOLDOWN_MIN};          // délai min entre 2 mails (minutes)
  uint32_t floodDebounceMin{::SleepConfig::FLOOD_DEBOUNCE_MIN};            // temps minimal sous seuil avant de considérer le trop-plein (minutes)
  uint16_t floodHystCm{::SleepConfig::FLOOD_HYST_CM};                 // hystérésis de sortie (cm au-dessus de limFlood)
  uint32_t floodResetStableMin{::SleepConfig::FLOOD_RESET_STABLE_MIN};        // durée au-dessus du seuil+hyst avant réarmement (minutes)
  uint32_t lastFloodEmailEpoch{0};         // dernier envoi d'email (epoch secondes)
  uint32_t floodEnterSinceEpoch{0};        // depuis quand on est sous limFlood (epoch)
  uint32_t aboveResetSinceEpoch{0};        // depuis quand on est au-dessus de limFlood+hyst (epoch)

  // Permet de basculer entre écran principal et écran Vars.
  // true  => écran principal
  // false => écran Vars
  bool _oledToggle = true; // commence par écran principal
  unsigned long _lastOled{0};
  // Chronomètre de changement d'écran
  unsigned long _lastScreenSwitch{0};
  static constexpr unsigned long screenSwitchInterval = 4000; // 4 s pour plus de dynamisme
  // Intervalle de rafraîchissement ultra-fluide de l'OLED (100ms pour temps réel)
  const unsigned long oledInterval = 80; // Optimisé de 100ms à 80ms pour plus de fluidité

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
  bool _manualTankOverride{false};

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

  void handleAutoSleep(const SensorReadings& r);
  bool shouldEnterSleepEarly(const SensorReadings& r);
  void handleMaree(const SensorReadings& r);
  void handleAlerts(const SensorReadings& r);
  void handleRefill(const SensorReadings& r);
  void handleFeeding();
  void finalizeFeedingIfNeeded(uint32_t nowMs);
  void handleRemoteState();
  void checkNewDay();
  void checkCriticalChanges();
  void saveFeedingState();
  void traceFeedingEvent();
  void traceFeedingEventSelective(bool feedSmall, bool feedBig);

  // Nouvelles méthodes pour la détection d'activité fine
  bool hasSignificantActivity();
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

  void handleRefillInternal(const AutomatismRuntimeContext& ctx);
  void updateDisplayInternal(const AutomatismRuntimeContext& ctx);
  uint32_t getRecommendedDisplayIntervalMsInternal(uint32_t nowMs) const;

  // Persistance NVS: snapshot des actionneurs autour du sleep
  void saveActuatorSnapshotToNVS(bool pumpAquaWasOn, bool heaterWasOn, bool lightWasOn);
  bool loadActuatorSnapshotFromNVS(bool& pumpAquaWasOn, bool& heaterWasOn, bool& lightWasOn);
  void clearActuatorSnapshotInNVS();

  void storeEmailAddress(const char* address);
}; 