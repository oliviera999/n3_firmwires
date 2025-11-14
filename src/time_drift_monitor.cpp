#include "time_drift_monitor.h"
#include "nvs_manager.h" // v11.109

#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <Arduino.h>

#include "project_config.h"
#include "power.h"


TimeDriftMonitor::TimeDriftMonitor()
  : _driftPPM(0.0f),
    _driftSeconds(0.0f),
    _lastSyncTime(0),
    _lastSyncMillis(0),
    _driftCalculated(false),
    _syncInterval(DEFAULT_SYNC_INTERVAL),
    _driftThresholdPPM(DEFAULT_DRIFT_THRESHOLD_PPM),
    _lastNtpEpoch(0),
    _lastNtpMillis(0),
    _lastLocalEpoch(0),
    _lastLocalMillis(0),
    _previousNtpEpoch(0),
    _previousNtpMillis(0),
    _previousLocalEpoch(0),
    _previousLocalMillis(0),
    _powerManager(nullptr) {

}

void TimeDriftMonitor::attachPowerManager(PowerManager* manager) {
  _powerManager = manager;
}

void TimeDriftMonitor::begin() {
  LOG_DRIFT(LogConfig::LOG_INFO, "Initialisation du moniteur de dérive temporelle");

  // En production : garder les données NVS existantes
  // (l'effacement était seulement pour les tests)
  loadFromNVS();

  // Configuration NTP avec offset GMT en secondes (7200s = 2h)
  configTime(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);

  // Configuration de production : sync toutes les heures
  LOG_DRIFT(LogConfig::LOG_INFO, "Intervalle de sync: %lu ms (%.1f heures)", 
            _syncInterval, _syncInterval / 3600000.0f);
  LOG_DRIFT(LogConfig::LOG_INFO, "Seuil d'alerte: %.1f PPM", _driftThresholdPPM);

  if (_driftCalculated) {
    LOG_DRIFT(LogConfig::LOG_INFO, "Dérive précédente: %.2f PPM (%.2f secondes)", 
              _driftPPM, _driftSeconds);
  } else {
    LOG_DRIFT(LogConfig::LOG_INFO, "Aucune dérive calculée précédemment");
  }
  
  // Informations sur l'état temporel actuel
  time_t currentEpoch = time(nullptr);
  LOG_TIME(LogConfig::LOG_INFO, "État temporel actuel: %s (epoch: %lu)", 
           getCurrentTimeString(currentEpoch).c_str(), currentEpoch);
}


void TimeDriftMonitor::update() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long nowMs = millis();
  if (_lastSyncMillis == 0 || nowMs - _lastSyncMillis > _syncInterval) {
    syncWithNTP();
  }
}

void TimeDriftMonitor::syncWithNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    LOG_DRIFT(LogConfig::LOG_WARN, "Pas de WiFi - synchronisation NTP impossible");
    return;
  }

  timeval localBefore{};
  gettimeofday(&localBefore, nullptr);
  const time_t localBeforeEpoch = localBefore.tv_sec;
  const unsigned long millisBeforeSync = millis();

  LOG_DRIFT(LogConfig::LOG_INFO, "Début synchronisation NTP - Heure locale avant: %s (epoch: %lu)", 
            getCurrentTimeString(localBeforeEpoch).c_str(), localBeforeEpoch);

  bool syncSuccess = false;
  for (int attempt = 0; attempt < MAX_NTP_RETRIES && !syncSuccess; ++attempt) {
    if (attempt > 0) {
      LOG_DRIFT(LogConfig::LOG_INFO, "Tentative NTP %d/%d...", attempt + 1, MAX_NTP_RETRIES);
      delay(NTP_RETRY_DELAY_MS * attempt);
    }
    syncSuccess = performNTPSync();
  }

  if (!syncSuccess) {
    LOG_DRIFT(LogConfig::LOG_ERROR, "Échec de synchronisation NTP après toutes les tentatives");
    return;
  }

  const time_t ntpEpoch = getNTPTime();
  const unsigned long syncMillis = millis();
  const time_t localAfterEpoch = time(nullptr);
  
  LOG_DRIFT(LogConfig::LOG_INFO, "Synchronisation NTP réussie - Heure NTP: %s (epoch: %lu)", 
            getCurrentTimeString(ntpEpoch).c_str(), ntpEpoch);

  if (ntpEpoch <= 0 || ntpEpoch < SleepConfig::EPOCH_MIN_VALID) {
    LOG_DRIFT(LogConfig::LOG_ERROR, "Temps NTP invalide: %ld", ntpEpoch);
    return;
  }

  if (_lastNtpEpoch > 0) {
    _previousNtpEpoch = _lastNtpEpoch;
    _previousNtpMillis = _lastNtpMillis;
    _previousLocalEpoch = _lastLocalEpoch;
    _previousLocalMillis = _lastLocalMillis;
    LOG_DRIFT(LogConfig::LOG_DEBUG, "Variables précédentes sauvegardées: NTP=%ld, Local=%ld",
              _previousNtpEpoch, _previousLocalEpoch);

    if (localBeforeEpoch >= SleepConfig::EPOCH_MIN_VALID) {
      calculateDrift(ntpEpoch, localBeforeEpoch, millisBeforeSync);
    } else {
      LOG_DRIFT(LogConfig::LOG_WARN, "Horloge locale non valide avant sync, calcul de dérive ignoré");
      _driftCalculated = false;
      _driftPPM = 0.0f;
      _driftSeconds = 0.0f;
    }
  } else {
    LOG_DRIFT(LogConfig::LOG_INFO, "Première synchronisation NTP - initialisation des références");
    _driftCalculated = false;
    _driftPPM = 0.0f;
    _driftSeconds = 0.0f;
  }

  _lastNtpEpoch = ntpEpoch;
  _lastNtpMillis = syncMillis;
  _lastLocalEpoch = localAfterEpoch;
  _lastLocalMillis = syncMillis;
  _lastSyncMillis = syncMillis;
  _lastSyncTime = localAfterEpoch;

  if (_powerManager) {
    _powerManager->onExternalNtpSync(ntpEpoch, syncMillis);
  }

  saveToNVS();

  LOG_DRIFT(LogConfig::LOG_INFO, "Sync NTP réussie: %s",
            getCurrentTimeString(ntpEpoch).c_str());
  printDriftInfo();
}

