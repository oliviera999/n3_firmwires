#include "sensor_failure_manager.h"
#include "config.h"

SensorFailureManager::SensorFailureManager(const char* sensorName,
                                           uint8_t maxFailures,
                                           uint32_t reactivationIntervalMs,
                                           uint8_t reactivationSuccessThreshold)
  : _sensorName(sensorName),
    _maxFailures(maxFailures),
    _reactivationIntervalMs(reactivationIntervalMs),
    _reactivationSuccessThreshold(reactivationSuccessThreshold) {
}

void SensorFailureManager::recordSuccess() {
  if (_consecutiveFailures > 0) {
    SENSOR_LOG_PRINTF("[%s] Succès de lecture - reset compteur d'échecs (était: %d)\n", 
                      _sensorName, _consecutiveFailures);
  }
  _consecutiveFailures = 0;
}

void SensorFailureManager::recordFailure() {
  _consecutiveFailures++;
  
  // Log seulement les premiers échecs pour éviter spam
  if (_consecutiveFailures <= 3) {
    SENSOR_LOG_PRINTF("[%s] Échec de lecture %d/%d\n", 
                      _sensorName, _consecutiveFailures, _maxFailures);
  }
  
  // Désactiver si seuil atteint
  if (_consecutiveFailures >= _maxFailures && !_disableLogged) {
    _disabled = true;
    _disableLogged = true;
    SENSOR_LOG_PRINTF("[%s] 🔴 Capteur désactivé après %d échecs consécutifs\n", 
                      _sensorName, _maxFailures);
  }
}

bool SensorFailureManager::shouldTestReactivation() const {
  if (!_disabled) {
    return false; // Capteur actif, pas besoin de test
  }
  
  uint32_t now = millis();
  // Gérer le wraparound de millis()
  if (now < _lastReactivationTestMs) {
    // Wraparound détecté, considérer qu'on peut tester
    return true;
  }
  
  return (now - _lastReactivationTestMs) >= _reactivationIntervalMs;
}

bool SensorFailureManager::recordReactivationTestSuccess() {
  _lastReactivationTestMs = millis();
  _consecutiveReactivationSuccesses++;
  
  SENSOR_LOG_PRINTF("[%s] Test réactivation: succès %d/%d\n", 
                    _sensorName, 
                    _consecutiveReactivationSuccesses, 
                    _reactivationSuccessThreshold);
  
  if (_consecutiveReactivationSuccesses >= _reactivationSuccessThreshold) {
    // Réactiver le capteur
    reactivate();
    return true;
  }
  
  return false;
}

void SensorFailureManager::recordReactivationTestFailure() {
  _lastReactivationTestMs = millis();
  if (_consecutiveReactivationSuccesses > 0) {
    SENSOR_LOG_PRINTF("[%s] Test réactivation échoué - reset compteur (était: %d)\n", 
                      _sensorName, _consecutiveReactivationSuccesses);
  }
  _consecutiveReactivationSuccesses = 0;
}

void SensorFailureManager::reactivate() {
  _disabled = false;
  _disableLogged = false;
  _consecutiveFailures = 0;
  _consecutiveReactivationSuccesses = 0;
  SENSOR_LOG_PRINTF("[%s] ✅ Capteur réactivé automatiquement\n", _sensorName);
}

void SensorFailureManager::reset() {
  _disabled = false;
  _disableLogged = false;
  _consecutiveFailures = 0;
  _consecutiveReactivationSuccesses = 0;
  _lastReactivationTestMs = 0;
}
