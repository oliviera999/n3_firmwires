#include "power.h"
#include "nvs_manager.h" // v11.107
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <cmath>
#include <functional>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "log.h"
#include "realtime_websocket.h"

// Fonction tcpip_safe_call supprimée - appels directs utilisés à la place

PowerManager::PowerManager()
    : _gmtOffsetSec(SystemConfig::NTP_GMT_OFFSET_SEC), _daylightOffsetSec(SystemConfig::NTP_DAYLIGHT_OFFSET_SEC), _ntpServer(SystemConfig::NTP_SERVER),
      _lastNtpSync(0), _lastSSID(""), _lastPassword(""), _hasSavedCredentials(false),
      _useModemSleep(false), _modemSleepEnabled(false), _wifiWakeupEnabled(false), _lastModemSleepTest(0),
      _lastTimeSave(0), _lastSavedEpoch(0), _lastDriftCorrection(0), _currentDriftPPM(0.0f), _lastSyncEpoch(0),
      _lastDriftSeconds(0.0f), _defaultDriftAccumulator(0.0f), _measuredDriftAccumulator(0.0f), _sleepRemainderUs(0) {
  
  Serial.println("[Power] ✅ PowerManager initialisé avec support Modem Sleep + Light Sleep");
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

uint32_t PowerManager::goToLightSleep(uint32_t sleepTimeSeconds) {
  // NOUVELLE LOGIQUE : Utiliser modem sleep si configuré et WiFi connecté
  if (_useModemSleep && WiFi.status() == WL_CONNECTED) {
    Serial.println("[Power] Mode modem sleep activé - Utilisation du nouveau système");
    return goToModemSleepWithLightSleep(sleepTimeSeconds);
  }
  
  // LOGIQUE EXISTANTE : Light sleep classique
  Serial.printf("[Power] Mise en veille légère pour %u secondes\n", sleepTimeSeconds);
  
  // Timestamp avant le passage en veille
  const uint64_t startUs = esp_timer_get_time();

  // Sauvegarde de l'heure avant le sommeil (si configuré)
  if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
    saveTimeToFlash();
  }
  
  // Sauvegarde des identifiants WiFi avant la déconnexion (si configuré)
  // Utiliser la configuration existante ou une valeur par défaut
  #ifdef SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP
  if (SAVE_WIFI_CREDENTIALS_BEFORE_SLEEP) {
    saveCurrentWifiCredentials();
  }
  #else
  saveCurrentWifiCredentials(); // Par défaut, sauvegarder
  #endif
  
  // Configuration du réveil
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL); // Conversion en microsecondes
  
  // Déconnexion WiFi avant le sommeil (si configuré)
  // Utiliser la configuration existante ou une valeur par défaut
  bool disconnectWifi = true;
  #ifdef DISCONNECT_WIFI_BEFORE_SLEEP
  disconnectWifi = DISCONNECT_WIFI_BEFORE_SLEEP;
  #endif

  if (disconnectWifi) {
    // AMÉLIORATION: Fermer proprement le WebSocket avant de déconnecter le WiFi
    // pour éviter les erreurs 1006 (connexion fermée sans Close frame)
    uint8_t wsClients = g_realtimeWebSocket.getConnectedClients();
    
    if (wsClients > 0) {
      Serial.printf("[Power] 🔌 Fermeture propre de %u connexion(s) WebSocket avant sleep...\n", wsClients);
      
      // Notifier les clients que le serveur entre en veille avec message explicite
      // Le client recevra un message "server_closing" qui bloquera temporairement les reconnexions
      g_realtimeWebSocket.notifyWifiChange("System entering light sleep mode");
      
      // Fermer toutes les connexions proprement avec Close frame
      g_realtimeWebSocket.closeAllConnections();
      
      // Délai critique pour assurer l'envoi des Close frames avant déconnexion WiFi
      // Réduit le risque de code 1006 (fermeture anormale)
      vTaskDelay(pdMS_TO_TICKS(350));
    }
    
    // Déconnecter le WiFi
    WiFi.disconnect();
    Serial.println(F("[Power] WiFi déconnecté avant sommeil"));
  }
  
  // DIAGNOSTIC FINAL: Log mémoire juste avant sleep
  uint32_t finalHeap = ESP.getFreeHeap();
  uint32_t minHeap = ESP.getMinFreeHeap();
  Serial.printf("[Power] 📊 Mémoire avant sleep: %u bytes (min historique: %u bytes)\n", 
                finalHeap, minHeap);
  
  if (finalHeap < 30000) {
    Serial.printf("[Power] 🚨 ATTENTION CRITIQUE: Heap très bas (%u bytes) avant sleep\n", finalHeap);
  }
  
  // Mise en veille
  esp_light_sleep_start();
  
  // Calcul effectif du temps passé en veille légère
  const uint32_t sleptSec = getSleptTime(startUs);

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
  
  // Retourner la durée réelle de veille
  return sleptSec;
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
  
  // Configuration de la timezone pour le Maroc (UTC+1 fixe)
  // Utilisation directe des offsets GMT en secondes (plus simple et fiable)
  configTime(_gmtOffsetSec, _daylightOffsetSec, _ntpServer);
  
  LOG_TIME(LogConfig::LOG_INFO, "Heure actuelle: %s (epoch: %lu)", 
           getCurrentTimeString().c_str(), loadedEpoch);
  LOG_TIME(LogConfig::LOG_INFO, "Timezone configurée: GMT+%d heures (offset: %d secondes)", 
           _gmtOffsetSec/3600, _gmtOffsetSec);
  
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
  
  // Configuration NTP avec offset GMT en secondes (utilise les variables membres)
  configTime(_gmtOffsetSec, _daylightOffsetSec, _ntpServer);
  
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
    // Utiliser vTaskDelay() pour être conforme aux règles (peut être appelé depuis loop())
    vTaskDelay(pdMS_TO_TICKS(100));
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
  
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "rtc_epoch", currentEpoch);

  // Mise à jour des variables de suivi
  _lastTimeSave = millis();
  _lastSavedEpoch = currentEpoch;
  
  Serial.printf("[Power] Heure sauvegardée de force: %s (epoch: %lu)\n", 
                getCurrentTimeString().c_str(), currentEpoch);
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
    WiFi.setSleep(true);  // Active le modem-sleep pour économie d'énergie
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
  return epoch >= SleepConfig::EPOCH_MIN_VALID && 
         epoch <= SleepConfig::EPOCH_MAX_VALID &&
         epoch != 0;
}

  time_t PowerManager::loadTimeWithFallback() {
  unsigned long savedEpochUL;
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "rtc_epoch", savedEpochUL, 0);
  time_t savedEpoch = static_cast<time_t>(savedEpochUL);
  
  // Fallback 1: Epoch sauvegardé valide
  if (isValidEpoch(savedEpoch)) {
    Serial.printf("[Power] Epoch sauvegardé valide: %lu\n", savedEpoch);
    timeval tv = {savedEpoch, 0};
    settimeofday(&tv, nullptr);
    return savedEpoch;
  }
  
  // Fallback 2: Epoch de compilation
  if (isValidEpoch(SleepConfig::EPOCH_COMPILE_TIME)) {
    Serial.printf("[Power] Utilisation epoch de compilation: %lu\n", SleepConfig::EPOCH_COMPILE_TIME);
    timeval tv = {SleepConfig::EPOCH_COMPILE_TIME, 0};
    settimeofday(&tv, nullptr);
    return SleepConfig::EPOCH_COMPILE_TIME;
  }
  
  // Fallback 3: Epoch par défaut
  Serial.printf("[Power] Utilisation epoch par défaut: %lu\n", SleepConfig::EPOCH_DEFAULT_FALLBACK);
  timeval tv = {SleepConfig::EPOCH_DEFAULT_FALLBACK, 0};
  settimeofday(&tv, nullptr);
  return SleepConfig::EPOCH_DEFAULT_FALLBACK;
}

