#include "power.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <cmath>
#include <functional>
#include "project_config.h"

// Simple exécution immédiate – si l'assert LWIP réapparaît, il faudra activer
// LWIP_TCPIP_CORE_LOCKING dans le menuconfig et remplacer cette implémentation
// par un appel tcpip_api_call, mais pour l'instant on garde quelque chose qui
// compile partout.
static inline void tcpip_safe_call(std::function<void()> fn) {
  fn();
}

PowerManager::PowerManager()
    : _gmtOffsetSec(SystemConfig::NTP_GMT_OFFSET_SEC), _daylightOffsetSec(SystemConfig::NTP_DAYLIGHT_OFFSET_SEC), _ntpServer(SystemConfig::NTP_SERVER),
      _lastNtpSync(0), _lastSSID(""), _lastPassword(""), _hasSavedCredentials(false),
      _lastTimeSave(0), _lastSavedEpoch(0), _lastDriftCorrection(0), _currentDriftPPM(0.0f), _lastSyncEpoch(0),
      _lastDriftSeconds(0.0f), _defaultDriftAccumulator(0.0f), _measuredDriftAccumulator(0.0f), _sleepRemainderUs(0) {
}

void PowerManager::initWatchdog() {
  static bool initialized = false;
  if (initialized) {
    esp_task_wdt_reset();
    return;
  }
  initialized = true;
  esp_task_wdt_add(NULL);
  esp_task_wdt_reset();
  Serial.println(F("[Power] Watchdog prêt (géré par TWDT natif)"));
}

void PowerManager::resetWatchdog() {
  esp_task_wdt_reset();
}

void PowerManager::goToLightSleep(uint32_t sleepTimeSeconds) {
  Serial.printf("[Power] Mise en veille légère pour %u secondes\n", sleepTimeSeconds);
  
  // Timestamp avant le passage en veille
  const uint64_t startUs = esp_timer_get_time();

  // Sauvegarde de l'heure avant le sommeil (si configuré)
  if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
    saveTimeToFlash();
  }
  
  // Sauvegarde des identifiants WiFi avant la déconnexion (si configuré)
  if (SleepConfig::SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP) {
    saveCurrentWifiCredentials();
  }
  
  // Configuration du réveil
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL); // Conversion en microsecondes
  
  // Déconnexion WiFi avant le sommeil (si configuré)
  if (SleepConfig::DISCONNECT_WIFI_BEFORE_SLEEP) {
    tcpip_safe_call([](){ WiFi.disconnect(); });
    Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
  }
  
  // Mise en veille
  esp_light_sleep_start();
  
  // Calcul effectif du temps passé en veille légère
  const uint64_t sleptUs = esp_timer_get_time() - startUs;
  _sleepRemainderUs += (uint32_t)(sleptUs % 1000000ULL);
  const uint32_t carrySec = _sleepRemainderUs / 1000000UL;
  _sleepRemainderUs = _sleepRemainderUs % 1000000UL;
  const uint32_t sleptSec = (uint32_t)(sleptUs / 1000000ULL) + carrySec;

  // Ajustement de l'horloge système pour refléter le temps réellement écoulé
  if (sleptSec > 0) {
    time_t currentEpoch = time(nullptr);
    time_t newEpoch = currentEpoch + sleptSec;
    timeval tv = {newEpoch, 0};
    settimeofday(&tv, nullptr);
    Serial.printf("[Power] Veille légère: %llu us (~%u s), reste=%u us – heure: %s\n",
                  (unsigned long long)sleptUs, sleptSec, _sleepRemainderUs, getCurrentTimeString().c_str());

    // Persistance conditionnelle: près d'une frontière de seconde ou périodiquement
    const bool nearSecondBoundary = (_sleepRemainderUs < 20000U); // <20 ms
    const unsigned long nowMs = millis();
    const bool intervalElapsed = (_lastTimeSave == 0) || (nowMs - _lastTimeSave > (::SleepConfig::MAX_SAVE_INTERVAL_MS * 3UL));
    const time_t curEpoch = time(nullptr);
    const bool largeDrift = (labs(curEpoch - _lastSavedEpoch) > 3600);
    if (nearSecondBoundary || intervalElapsed || largeDrift) {
      saveTimeToFlash();
    }
  }

  // Réveil - reconnexion WiFi avec les identifiants sauvegardés (si configuré)
  if (SleepConfig::AUTO_RECONNECT_WIFI_AFTER_SLEEP) {
    Serial.println(F("[Power] Réveil - reconnexion WiFi"));
    if (!reconnectWithSavedCredentials()) {
      Serial.println(F("[Power] Échec reconnexion avec identifiants sauvegardés - laissez wifi.loop() prendre le relais"));
    } else {
      // Événement clé: après reconnexion WiFi, persiste l'heure courante
      if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
        saveTimeToFlash();
      }
    }
  }
}

