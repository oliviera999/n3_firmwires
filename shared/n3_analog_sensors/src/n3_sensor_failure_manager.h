#pragma once
// =============================================================================
// n3_analog_sensors — Gestionnaire générique de défaillances de capteurs
// =============================================================================
// Mutualisé depuis ffp5cs (sensor_failure_manager.h/.cpp), logique identique.
// Machine d'état inter-lectures : désactivation automatique après N échecs
// consécutifs, tests de réactivation espacés, réactivation après M succès.
// Complète le filtrage intra-lecture de n3_analog_sensors (médiane/EMA) qui
// n'a aucune mémoire entre lectures.
//
// Header-only pour permettre l'injection de la macro de log PAR PROJET :
// définir N3_SENSOR_LOG_PRINTF avant l'include (ex. ffp5cs la mappe sur son
// SENSOR_LOG_PRINTF gé par profil). Défaut : Serial.printf. ⚠ La macro doit
// être LA MÊME dans toutes les TU d'un binaire (wrapper unique par firmware).
//
// Dépendance : Arduino.h (millis(), Serial par défaut). Testable nativement
// avec le mock Arduino (millis injectable).
// =============================================================================

#include <Arduino.h>

#ifndef N3_SENSOR_LOG_PRINTF
#define N3_SENSOR_LOG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#endif

/**
 * Gestionnaire générique de défaillances de capteurs.
 *
 * Encapsule la logique de désactivation/réactivation automatique pour tous les
 * capteurs. Évite les tentatives de lecture inutiles sur des capteurs absents
 * ou défaillants. Entièrement paramétré par constructeur (aucune dépendance à
 * une config projet).
 */
class SensorFailureManager {
public:
  /**
   * @param sensorName Nom du capteur pour le logging (ex: "WaterTemp")
   * @param maxFailures Échecs consécutifs avant désactivation (défaut: 10)
   * @param reactivationIntervalMs Intervalle entre tests de réactivation (défaut: 60000)
   * @param reactivationSuccessThreshold Succès consécutifs requis pour réactiver (défaut: 3)
   */
  SensorFailureManager(const char* sensorName,
                       uint8_t maxFailures = 10,
                       uint32_t reactivationIntervalMs = 60000,
                       uint8_t reactivationSuccessThreshold = 3)
    : _sensorName(sensorName),
      _maxFailures(maxFailures),
      _reactivationIntervalMs(reactivationIntervalMs),
      _reactivationSuccessThreshold(reactivationSuccessThreshold) {
  }

  /** Vérifier si le capteur est actuellement désactivé. */
  bool isDisabled() const { return _disabled; }

  /** Enregistrer un succès de lecture (réinitialise le compteur d'échecs). */
  void recordSuccess() {
    if (_consecutiveFailures > 0) {
      N3_SENSOR_LOG_PRINTF("[%s] Succès de lecture - reset compteur d'échecs (était: %d)\n",
                           _sensorName, _consecutiveFailures);
    }
    _consecutiveFailures = 0;
  }

  /** Enregistrer un échec de lecture (désactive le capteur si seuil atteint). */
  void recordFailure() {
    _consecutiveFailures++;

    // Log seulement les premiers échecs pour éviter spam
    if (_consecutiveFailures <= 3) {
      N3_SENSOR_LOG_PRINTF("[%s] Échec de lecture %d/%d\n",
                           _sensorName, _consecutiveFailures, _maxFailures);
    }

    // Désactiver si seuil atteint
    if (_consecutiveFailures >= _maxFailures && !_disableLogged) {
      _disabled = true;
      _disableLogged = true;
      N3_SENSOR_LOG_PRINTF("[%s] 🔴 Capteur désactivé après %d échecs consécutifs\n",
                           _sensorName, _maxFailures);
    }
  }

  /** true si le capteur est désactivé et qu'un test de réactivation est dû. */
  bool shouldTestReactivation() const {
    if (!_disabled) {
      return false;  // Capteur actif, pas besoin de test
    }

    uint32_t now = millis();
    // Gérer le wraparound de millis()
    if (now < _lastReactivationTestMs) {
      // Wraparound détecté, considérer qu'on peut tester
      return true;
    }

    return (now - _lastReactivationTestMs) >= _reactivationIntervalMs;
  }

  /** Succès d'un test de réactivation. @return true si le capteur est réactivé. */
  bool recordReactivationTestSuccess() {
    _lastReactivationTestMs = millis();
    _consecutiveReactivationSuccesses++;

    N3_SENSOR_LOG_PRINTF("[%s] Test réactivation: succès %d/%d\n",
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

  /** Échec d'un test de réactivation (réinitialise le compteur de succès). */
  void recordReactivationTestFailure() {
    _lastReactivationTestMs = millis();
    if (_consecutiveReactivationSuccesses > 0) {
      N3_SENSOR_LOG_PRINTF("[%s] Test réactivation échoué - reset compteur (était: %d)\n",
                           _sensorName, _consecutiveReactivationSuccesses);
    }
    _consecutiveReactivationSuccesses = 0;
  }

  /** Réactiver le capteur (réinitialise compteurs et état). */
  void reactivate() {
    _disabled = false;
    _disableLogged = false;
    _consecutiveFailures = 0;
    _consecutiveReactivationSuccesses = 0;
    N3_SENSOR_LOG_PRINTF("[%s] ✅ Capteur réactivé automatiquement\n", _sensorName);
  }

  /** Réinitialiser l'état (après reset matériel ou réactivation). */
  void reset() {
    _disabled = false;
    _disableLogged = false;
    _consecutiveFailures = 0;
    _consecutiveReactivationSuccesses = 0;
    _lastReactivationTestMs = 0;
  }

  /** Échecs consécutifs actuels. */
  uint8_t getConsecutiveFailures() const { return _consecutiveFailures; }

  /** Succès de réactivation consécutifs. */
  uint8_t getReactivationSuccesses() const { return _consecutiveReactivationSuccesses; }

private:
  const char* _sensorName;
  uint8_t _maxFailures;
  uint32_t _reactivationIntervalMs;
  uint8_t _reactivationSuccessThreshold;

  bool _disabled{false};
  bool _disableLogged{false};
  uint8_t _consecutiveFailures{0};
  uint32_t _lastReactivationTestMs{0};
  uint8_t _consecutiveReactivationSuccesses{0};
};