void PowerManager::applyDriftCorrection() {
  // Interrupteur global: désactive toute correction
  if (!SleepConfig::ENABLE_DRIFT_CORRECTION) {
    return;
  }
  const unsigned long now = millis();

  if (_lastDriftCorrection != 0 && now - _lastDriftCorrection < SleepConfig::DRIFT_CORRECTION_INTERVAL_MS) {
    return;
  }

  const unsigned long elapsedMs = (_lastDriftCorrection == 0) ? now : (now - _lastDriftCorrection);
  const float elapsedSeconds = elapsedMs / 1000.0f;

  if (elapsedSeconds <= 0.0f) {
    _lastDriftCorrection = now;
    return;
  }

  if (_lastNtpSync == 0 && SleepConfig::ENABLE_DEFAULT_DRIFT_CORRECTION) {
    _defaultDriftAccumulator += (SleepConfig::DEFAULT_DRIFT_CORRECTION_PPM * elapsedSeconds) / 1000000.0f;
    float wholeSeconds = std::trunc(_defaultDriftAccumulator);
    if (wholeSeconds != 0.0f) {
      const int correctionSeconds = static_cast<int>(wholeSeconds);
      time_t currentEpoch = time(nullptr);
      time_t correctedEpoch = currentEpoch + correctionSeconds;

      if (isValidEpoch(correctedEpoch)) {
        timeval tv = {correctedEpoch, 0};
        settimeofday(&tv, nullptr);

        Serial.printf("[Power] Correction de dérive par défaut appliquée: %d s (dérive configurée: %.2f PPM)\n",
                      correctionSeconds, SleepConfig::DEFAULT_DRIFT_CORRECTION_PPM);
        smartSaveTime();
      }

      _defaultDriftAccumulator -= wholeSeconds;
    }

    _lastDriftCorrection = now;
    return;
  }

  if (_lastNtpSync == 0 || std::fabs(_currentDriftPPM) < SleepConfig::DRIFT_CORRECTION_THRESHOLD_PPM) {
    _lastDriftCorrection = now;
    return;
  }

  _measuredDriftAccumulator += (_currentDriftPPM * elapsedSeconds * SleepConfig::DRIFT_CORRECTION_FACTOR) / 1000000.0f;
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
  else if (currentMillis - _lastTimeSave > SleepConfig::MAX_SAVE_INTERVAL_MS) {
    shouldSave = true;
    Serial.println(F("[Power] Sauvegarde forcée périodique"));
  }
  // 3. Sauvegarde si différence significative
  else if ((currentMillis - _lastTimeSave > SleepConfig::MIN_SAVE_INTERVAL_MS) && 
           (abs(currentEpoch - _lastSavedEpoch) > SleepConfig::MIN_TIME_DIFF_FOR_SAVE_SEC)) {
    shouldSave = true;
    Serial.printf("[Power] Sauvegarde (diff: %ld s)\n", abs(currentEpoch - _lastSavedEpoch));
  }
  
  if (!shouldSave) {
    return;
  }
  
  // Sauvegarde en NVS
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "rtc_epoch", currentEpoch);

  _lastTimeSave = currentMillis;
  _lastSavedEpoch = currentEpoch;
  Serial.printf("[Power] Heure sauvegardée: %s (epoch: %lu)\n", 
                getCurrentTimeString().c_str(), currentEpoch);
}