// Suppression des fonctions deep sleep non utilisées

void PowerManager::setNTPConfig(int gmtOffset, int daylightOffset, const char* ntpServer) {
  _gmtOffsetSec = gmtOffset;
  _daylightOffsetSec = daylightOffset;
  _ntpServer = ntpServer;
  Serial.printf("[Power] Configuration NTP: GMT+%d, DST+%d, serveur: %s\n", 
                gmtOffset/3600, daylightOffset/3600, ntpServer);
}

void PowerManager::initTime() {
  LOG_TIME(LogConfig::LOG_INFO, "Initialisation de la gestion du temps");
  
  // Initialiser les variables de dérive
  _lastDriftCorrection = 0;
  _currentDriftPPM = 0.0f;
  _lastSyncEpoch = 0;
  _lastDriftSeconds = 0.0f;
  _defaultDriftAccumulator = 0.0f;
  _measuredDriftAccumulator = 0.0f;
  
  // Chargement de l'heure sauvegardée avec fallback robuste
  time_t loadedEpoch = loadTimeWithFallback();
  
  // Configuration de la timezone
  setenv("TZ", "CET-1CEST,M3.5.0,M10.5.0/3", 1);
  tzset();
  
  LOG_TIME(LogConfig::LOG_INFO, "Heure actuelle: %s (epoch: %lu)", 
           getCurrentTimeString().c_str(), loadedEpoch);
  
  // Informations détaillées sur l'état temporel
  time_t currentEpoch = time(nullptr);
  struct tm timeinfo;
  if (localtime_r(&currentEpoch, &timeinfo)) {
    LOG_TIME(LogConfig::LOG_INFO, "Détails temporels - Année: %d, Mois: %d, Jour: %d, Heure: %d, Min: %d, Sec: %d",
             timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday,
             timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec);
    LOG_TIME(LogConfig::LOG_INFO, "Jour de la semaine: %d, Jour de l'année: %d, DST: %s",
             timeinfo.tm_wday, timeinfo.tm_yday, timeinfo.tm_isdst ? "OUI" : "NON");
  }
  
  // Informations sur la configuration NTP (offsets désormais gérés par TZ)
  LOG_NTP(LogConfig::LOG_INFO, "Configuration NTP - Serveur: %s (TZ active)", 
          _ntpServer);
}

void PowerManager::syncTimeFromNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_NTP(LogConfig::LOG_WARN, "Pas de WiFi - synchronisation NTP impossible");
    return;
  }
  
  LOG_NTP(LogConfig::LOG_INFO, "Début de synchronisation NTP avec serveur: %s", _ntpServer);
  
  // Sauvegarder l'heure locale avant sync pour calcul de dérive
  time_t localBeforeEpoch = time(nullptr);
  unsigned long localBeforeMillis = millis();
  
  // Configuration NTP: offsets gérés via TZ
  configTime(0, 0, _ntpServer);
  
  // Attente de la synchronisation avec timeout
  struct tm timeinfo;
  unsigned long startTime = millis();
  bool syncSuccess = false;
  int attempts = 0;
  
  while (millis() - startTime < 10000) { // 10 secondes max
    if (getLocalTime(&timeinfo)) {
      syncSuccess = true;
      break;
    }
    attempts++;
    delay(100);
  }
  
  unsigned long syncDuration = millis() - startTime;
  
  if (syncSuccess) {
    // Sauvegarde de l'heure synchronisée
    smartSaveTime();

    unsigned long syncMillis = millis();
    time_t ntpEpoch = mktime(&timeinfo);
    _lastSyncEpoch = ntpEpoch;
    _currentDriftPPM = 0.0f;
    _lastDriftSeconds = 0.0f;
    _defaultDriftAccumulator = 0.0f;
    _measuredDriftAccumulator = 0.0f;
    _lastDriftCorrection = syncMillis;

    LOG_NTP(LogConfig::LOG_INFO, "Synchronisation NTP réussie en %lu ms (%d tentatives)", 
            syncDuration, attempts);
    LOG_NTP(LogConfig::LOG_INFO, "Heure NTP: %s (epoch: %lu)", 
            getCurrentTimeString().c_str(), ntpEpoch);
    
    // Calcul de la dérive si on avait une heure locale valide
    if (localBeforeEpoch > 1600000000) {
      time_t timeDiff = ntpEpoch - localBeforeEpoch;
      unsigned long millisDiff = syncMillis - localBeforeMillis;
      LOG_DRIFT(LogConfig::LOG_INFO, "Différence temps local vs NTP: %ld secondes (%lu ms)", 
                timeDiff, millisDiff);
    }

    _lastNtpSync = syncMillis;
  } else {
    LOG_NTP(LogConfig::LOG_ERROR, "Échec de synchronisation NTP après %lu ms (%d tentatives)", 
            syncDuration, attempts);
  }
}