bool TimeDriftMonitor::performNTPSync() {

  // Configuration NTP avec offset GMT en secondes (7200s = 2h)
  configTime(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);

  // Attendre la synchronisation

  struct tm timeinfo;

  int attempts = 0;

  const int maxAttempts = 10;

  while (attempts < maxAttempts) {

    if (getLocalTime(&timeinfo)) {

      return true;

    }

    delay(1000);

    attempts++;

  }

  return false;

}

time_t TimeDriftMonitor::getNTPTime() {

  struct tm timeinfo;

  if (getLocalTime(&timeinfo)) {

    return mktime(&timeinfo);

  }

  return 0;

}

void TimeDriftMonitor::calculateDrift(time_t newNtpEpoch, time_t localEpochBeforeSync, unsigned long localMillisBeforeSync) {
  Serial.println("[TimeDrift] === DÉBUT CALCUL DE DÉRIVE ===");
  Serial.printf("[TimeDrift] Variables d'entrée:\n");
  Serial.printf("  - _previousNtpEpoch: %ld\n", _previousNtpEpoch);
  Serial.printf("  - _previousLocalEpoch: %ld\n", _previousLocalEpoch);
  Serial.printf("  - newNtpEpoch: %ld\n", newNtpEpoch);
  Serial.printf("  - localEpochBeforeSync: %ld\n", localEpochBeforeSync);
  Serial.printf("  - _previousNtpMillis: %lu\n", _previousNtpMillis);
  Serial.printf("  - _previousLocalMillis: %lu\n", _previousLocalMillis);
  Serial.printf("  - localMillisBeforeSync: %lu\n", localMillisBeforeSync);

  if (_previousNtpEpoch == 0 || _previousLocalMillis == 0) {
    Serial.println("[TimeDrift] ❌ Données insuffisantes pour calculer la dérive");
    Serial.printf("[TimeDrift] _previousNtpEpoch=%ld, _previousLocalMillis=%lu\n",
                  _previousNtpEpoch, _previousLocalMillis);
    return;
  }

  // Garde-fous robustes: reboot/wrap millis, fenêtre trop courte/longue, NTP non monotone
  if (localMillisBeforeSync < _previousLocalMillis) {
    Serial.println("[TimeDrift] ❌ Reboot ou wrap millis détecté, dérive ignorée");
    _driftCalculated = false;
    _driftPPM = 0.0f;
    _driftSeconds = 0.0f;
    return;
  }

  const unsigned long localMillisElapsed = localMillisBeforeSync - _previousLocalMillis;
  const double localElapsedSeconds = static_cast<double>(localMillisElapsed) / 1000.0;
  const double ntpElapsedSeconds = difftime(newNtpEpoch, _previousNtpEpoch);
  const double localEpochElapsed = difftime(localEpochBeforeSync, _previousLocalEpoch);

  Serial.printf("[TimeDrift] Calculs intermédiaires:\n");
  Serial.printf("  - localMillisElapsed: %lu ms\n", localMillisElapsed);
  Serial.printf("  - localEpochElapsed: %.0f s\n", localEpochElapsed);
  Serial.printf("  - ntpEpochElapsed: %.0f s\n", ntpElapsedSeconds);

  if (localElapsedSeconds <= 0.0) {
    Serial.println("[TimeDrift] ❌ Durée locale invalide, dérive ignorée");
    return;
  }

  if (ntpElapsedSeconds <= 0.0) {
    Serial.println("[TimeDrift] ❌ NTP non monotone, dérive ignorée");
    _driftCalculated = false;
    _driftPPM = 0.0f;
    _driftSeconds = 0.0f;
    return;
  }

  // Fenêtre d'observation plausible: 60 s à 4 h
  const double MIN_WINDOW_SECONDS = 60.0;
  const double MAX_WINDOW_SECONDS = 4.0 * 3600.0;
  if (localElapsedSeconds < MIN_WINDOW_SECONDS || ntpElapsedSeconds < MIN_WINDOW_SECONDS ||
      localElapsedSeconds > MAX_WINDOW_SECONDS || ntpElapsedSeconds > MAX_WINDOW_SECONDS) {
    Serial.printf("[TimeDrift] ❌ Fenêtre invalide (local=%.0fs, ntp=%.0fs), dérive ignorée\n",
                  localElapsedSeconds, ntpElapsedSeconds);
    _driftCalculated = false;
    _driftPPM = 0.0f;
    _driftSeconds = 0.0f;
    return;
  }

  _driftSeconds = static_cast<float>(localElapsedSeconds - ntpElapsedSeconds);
  _driftPPM = static_cast<float>((_driftSeconds * 1000000.0) / localElapsedSeconds);

  _driftPPM = constrain(_driftPPM, -10000.0f, 10000.0f);
  _driftSeconds = constrain(_driftSeconds, -86400.0f, 86400.0f);
  _driftCalculated = true;

  Serial.printf("[TimeDrift] Résultats finaux:\n");
  Serial.printf("  - Dérive: %.2f secondes (%.2f PPM)\n", _driftSeconds, _driftPPM);

  if (abs(_driftPPM) > _driftThresholdPPM) {
    Serial.printf("[TimeDrift] ⚠️ ALERTE: Dérive élevée détectée! %.2f PPM (seuil: %.2f PPM)\n",
                  _driftPPM, _driftThresholdPPM);
  }

  if (_powerManager) {
    _powerManager->setMeasuredDrift(_driftPPM, _driftSeconds);
  }

  Serial.println("[TimeDrift] === FIN CALCUL DE DÉRIVE ===");
}