// ========================================
// NOUVELLES MÉTHODES : MODEM SLEEP AVEC LIGHT SLEEP
// ========================================

uint32_t PowerManager::goToModemSleepWithLightSleep(uint32_t sleepTimeSeconds) {
  Serial.printf("[Power] Modem sleep + Light sleep pour %u secondes\n", sleepTimeSeconds);
  
  // Vérifier si WiFi est connecté
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Power] WiFi non connecté - Fallback vers light sleep classique");
    return goToLightSleep(sleepTimeSeconds);
  }
  
  // Timestamp avant le passage en veille
  const uint64_t startUs = esp_timer_get_time();
  
  // Sauvegarde de l'heure avant le sommeil (si configuré)
  if (SleepConfig::SAVE_TIME_BEFORE_SLEEP) {
    saveTimeToFlash();
  }
  
  // IMPORTANT : NE PAS déconnecter le WiFi
  // Le WiFi reste connecté en modem sleep automatique
  
  // CRITIQUE : Attendre que toutes les opérations réseau soient terminées
  // avant d'entrer en sleep pour éviter les conflits LWIP/TCPIP
  vTaskDelay(pdMS_TO_TICKS(100));  // 100ms pour terminer les opérations en cours
  
  // Configuration du réveil par timer
  esp_sleep_enable_timer_wakeup(sleepTimeSeconds * 1000000ULL);
  
  // NOTE : Le réveil WiFi fonctionne AUTOMATIQUEMENT avec le modem sleep
  // PAS BESOIN d'appeler esp_sleep_enable_wifi_wakeup() qui n'est pas disponible partout
  // Le modem WiFi reste actif en light sleep et peut réveiller l'ESP32
  
  // Configuration SIMPLIFIÉE des domaines de puissance pour light sleep avec WiFi
  // On ne touche PAS aux power domains - le modem sleep gère tout automatiquement
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);   // RTC ON pour timer wakeup
  
  _wifiWakeupEnabled = true;  // Indicateur que WiFi peut réveiller
  
  Serial.println("[Power] WiFi maintenu connecté - Réveil réseau automatique via modem sleep");
  Serial.printf("[Power] IP: %s, RSSI: %d dBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
  
  // Light sleep avec WiFi en modem sleep automatique
  esp_light_sleep_start();
  
  // Après réveil, déterminer la cause
  esp_sleep_wakeup_cause_t wakeup_cause = esp_sleep_get_wakeup_cause();
  logWakeupCause(wakeup_cause);
  
  _wifiWakeupEnabled = false;
  
  // Vérifier que le WiFi est toujours connecté
  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("[Power] WiFi toujours connecté après réveil (%s)\n", 
                  WiFi.localIP().toString().c_str());
  } else {
    Serial.println("[Power] ⚠️ WiFi déconnecté après réveil - reconnexion nécessaire");
    // Tentative de reconnexion avec identifiants sauvegardés
    if (_hasSavedCredentials) {
      reconnectWithSavedCredentials();
    }
  }
  
  return getSleptTime(startUs);
}