void PowerManager::saveTimeToFlash() {
  // Délégation vers la nouvelle méthode intelligente
  smartSaveTime();
}

void PowerManager::forceSaveTimeToFlash() {
  time_t currentEpoch = time(nullptr);
  
  if (!isValidEpoch(currentEpoch)) {
    Serial.printf("[Power] Epoch invalide ignoré: %lu\n", currentEpoch);
    return;
  }
  
  _preferences.begin("rtc", false);
  if (!_preferences.putULong("epoch", currentEpoch)) {
    Serial.println(F("[Power] Erreur: impossible d'enregistrer l'heure"));
    _preferences.end();
    return;
  }
  
  // Mise à jour des variables de suivi
  _lastTimeSave = millis();
  _lastSavedEpoch = currentEpoch;
  
  Serial.printf("[Power] Heure sauvegardée de force: %s (epoch: %lu)\n", 
                getCurrentTimeString().c_str(), currentEpoch);
  _preferences.end();
}

void PowerManager::loadTimeFromFlash() {
  // Délégation vers la nouvelle méthode avec fallback
  loadTimeWithFallback();
}

void PowerManager::updateTime() {
  // Synchronisation périodique NTP si WiFi disponible
  if (WiFi.status() == WL_CONNECTED && 
      (millis() - _lastNtpSync > NTP_SYNC_INTERVAL || _lastNtpSync == 0)) {
    syncTimeFromNTP();
  }
  
  // Application de la correction de dérive
  applyDriftCorrection();
  
  // Sauvegarde intelligente périodique
  smartSaveTime();
} 

// ---------------------------------------------------------------------------
// Formatage de l'heure en chaîne locale (HH:MM:SS JJ/MM/AAAA)
// ---------------------------------------------------------------------------
String PowerManager::getCurrentTimeString() {
  time_t epoch = time(nullptr);

  struct tm timeinfo;
  if (!localtime_r(&epoch, &timeinfo)) {
    return String("00:00:00 01/01/1970");
  }

  char buffer[25];
  strftime(buffer, sizeof(buffer), "%H:%M:%S %d/%m/%Y", &timeinfo);
  return String(buffer);
} 

void PowerManager::saveCurrentWifiCredentials() {
  if (WiFi.status() == WL_CONNECTED) {
    _lastSSID = WiFi.SSID();
    _lastPassword = WiFi.psk(); // Récupère le mot de passe actuel
    _hasSavedCredentials = true;
    Serial.printf("[Power] Identifiants WiFi sauvegardés: SSID=%s\n", _lastSSID.c_str());
  } else {
    _hasSavedCredentials = false;
    Serial.println(F("[Power] Pas de connexion WiFi active - pas d'identifiants à sauvegarder"));
  }
}

