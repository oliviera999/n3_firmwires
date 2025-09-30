#include "time_drift_monitor.h"
#include <WiFi.h>
#include <time.h>
#include <Arduino.h>
#include "project_config.h"

const char* TimeDriftMonitor::NVS_NAMESPACE = "timeDrift";

TimeDriftMonitor::TimeDriftMonitor() 
  : _driftPPM(0.0f), _driftSeconds(0.0f), _lastSyncTime(0), _driftCalculated(false),
    _syncInterval(DEFAULT_SYNC_INTERVAL), _driftThresholdPPM(DEFAULT_DRIFT_THRESHOLD_PPM),
    _lastNtpEpoch(0), _lastNtpMillis(0), _lastLocalEpoch(0), _lastLocalMillis(0),
    _previousNtpEpoch(0), _previousNtpMillis(0), _previousLocalEpoch(0), _previousLocalMillis(0) {
}

void TimeDriftMonitor::begin() {
  Serial.println("[TimeDrift] Initialisation du moniteur de dérive temporelle");
  
  // En production : garder les données NVS existantes
  // (l'effacement était seulement pour les tests)
  
  // Charger les données depuis NVS (maintenant vides)
  loadFromNVS();
  
  // Configuration NTP par défaut
  configTime(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);
  
  // Configuration de production : sync toutes les heures
  // _syncInterval reste à la valeur par défaut (1 heure)
  
  Serial.printf("[TimeDrift] Intervalle de sync: %lu ms (%.1f heures)\n", 
                _syncInterval, _syncInterval / 3600000.0f);
  Serial.printf("[TimeDrift] Seuil d'alerte: %.1f PPM\n", _driftThresholdPPM);
  
  if (_driftCalculated) {
    Serial.printf("[TimeDrift] Dérive précédente: %.2f PPM (%.2f secondes)\n", 
                  _driftPPM, _driftSeconds);
  } else {
    Serial.println("[TimeDrift] Aucune dérive calculée précédemment");
  }
}

void TimeDriftMonitor::update() {
  // Vérifier si on doit faire une synchronisation NTP
  if (WiFi.status() == WL_CONNECTED && 
      (millis() - _lastSyncTime > _syncInterval || _lastSyncTime == 0)) {
    syncWithNTP();
  }
}

void TimeDriftMonitor::syncWithNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[TimeDrift] Pas de WiFi - synchronisation NTP impossible");
    return;
  }
  
  Serial.println("[TimeDrift] Synchronisation NTP en cours...");
  
  // Effectuer la synchronisation NTP avec retry
  bool syncSuccess = false;
  for (int attempt = 0; attempt < MAX_NTP_RETRIES && !syncSuccess; attempt++) {
    if (attempt > 0) {
      Serial.printf("[TimeDrift] Tentative NTP %d/%d...\n", attempt + 1, MAX_NTP_RETRIES);
      delay(NTP_RETRY_DELAY_MS * attempt); // Backoff exponentiel
    }
    syncSuccess = performNTPSync();
  }
  
  if (syncSuccess) {
    time_t ntpEpoch = getNTPTime();
    unsigned long currentMillis = millis();
    
    if (ntpEpoch > 0 && ntpEpoch > 1600000000) { // Validation basique (après 2020)
      // Sauvegarder l'état précédent pour le calcul de dérive (si des données existent)
      if (_lastNtpEpoch > 0) {
        _previousNtpEpoch = _lastNtpEpoch;
        _previousNtpMillis = _lastNtpMillis;
        _previousLocalEpoch = _lastLocalEpoch;
        _previousLocalMillis = _lastLocalMillis;
        Serial.printf("[TimeDrift] Variables précédentes sauvegardées: NTP=%ld, Local=%ld\n", 
                      _previousNtpEpoch, _previousLocalEpoch);
        
        // Calculer la dérive avec les nouvelles valeurs
        time_t currentLocalTime = time(nullptr);
        Serial.printf("[TimeDrift] Calcul de dérive demandé: prevNTP=%ld, currNTP=%ld, localTime=%ld\n", 
                      _previousNtpEpoch, ntpEpoch, currentLocalTime);
        
        // Vérifier que l'horloge locale est synchronisée
        if (currentLocalTime > 0 && currentLocalTime > 1600000000) {
          calculateDrift(ntpEpoch, currentLocalTime, currentMillis);
        } else {
          Serial.println("[TimeDrift] ⚠️ Horloge locale non synchronisée, calcul de dérive reporté");
        }
      } else {
        Serial.println("[TimeDrift] Première synchronisation NTP - initialisation des références");
        // Pour la première sync, on initialise juste les références sans calculer de dérive
        _driftCalculated = false;
        _driftPPM = 0.0f;
        _driftSeconds = 0.0f;
      }
      
      // Mettre à jour les variables de suivi APRÈS le calcul de dérive
      _lastNtpEpoch = ntpEpoch;
      _lastNtpMillis = currentMillis;
      _lastLocalEpoch = time(nullptr);
      _lastLocalMillis = currentMillis;
      _lastSyncTime = currentMillis;
      
      // Marquer qu'on a maintenant des données de dérive
      _driftCalculated = true;
      
      // Sauvegarder en NVS
      saveToNVS();
      
      Serial.printf("[TimeDrift] Sync NTP réussie: %s\n", 
                    getCurrentTimeString(ntpEpoch).c_str());
      
      // Afficher les informations de dérive
      printDriftInfo();
    } else {
      Serial.printf("[TimeDrift] Temps NTP invalide: %ld\n", ntpEpoch);
    }
  } else {
    Serial.println("[TimeDrift] Échec de synchronisation NTP après toutes les tentatives");
  }
}