void PowerManager::enableModemSleepMode() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Power] ⚠️ Impossible d'activer modem sleep - WiFi non connecté");
    return;
  }
  
  // Activation du modem sleep automatique
  WiFi.setSleep(true);
  
  // Configuration DTIM optimale
  // esp_wifi_set_ps(1); // WIFI_PS_MIN_MODEM - non disponible dans cette version
  
  _modemSleepEnabled = true;
  
  Serial.println("[Power] ✅ Modem sleep activé - Réveil WiFi possible");
  Serial.printf("[Power] DTIM configuré, IP: %s\n", WiFi.localIP().toString().c_str());
}

void PowerManager::disableModemSleepMode() {
  WiFi.setSleep(false);
  // esp_wifi_set_ps(0); // WIFI_PS_NONE - non disponible dans cette version
  _modemSleepEnabled = false;
  
  Serial.println("[Power] ❌ Modem sleep désactivé");
}

bool PowerManager::isWifiWakeupAvailable() const {
  return _modemSleepEnabled && (WiFi.status() == WL_CONNECTED);
}

bool PowerManager::testDTIMCompatibility() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[Power] ⚠️ Test DTIM impossible - WiFi non connecté");
    return false;
  }
  
  unsigned long now = millis();
  if (now - _lastModemSleepTest < MODEM_SLEEP_TEST_INTERVAL) {
    Serial.println("[Power] Test DTIM récent - skip");
    return true; // On assume que ça fonctionne si test récent
  }
  
  Serial.println("[Power] 🧪 Test de compatibilité DTIM...");
  
  // Sauvegarder l'état actuel du modem sleep
  bool wasEnabled = _modemSleepEnabled;
  
  // Activer modem sleep temporairement
  enableModemSleepMode();
  
  // Attendre quelques cycles DTIM (5 secondes)
  // Note: Utilisation de vTaskDelay() car cette fonction est appelée après l'initialisation de FreeRTOS
  // Si cette fonction est appelée avant l'initialisation de FreeRTOS, utiliser delay() à la place
  vTaskDelay(pdMS_TO_TICKS(5000));
  
  // Vérifier si la connexion est maintenue
  bool stillConnected = (WiFi.status() == WL_CONNECTED);
  
  // Restaurer l'état précédent
  if (!wasEnabled) {
    disableModemSleepMode();
  }
  
  _lastModemSleepTest = now;
  
  if (stillConnected) {
    Serial.println("[Power] ✅ Test DTIM réussi - Compatible avec modem sleep");
  } else {
    Serial.println("[Power] ❌ Test DTIM échoué - Incompatible avec modem sleep");
    Serial.println("[Power] 💡 Le routeur peut ne pas supporter le DTIM correctement");
  }
  
  return stillConnected;
}

