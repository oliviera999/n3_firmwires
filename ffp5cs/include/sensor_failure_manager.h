#pragma once
#include <Arduino.h>
#include "config.h"

/**
 * Gestionnaire générique de défaillances de capteurs
 * 
 * Encapsule la logique de désactivation/réactivation automatique pour tous les capteurs.
 * Permet d'éviter les tentatives de lecture inutiles sur des capteurs absents ou défaillants.
 */
class SensorFailureManager {
public:
  /**
   * Constructeur
   * @param sensorName Nom du capteur pour le logging (ex: "WaterTemp", "Ultrasonic-Aqua")
   * @param maxFailures Nombre maximum d'échecs consécutifs avant désactivation (défaut: 10)
   * @param reactivationIntervalMs Intervalle entre tests de réactivation en ms
   *                               (défaut: 60000 = 60s)
   * @param reactivationSuccessThreshold Nombre de succès consécutifs requis
   *                                     (défaut: 3)
   */
  SensorFailureManager(const char* sensorName,
                       uint8_t maxFailures = 10,
                       uint32_t reactivationIntervalMs = 60000,
                       uint8_t reactivationSuccessThreshold = 3);
  
  /**
   * Vérifier si le capteur est actuellement désactivé
   */
  bool isDisabled() const { return _disabled; }
  
  /**
   * Enregistrer un succès de lecture
   * Réinitialise le compteur d'échecs consécutifs
   */
  void recordSuccess();
  
  /**
   * Enregistrer un échec de lecture
   * Incrémente le compteur d'échecs et désactive le capteur si le seuil est atteint
   */
  void recordFailure();
  
  /**
   * Vérifier si un test de réactivation doit être effectué maintenant
   * @return true si le capteur est désactivé et qu'un test doit être effectué
   */
  bool shouldTestReactivation() const;
  
  /**
   * Enregistrer un succès de test de réactivation
   * @return true si le capteur doit être réactivé (seuil de succès atteint)
   */
  bool recordReactivationTestSuccess();
  
  /**
   * Enregistrer un échec de test de réactivation
   * Réinitialise le compteur de succès de réactivation
   */
  void recordReactivationTestFailure();
  
  /**
   * Réactiver le capteur manuellement
   * Réinitialise tous les compteurs et l'état
   */
  void reactivate();
  
  /**
   * Réinitialiser l'état (après reset matériel ou réactivation)
   */
  void reset();
  
  /**
   * Obtenir le nombre d'échecs consécutifs actuels
   */
  uint8_t getConsecutiveFailures() const { return _consecutiveFailures; }
  
  /**
   * Obtenir le nombre de succès de réactivation consécutifs
   */
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