String TimeDriftMonitor::getDriftStatusString() const {

  if (!_driftCalculated) {

    return "Non calculée";

  }

  String status = String("Dérive: ") + String(_driftPPM, 2) + " PPM";

  if (abs(_driftPPM) > _driftThresholdPPM) {

    status += " (⚠️ ÉLEVÉE)";

  } else {

    status += " (✓ Normale)";

  }

  return status;

}

void TimeDriftMonitor::printDriftInfo() const {

  Serial.println("=== INFORMATIONS DE DÉRIVE TEMPORELLE ===");

  Serial.printf("Dérive: %.2f PPM (%.2f secondes)\n", _driftPPM, _driftSeconds);

  Serial.printf("Dernière sync: %s\n", getCurrentTimeString(_lastSyncTime).c_str());

  Serial.printf("Statut: %s\n", getDriftStatusString().c_str());

  Serial.printf("Seuil d'alerte: %.2f PPM\n", _driftThresholdPPM);

  Serial.println("=========================================");

}

String TimeDriftMonitor::generateDriftReport() const {

  String report = "";

  report.reserve(512);

  report += "=== RAPPORT DE DÉRIVE TEMPORELLE ===\n";

  report += "Timestamp: " + getCurrentTimeString(time(nullptr)) + "\n";

  report += "Dérive: " + String(_driftPPM, 2) + " PPM\n";

  report += "Dérive: " + String(_driftSeconds, 2) + " secondes\n";

  report += "Dernière sync NTP: " + getCurrentTimeString(_lastSyncTime) + "\n";

  report += "Statut: " + getDriftStatusString() + "\n";

  report += "Seuil d'alerte: " + String(_driftThresholdPPM, 1) + " PPM\n";

  if (abs(_driftPPM) > _driftThresholdPPM) {

    report += "\n⚠️ ATTENTION: Dérive élevée détectée!\n";

    report += "L'horloge de l'ESP32 dérive de manière significative.\n";

    report += "Considérer un redémarrage ou une synchronisation plus fréquente.\n";

  } else {

    report += "\n✓ Dérive dans les limites normales.\n";

  }

  report += "=====================================\n";

  return report;

}