void PowerManager::setSleepMode(bool useModemSleep) {
  _useModemSleep = useModemSleep;
  
  if (useModemSleep) {
    Serial.println("[Power] 🔄 Mode de sleep changé : Modem Sleep + Light Sleep");
    // Tester la compatibilité
    testDTIMCompatibility();
  } else {
    Serial.println("[Power] 🔄 Mode de sleep changé : Light Sleep classique");
    disableModemSleepMode();
  }
}

void PowerManager::configurePowerDomainsForModemSleep() {
  // DÉSACTIVÉ : Configuration trop agressive causant des conflits
  // Le modem sleep + light sleep fonctionne AUTOMATIQUEMENT sans configuration manuelle
  // des power domains. L'ESP-IDF gère cela de manière optimale.
  
  // ANCIENNE CONFIG (causait des crashes) :
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_SLOW_MEM, ESP_PD_OPTION_ON);
  // esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_FAST_MEM, ESP_PD_OPTION_ON);
  // esp_sleep_pd_config(ESP_PD_DOMAIN_XTAL, ESP_PD_OPTION_OFF);
  // esp_sleep_pd_config(ESP_PD_DOMAIN_VDDSDIO, ESP_PD_OPTION_OFF);
  
  Serial.println("[Power] Power domains: utilisation configuration automatique ESP-IDF");
}

void PowerManager::logWakeupCause(esp_sleep_wakeup_cause_t cause) {
  switch(cause) {
    case ESP_SLEEP_WAKEUP_TIMER:
      Serial.println("[Power] ⏰ Réveil par timer");
      break;
    case ESP_SLEEP_WAKEUP_WIFI:
      Serial.println("[Power] 🌐 Réveil par activité WiFi !");
      Serial.println("[Power] 📡 Paquet réseau reçu pendant le sommeil");
      break;
    case ESP_SLEEP_WAKEUP_EXT0:
      Serial.println("[Power] 🔌 Réveil par interruption externe (EXT0)");
      break;
    case ESP_SLEEP_WAKEUP_EXT1:
      Serial.println("[Power] 🔌 Réveil par interruption externe (EXT1)");
      break;
    case ESP_SLEEP_WAKEUP_TOUCHPAD:
      Serial.println("[Power] 👆 Réveil par touchpad");
      break;
    case ESP_SLEEP_WAKEUP_ULP:
      Serial.println("[Power] 🔋 Réveil par ULP coprocessor");
      break;
    default:
      Serial.printf("[Power] ❓ Réveil par cause inconnue: %d\n", cause);
      break;
  }
}

uint32_t PowerManager::getSleptTime(uint64_t startUs) {
  // Calcul effectif du temps passé en veille
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
    
    Serial.printf("[Power] Veille: %llu us (~%u s), reste=%u us – heure: %s\n",
                  (unsigned long long)sleptUs, sleptSec, _sleepRemainderUs, 
                  getCurrentTimeString().c_str());
  }
  
  return sleptSec;
}

// Initialisation du nouveau système
void PowerManager::initModemSleep() {
  Serial.println("[Power] 🔧 Initialisation du système Modem Sleep");
  
  // Configuration par défaut : activer modem sleep si WiFi connecté
  if (WiFi.status() == WL_CONNECTED) {
    _useModemSleep = true;
    enableModemSleepMode();
    Serial.println("[Power] ✅ Modem sleep activé automatiquement (WiFi connecté)");
  } else {
    _useModemSleep = false;
    Serial.println("[Power] ⚠️ Modem sleep désactivé (WiFi non connecté)");
  }
  
  // Test de compatibilité DTIM
  if (_useModemSleep) {
    testDTIMCompatibility();
  }
}