bool TimeDriftMonitor::performNTPSync() {
  // Configuration NTP
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

void TimeDriftMonitor::calculateDrift(time_t newNtpEpoch, time_t newLocalEpoch, unsigned long newLocalMillis) {
  Serial.println("[TimeDrift] === DÉBUT CALCUL DE DÉRIVE ===");
  Serial.printf("[TimeDrift] Variables d'entrée:\n");
  Serial.printf("  - _previousNtpEpoch: %ld\n", _previousNtpEpoch);
  Serial.printf("  - _previousLocalEpoch: %ld\n", _previousLocalEpoch);
  Serial.printf("  - newNtpEpoch: %ld\n", newNtpEpoch);
  Serial.printf("  - newLocalEpoch: %ld\n", newLocalEpoch);
  Serial.printf("  - _previousNtpMillis: %lu\n", _previousNtpMillis);
  Serial.printf("  - _lastNtpMillis: %lu\n", _lastNtpMillis);
  Serial.printf("  - _previousLocalMillis: %lu\n", _previousLocalMillis);
  Serial.printf("  - newLocalMillis: %lu\n", newLocalMillis);
  
  if (_previousNtpEpoch == 0 || _previousLocalEpoch == 0) {
    Serial.println("[TimeDrift] ❌ Données insuffisantes pour calculer la dérive");
    Serial.printf("[TimeDrift] _previousNtpEpoch=%ld, _previousLocalEpoch=%ld\n", 
                  _previousNtpEpoch, _previousLocalEpoch);
    return;
  }
  
  // Calculer le temps écoulé selon l'horloge locale (en millisecondes)
  unsigned long localMillisElapsed = newLocalMillis - _previousLocalMillis;
  time_t localEpochElapsed = newLocalEpoch - _previousLocalEpoch;
  
  // Calculer le temps écoulé selon NTP (en secondes)
  time_t ntpEpochElapsed = newNtpEpoch - _previousNtpEpoch;
  
  Serial.printf("[TimeDrift] Calculs intermédiaires:\n");
  Serial.printf("  - localMillisElapsed: %lu ms\n", localMillisElapsed);
  Serial.printf("  - localEpochElapsed: %ld s\n", localEpochElapsed);
  Serial.printf("  - ntpEpochElapsed: %ld s\n", ntpEpochElapsed);
  
  // Calculer la dérive en secondes
  _driftSeconds = (float)localEpochElapsed - (float)ntpEpochElapsed;
  
  // Convertir en PPM (parties par million)
  if (localMillisElapsed > 0) {
    float elapsedSeconds = localMillisElapsed / 1000.0f;
    _driftPPM = (_driftSeconds * 1000000.0f) / elapsedSeconds;
  } else {
    _driftPPM = 0.0f;
  }
  
  // Limiter les valeurs extrêmes pour éviter les calculs aberrants
  _driftPPM = constrain(_driftPPM, -10000.0f, 10000.0f);
  _driftSeconds = constrain(_driftSeconds, -86400.0f, 86400.0f);
  
  Serial.printf("[TimeDrift] Résultats finaux:\n");
  Serial.printf("  - Dérive: %.2f secondes (%.2f PPM)\n", _driftSeconds, _driftPPM);
  
  // Vérifier le seuil d'alerte
  if (abs(_driftPPM) > _driftThresholdPPM) {
    Serial.printf("[TimeDrift] ⚠️ ALERTE: Dérive élevée détectée! %.2f PPM (seuil: %.2f PPM)\n", 
                  _driftPPM, _driftThresholdPPM);
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
  _preferences.begin(NVS_NAMESPACE, false);
  
  // Données de dérive
  _preferences.putFloat("driftPPM", _driftPPM);
  _preferences.putFloat("driftSeconds", _driftSeconds);
  _preferences.putBool("driftCalculated", _driftCalculated);
  
  // Configuration
  _preferences.putULong("syncInterval", _syncInterval);
  _preferences.putFloat("driftThresholdPPM", _driftThresholdPPM);
  _preferences.putULong("lastSyncTime", _lastSyncTime);
  
  // Variables de suivi temporel (nécessaires pour la persistance)
  _preferences.putULong("lastNtpEpoch", (unsigned long)_lastNtpEpoch);
  _preferences.putULong("lastNtpMillis", _lastNtpMillis);
  _preferences.putULong("lastLocalEpoch", (unsigned long)_lastLocalEpoch);
  _preferences.putULong("lastLocalMillis", _lastLocalMillis);
  
  // Variables de référence précédente
  _preferences.putULong("previousNtpEpoch", (unsigned long)_previousNtpEpoch);
  _preferences.putULong("previousNtpMillis", _previousNtpMillis);
  _preferences.putULong("previousLocalEpoch", (unsigned long)_previousLocalEpoch);
  _preferences.putULong("previousLocalMillis", _previousLocalMillis);
  
  _preferences.end();
  
  Serial.println("[TimeDrift] Données sauvegardées en NVS");
}

void TimeDriftMonitor::loadFromNVS() {
  _preferences.begin(NVS_NAMESPACE, true);
  
  // Données de dérive
  _driftPPM = _preferences.getFloat("driftPPM", 0.0f);
  _driftSeconds = _preferences.getFloat("driftSeconds", 0.0f);
  _driftCalculated = _preferences.getBool("driftCalculated", false);
  
  // Configuration
  _syncInterval = _preferences.getULong("syncInterval", DEFAULT_SYNC_INTERVAL);
  _driftThresholdPPM = _preferences.getFloat("driftThresholdPPM", DEFAULT_DRIFT_THRESHOLD_PPM);
  _lastSyncTime = _preferences.getULong("lastSyncTime", 0);
  
  // Variables de suivi temporel
  _lastNtpEpoch = (time_t)_preferences.getULong("lastNtpEpoch", 0);
  _lastNtpMillis = _preferences.getULong("lastNtpMillis", 0);
  _lastLocalEpoch = (time_t)_preferences.getULong("lastLocalEpoch", 0);
  _lastLocalMillis = _preferences.getULong("lastLocalMillis", 0);
  
  // Variables de référence précédente
  _previousNtpEpoch = (time_t)_preferences.getULong("previousNtpEpoch", 0);
  _previousNtpMillis = _preferences.getULong("previousNtpMillis", 0);
  _previousLocalEpoch = (time_t)_preferences.getULong("previousLocalEpoch", 0);
  _previousLocalMillis = _preferences.getULong("previousLocalMillis", 0);
  
  _preferences.end();
  
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