void TimeDriftMonitor::saveToNVS() {
  // Données de dérive
  g_nvsManager.saveFloat(NVS_NAMESPACES::TIME, "drift_driftPPM", _driftPPM);
  g_nvsManager.saveFloat(NVS_NAMESPACES::TIME, "drift_driftSeconds", _driftSeconds);
  g_nvsManager.saveBool(NVS_NAMESPACES::TIME, "drift_driftCalculated", _driftCalculated);

  // Configuration
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_syncInterval", _syncInterval);
  g_nvsManager.saveFloat(NVS_NAMESPACES::TIME, "drift_driftThrPPM", _driftThresholdPPM);
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_lastSyncTime", static_cast<unsigned long>(_lastSyncTime));

  // Variables de suivi temporel
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_lastNtpEpoch", static_cast<unsigned long>(_lastNtpEpoch));
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_lastLocalEpoch", static_cast<unsigned long>(_lastLocalEpoch));

  // Variables de référence précédente
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_prevNtpEpoch", static_cast<unsigned long>(_previousNtpEpoch));
  g_nvsManager.saveULong(NVS_NAMESPACES::TIME, "drift_prevLocalEpo", static_cast<unsigned long>(_previousLocalEpoch));

  Serial.println("[TimeDrift] Données sauvegardées en NVS");
}

void TimeDriftMonitor::loadFromNVS() {
  // Données de dérive
  g_nvsManager.loadFloat(NVS_NAMESPACES::TIME, "drift_driftPPM", _driftPPM, 0.0f);
  g_nvsManager.loadFloat(NVS_NAMESPACES::TIME, "drift_driftSeconds", _driftSeconds, 0.0f);
  g_nvsManager.loadBool(NVS_NAMESPACES::TIME, "drift_driftCalculated", _driftCalculated, false);

  // Configuration
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_syncInterval", _syncInterval, DEFAULT_SYNC_INTERVAL);
  g_nvsManager.loadFloat(NVS_NAMESPACES::TIME, "drift_driftThrPPM", _driftThresholdPPM, DEFAULT_DRIFT_THRESHOLD_PPM);
  unsigned long storedSyncTime;
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_lastSyncTime", storedSyncTime, 0);

  // Variables de suivi temporel
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_lastNtpEpoch", reinterpret_cast<unsigned long&>(_lastNtpEpoch), 0);
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_lastLocalEpoch", reinterpret_cast<unsigned long&>(_lastLocalEpoch), 0);

  // Variables de référence précédente
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_prevNtpEpoch", reinterpret_cast<unsigned long&>(_previousNtpEpoch), 0);
  g_nvsManager.loadULong(NVS_NAMESPACES::TIME, "drift_prevLocalEpo", reinterpret_cast<unsigned long&>(_previousLocalEpoch), 0);

  // Réinitialiser les compteurs millis() non persistants au boot
  _lastSyncMillis = 0;
  _lastNtpMillis = 0;
  _lastLocalMillis = 0;
  _previousNtpMillis = 0;
  _previousLocalMillis = 0;

  // Nettoyage one-shot des anciennes clés NVS (millis() + clés trop longues v11.10)
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_lastSyncMillis");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_lastNtpMillis");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_lastLocalMillis");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_previousNtpMillis");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_previousLocalMillis");
  // v11.10: Supprimer anciennes clés trop longues
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_driftThresholdPPM");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_previousNtpEpoch");
  g_nvsManager.removeKey(NVS_NAMESPACES::TIME, "drift_previousLocalEpoch");

  if (storedSyncTime >= SleepConfig::EPOCH_MIN_VALID && storedSyncTime <= SleepConfig::EPOCH_MAX_VALID) {
    _lastSyncTime = static_cast<time_t>(storedSyncTime);
  } else {
    _lastSyncTime = 0;
  }

  Serial.println("[TimeDrift] Données chargées depuis NVS");

  // Validation des données chargées
  if (_driftCalculated && (_lastNtpEpoch == 0 || _lastLocalEpoch == 0)) {
    Serial.println("[TimeDrift] ⚠️ Données NVS corrompues, réinitialisation");
    _driftCalculated = false;
    _driftPPM = 0.0f;
    _driftSeconds = 0.0f;
  }
}

String TimeDriftMonitor::getCurrentTimeString(time_t epoch) const {

  if (epoch == 0) {

    epoch = time(nullptr);

  }

  struct tm timeinfo;

  if (!localtime_r(&epoch, &timeinfo)) {

    return String("00:00:00 01/01/1970");

  }

  char buffer[25];

  strftime(buffer, sizeof(buffer), "%H:%M:%S %d/%m/%Y", &timeinfo);

  return String(buffer);

}