bool PowerManager::reconnectWithSavedCredentials() {
  if (!_hasSavedCredentials || _lastSSID.isEmpty()) {
    Serial.println(F("[Power] Aucun identifiant WiFi sauvegardé"));
    return false;
  }
  
  Serial.printf("[Power] Tentative de reconnexion WiFi avec SSID: %s\n", _lastSSID.c_str());
  
  // Mode station
  WiFi.mode(WIFI_STA);
  
  // Démarrage de la connexion WiFi avec les identifiants sauvegardés
  if (_lastPassword.isEmpty()) {
    Serial.println(F("[Power] Connexion sans mot de passe"));
    WiFi.begin(_lastSSID.c_str());
  } else {
    Serial.println(F("[Power] Connexion avec mot de passe"));
    WiFi.begin(_lastSSID.c_str(), _lastPassword.c_str());
  }
  
  // Attente de la connexion avec timeout
  uint32_t startTime = millis();
  const uint32_t timeoutMs = 10000; // 10 secondes de timeout
  
  Serial.print(F("[Power] Attente de connexion"));
  while (WiFi.status() != WL_CONNECTED && (millis() - startTime) < timeoutMs) {
    vTaskDelay(pdMS_TO_TICKS(100)); // Optimized delay - reduced from 250ms to 100ms for faster reconnection
    if ((millis() - startTime) % 1000 == 0) {
      Serial.print(".");
    }
  }
  Serial.println();
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[Power] Reconnexion WiFi réussie à %s (%s)\n", 
                  _lastSSID.c_str(), WiFi.localIP().toString().c_str());
    WiFi.setSleep(false); // Désactive le modem-sleep pour fiabilité
    return true;
  } else {
    Serial.printf("[Power] Échec de reconnexion WiFi à %s (timeout après %u ms)\n", 
                  _lastSSID.c_str(), timeoutMs);
    return false;
  }
}

// ========================================
// NOUVELLES MÉTHODES DE GESTION TEMPORELLE ROBUSTE
// ========================================

bool PowerManager::isValidEpoch(time_t epoch) const {
  return epoch >= ::SleepConfig::EPOCH_MIN_VALID && 
         epoch <= ::SleepConfig::EPOCH_MAX_VALID &&
         epoch != 0;
}

time_t PowerManager::loadTimeWithFallback() {
  _preferences.begin("rtc", true);
  time_t savedEpoch = _preferences.getULong("epoch", 0);
  _preferences.end();
  
  // Fallback 1: Epoch sauvegardé valide
  if (isValidEpoch(savedEpoch)) {
    Serial.printf("[Power] Epoch sauvegardé valide: %lu\n", savedEpoch);
    timeval tv = {savedEpoch, 0};
    settimeofday(&tv, nullptr);
    return savedEpoch;
  }
  
  // Fallback 2: Epoch de compilation
  if (isValidEpoch(::SleepConfig::EPOCH_COMPILE_TIME)) {
    Serial.printf("[Power] Utilisation epoch de compilation: %lu\n", ::SleepConfig::EPOCH_COMPILE_TIME);
    timeval tv = {::SleepConfig::EPOCH_COMPILE_TIME, 0};
    settimeofday(&tv, nullptr);
    return ::SleepConfig::EPOCH_COMPILE_TIME;
  }
  
  // Fallback 3: Epoch par défaut
  Serial.printf("[Power] Utilisation epoch par défaut: %lu\n", ::SleepConfig::EPOCH_DEFAULT_FALLBACK);
  timeval tv = {::SleepConfig::EPOCH_DEFAULT_FALLBACK, 0};
  settimeofday(&tv, nullptr);
  return ::SleepConfig::EPOCH_DEFAULT_FALLBACK;
}

