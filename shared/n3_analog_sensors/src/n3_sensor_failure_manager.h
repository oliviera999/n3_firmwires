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
//
// Persistance deep sleep (additif v1.2.0) : la cohorte n3pp/msp deep-sleepe
// entre chaque lecture ; setup() recrée les objets et la RAM repart à 0, si
// bien que les compteurs d'échecs ne franchissent jamais le seuil. Deux briques
// additives (sans impact pour ffp5cs, qui ne les utilise pas) :
//   * `SensorFailureState` (POD) + `serializeState()`/`restoreState()` : export/
//     import de l'ÉTAT mutable uniquement, à stocker par le firmware en
//     `RTC_DATA_ATTR` (la lib reste sans stockage statique).
//   * cadence de réactivation PAR CYCLES DE RÉVEIL (`shouldTestReactivationCyclic`
//     + `noteWakeCycle`) : `millis()` repart à 0 à chaque réveil et ne peut pas
//     mesurer un intervalle à travers le sommeil ; les réveils étant déjà espacés
//     par la durée de sommeil, on compte les réveils, pas les millisecondes. La
//     voie `millis()` historique (`shouldTestReactivation`) reste INCHANGÉE pour
//     ffp5cs (FreeRTOS, pas de deep sleep).
// =============================================================================

#include <Arduino.h>
#include <stdint.h>

#ifndef N3_SENSOR_LOG_PRINTF
#define N3_SENSOR_LOG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
#endif

/**
 * État mutable sérialisable d'un SensorFailureManager (POD trivialement copiable).
 *
 * Ne contient QUE l'état inter-lectures qui doit survivre à un reset de la RAM
 * (deep sleep) — jamais la configuration (nom, seuils), fournie au constructeur.
 * Le firmware qui a besoin de persistance possède le stockage (ex. un membre
 * `RTC_DATA_ATTR` de type `SensorFailureState`) : la lib n'impose aucun mécanisme
 * de persistance et ne détient aucun état statique. ffp5cs n'utilise pas ce POD.
 *
 * `cyclesSinceReactivationTest` compte les RÉVEILS écoulés depuis le dernier test
 * de réactivation — utilisable à travers le deep sleep (contrairement à millis()).
 */
struct SensorFailureState {
  uint8_t  consecutiveFailures = 0;
  uint8_t  reactivationSuccesses = 0;
  uint8_t  disabled = 0;                    // 0/1
  uint8_t  disableLogged = 0;               // 0/1
  uint16_t cyclesSinceReactivationTest = 0; // réveils depuis le dernier test
};

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

  /**
   * Cadence de réactivation PAR CYCLES DE RÉVEIL (indépendante de millis()).
   *
   * À utiliser par la cohorte deep-sleep : `millis()` repart à 0 à chaque réveil
   * donc `shouldTestReactivation()` est inexploitable à travers le sommeil. Les
   * réveils étant déjà espacés par la durée de sommeil, on teste la réactivation
   * tous les `everyNWakes` réveils (défaut 1 = à chaque réveil tant que désactivé ;
   * le seuil de succès consécutifs espace ensuite la réactivation effective).
   * @param everyNWakes intervalle en nombre de réveils entre deux tests.
   */
  bool shouldTestReactivationCyclic(uint16_t everyNWakes = 1) const {
    if (!_disabled) {
      return false;
    }
    if (everyNWakes == 0) {
      everyNWakes = 1;
    }
    return _cyclesSinceReactivationTest >= everyNWakes;
  }

  /**
   * Signaler qu'un réveil s'est écoulé (à appeler une fois par réveil au réveil,
   * après restoreState). N'a d'effet que si le capteur est désactivé : avance le
   * compteur de cycles utilisé par `shouldTestReactivationCyclic()`. Saturant.
   */
  void noteWakeCycle() {
    if (_disabled && _cyclesSinceReactivationTest < 0xFFFF) {
      _cyclesSinceReactivationTest++;
    }
  }

  /** Succès d'un test de réactivation. @return true si le capteur est réactivé. */
  bool recordReactivationTestSuccess() {
    _lastReactivationTestMs = millis();
    _cyclesSinceReactivationTest = 0;  // cadence par cycles : test consommé
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
    _cyclesSinceReactivationTest = 0;  // cadence par cycles : test consommé
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
    _cyclesSinceReactivationTest = 0;
    N3_SENSOR_LOG_PRINTF("[%s] ✅ Capteur réactivé automatiquement\n", _sensorName);
  }

  /** Réinitialiser l'état (après reset matériel ou réactivation). */
  void reset() {
    _disabled = false;
    _disableLogged = false;
    _consecutiveFailures = 0;
    _consecutiveReactivationSuccesses = 0;
    _lastReactivationTestMs = 0;
    _cyclesSinceReactivationTest = 0;
  }

  /** Échecs consécutifs actuels. */
  uint8_t getConsecutiveFailures() const { return _consecutiveFailures; }

  /** Succès de réactivation consécutifs. */
  uint8_t getReactivationSuccesses() const { return _consecutiveReactivationSuccesses; }

  /** Réveils écoulés depuis le dernier test de réactivation (cadence par cycles). */
  uint16_t getCyclesSinceReactivationTest() const { return _cyclesSinceReactivationTest; }

  /**
   * Exporter l'état mutable courant (POD) pour persistance (ex. RTC_DATA_ATTR).
   * La configuration (nom, seuils) N'est PAS incluse : elle vient du constructeur.
   */
  SensorFailureState serializeState() const {
    SensorFailureState s;
    s.consecutiveFailures = _consecutiveFailures;
    s.reactivationSuccesses = _consecutiveReactivationSuccesses;
    s.disabled = _disabled ? 1 : 0;
    s.disableLogged = _disableLogged ? 1 : 0;
    s.cyclesSinceReactivationTest = _cyclesSinceReactivationTest;
    return s;
  }

  /**
   * Restaurer l'état mutable depuis un POD (ex. au réveil, depuis RTC_DATA_ATTR).
   * `_lastReactivationTestMs` est remis à 0 (millis() non pertinent à travers le
   * sommeil : la cohorte deep-sleep utilise la cadence par cycles).
   */
  void restoreState(const SensorFailureState& s) {
    _consecutiveFailures = s.consecutiveFailures;
    _consecutiveReactivationSuccesses = s.reactivationSuccesses;
    _disabled = (s.disabled != 0);
    _disableLogged = (s.disableLogged != 0);
    _cyclesSinceReactivationTest = s.cyclesSinceReactivationTest;
    _lastReactivationTestMs = 0;
  }

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
  uint16_t _cyclesSinceReactivationTest{0};  // cadence de réactivation par cycles (deep sleep)
};