void PowerManager::applyDriftCorrection() {
  // Interrupteur global: désactive toute correction
  if (!::SleepConfig::ENABLE_DRIFT_CORRECTION) {
    return;
  }
  const unsigned long now = millis();

  if (_lastDriftCorrection != 0 && now - _lastDriftCorrection < ::SleepConfig::DRIFT_CORRECTION_INTERVAL_MS) {
    return;
  }

  const unsigned long elapsedMs = (_lastDriftCorrection == 0) ? now : (now - _lastDriftCorrection);
  const float elapsedSeconds = elapsedMs / 1000.0f;

  if (elapsedSeconds <= 0.0f) {
    _lastDriftCorrection = now;
    return;
  }

  if (_lastNtpSync == 0 && ::SleepConfig::ENABLE_DEFAULT_DRIFT_CORRECTION) {
    _defaultDriftAccumulator += (::SleepConfig::DEFAULT_DRIFT_CORRECTION_PPM * elapsedSeconds) / 1000000.0f;
    float wholeSeconds = std::trunc(_defaultDriftAccumulator);
    if (wholeSeconds != 0.0f) {
      const int correctionSeconds = static_cast<int>(wholeSeconds);
      time_t currentEpoch = time(nullptr);
      time_t correctedEpoch = currentEpoch + correctionSeconds;

      if (isValidEpoch(correctedEpoch)) {
        timeval tv = {correctedEpoch, 0};
        settimeofday(&tv, nullptr);

        Serial.printf("[Power] Correction de dérive par défaut appliquée: %d s (dérive configurée: %.2f PPM)\n",
                      correctionSeconds, ::SleepConfig::DEFAULT_DRIFT_CORRECTION_PPM);
        smartSaveTime();
      }

      _defaultDriftAccumulator -= wholeSeconds;
    }

    _lastDriftCorrection = now;
    return;
  }

  if (_lastNtpSync == 0 || std::fabs(_currentDriftPPM) < ::SleepConfig::DRIFT_CORRECTION_THRESHOLD_PPM) {
    _lastDriftCorrection = now;
    return;
  }

  _measuredDriftAccumulator += (_currentDriftPPM * elapsedSeconds * ::SleepConfig::DRIFT_CORRECTION_FACTOR) / 1000000.0f;
  float wholeSeconds = std::trunc(_measuredDriftAccumulator);
  if (wholeSeconds != 0.0f) {
    const int correctionSeconds = static_cast<int>(wholeSeconds);
    time_t currentEpoch = time(nullptr);
    time_t correctedEpoch = currentEpoch + correctionSeconds;

    if (isValidEpoch(correctedEpoch)) {
      timeval tv = {correctedEpoch, 0};
      settimeofday(&tv, nullptr);

      Serial.printf("[Power] Correction de dérive appliquée: %d s (dérive: %.2f PPM)\n",
                    correctionSeconds, _currentDriftPPM);
      smartSaveTime();
    }

    _measuredDriftAccumulator -= wholeSeconds;
  }

  _lastDriftCorrection = now;
}



void PowerManager::setMeasuredDrift(float driftPpm, float driftSeconds) {
  _currentDriftPPM = driftPpm;
  _lastDriftSeconds = driftSeconds;
  _measuredDriftAccumulator = 0.0f;
  _defaultDriftAccumulator = 0.0f;
  _lastDriftCorrection = millis();
}

void PowerManager::onExternalNtpSync(time_t epoch, unsigned long syncMillis) {
  _lastSyncEpoch = epoch;
  _lastNtpSync = syncMillis;
  _currentDriftPPM = 0.0f;
  _lastDriftSeconds = 0.0f;
  _defaultDriftAccumulator = 0.0f;
  _measuredDriftAccumulator = 0.0f;
  _lastDriftCorrection = syncMillis;
}

void PowerManager::smartSaveTime() {
  time_t currentEpoch = time(nullptr);
  unsigned long currentMillis = millis();
  
  // Validation stricte de l'epoch
  if (!isValidEpoch(currentEpoch)) {
    Serial.printf("[Power] Epoch invalide ignoré: %lu\n", currentEpoch);
    return;
  }
  
  // Logique de sauvegarde intelligente
  bool shouldSave = false;
  
  // 1. Première sauvegarde
  if (_lastTimeSave == 0) {
    shouldSave = true;
    Serial.println(F("[Power] Première sauvegarde d'heure"));
  }
  // 2. Sauvegarde forcée périodique
  else if (currentMillis - _lastTimeSave > ::SleepConfig::MAX_SAVE_INTERVAL_MS) {
    shouldSave = true;
    Serial.println(F("[Power] Sauvegarde forcée périodique"));
  }
  // 3. Sauvegarde si différence significative
  else if ((currentMillis - _lastTimeSave > ::SleepConfig::MIN_SAVE_INTERVAL_MS) && 
           (abs(currentEpoch - _lastSavedEpoch) > ::SleepConfig::MIN_TIME_DIFF_FOR_SAVE_SEC)) {
    shouldSave = true;
    Serial.printf("[Power] Sauvegarde (diff: %ld s)\n", abs(currentEpoch - _lastSavedEpoch));
  }
  
  if (!shouldSave) {
    return;
  }
  
  // Sauvegarde en NVS
  _preferences.begin("rtc", false);
  bool success = _preferences.putULong("epoch", currentEpoch);
  _preferences.end();
  
  if (success) {
    _lastTimeSave = currentMillis;
    _lastSavedEpoch = currentEpoch;
    Serial.printf("[Power] Heure sauvegardée: %s (epoch: %lu)\n", 
                  getCurrentTimeString().c_str(), currentEpoch);
  } else {
    Serial.println(F("[Power] Erreur: impossible d'enregistrer l'heure"));
  }
}