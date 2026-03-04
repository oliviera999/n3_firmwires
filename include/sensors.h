#pragma once
#include <Arduino.h>
#if defined(USE_AIR_SENSOR_AUTO)
#include <Adafruit_BME280.h>
#include <DHT.h>
#elif defined(USE_AIR_SENSOR_BME280)
#include <Adafruit_BME280.h>
#else
#include <DHT.h>
#endif
#include <DallasTemperature.h>
#include "pins.h"
#include "config.h"
#include "sensor_failure_manager.h"

#if (defined(USE_AIR_SENSOR_AUTO) || defined(USE_AIR_SENSOR_BME280)) && defined(BOARD_WROOM)
#error "BME280 / USE_AIR_SENSOR_AUTO are only supported on BOARD_S3 (S3 envs)"
#endif

class UltrasonicManager {
 public:
  UltrasonicManager(int pinTrigEcho, const char* sensorName = "Ultrasonic");
  // Renvoie la distance (cm), 0 si invalide
  uint16_t readFiltered(uint8_t samples = 5);
  uint16_t readAdvancedFiltered(); // Nouvelle méthode avec filtrage avancé
  uint16_t readReactiveFiltered(); // Lecture réactive avec lissage minimal
  void resetHistory(); // Reset l'historique en cas de problème
  bool isSensorDisabled() const; // Vérifier si le capteur est désactivé
 private:
  // Lecture manuelle par impulsion (pulseIn) – pas de dépendance lib requise
  int _pinTrigEcho; // pin partagé trig/echo
  // Mesure de la largeur d'impulsion via RMT si disponible, sinon pulseIn
  uint32_t readEchoPulseUs(uint32_t timeoutUs);
  
  // Historique glissant pour détection d'aberrations
  static const uint8_t HISTORY_SIZE = 5;
  uint16_t _history[HISTORY_SIZE];
  uint8_t _historyIndex;
  uint8_t _historyCount;
  uint16_t _lastValidDistance;
  
  // Configuration du filtrage - renforcé pour surface agitée (prod)
  static const uint16_t MAX_DISTANCE_DELTA = 30; // Seuil saut pour consensus
  static const uint16_t OUTLIER_SPREAD_CM = 15; // Rejet intra-batch si écart > 15 cm de la médiane
  static const uint8_t MIN_VALID_READINGS = 1;
  static const uint8_t READINGS_COUNT = 5; // 5 lectures + médiane pour eau agitée
  static const uint8_t REACTIVE_READINGS_COUNT = 5; // Idem mode réactif
  static const uint16_t MIN_DISTANCE = 2; // Minimum selon datasheet HC-SR04
  static const uint16_t MAX_DISTANCE = 400; // Maximum selon datasheet HC-SR04 (4m)
  // Délai minimum recommandé entre mesures
  static const uint32_t MIN_DELAY_BETWEEN_MEASUREMENTS_MS = 60;
  
  // Gestion des défaillances
  SensorFailureManager _failureManager;
  
  // v11.173: Rate-limiting des logs timeout (évite bruit excessif si capteur non connecté)
  uint16_t _timeoutCount;
  static const uint16_t TIMEOUT_LOG_INTERVAL = 10; // Log seulement tous les N timeouts après les 3 premiers
};

class AirSensor {
 public:
  AirSensor();
  void begin();
  // Méthodes publiques: utilisation robuste avec timeout et récupération automatique
  float robustTemperatureC(); // Méthode recommandée: récupération robuste avec timeout
  float robustHumidity(); // Méthode recommandée: récupération robuste avec timeout
  float pressureHpa(); // BME280 uniquement : pression en hPa ; NAN si DHT ou non disponible
  void resetHistory(); // Reset l'historique en cas de problème
  bool isSensorConnected(); // Vérifie la connectivité du capteur
  void resetSensor(); // Reset matériel du capteur
 private:
  // Méthodes internes: utilisation par robustTemperatureC() et robustHumidity()
  float temperatureC(); // Lecture simple (interne)
  float humidity(); // Lecture simple (interne)
  float filteredTemperatureC(); // Filtrage EMA avec throttle (interne)
  float filteredHumidity(); // Filtrage EMA avec throttle (interne)
#if defined(USE_AIR_SENSOR_AUTO)
  Adafruit_BME280 _bme;
  DHT _dht;
  bool _useBme280{false};
#elif defined(USE_AIR_SENSOR_BME280)
  Adafruit_BME280 _bme;
#else
  DHT _dht;
#endif

  // Historique glissant pour température
  static const uint8_t HISTORY_SIZE = 5;
  float _tempHistory[HISTORY_SIZE];
  uint8_t _tempHistoryIndex;
  uint8_t _tempHistoryCount;
  float _lastValidTemp;
  
  // Historique glissant pour humidité
  float _humidityHistory[HISTORY_SIZE];
  uint8_t _humidityHistoryIndex;
  uint8_t _humidityHistoryCount;
  float _lastValidHumidity;
  
  // Configuration du filtrage
  static constexpr float MAX_TEMP_DELTA = 3.0f; // Écart max accepté entre mesures (°C)
  static constexpr float MAX_HUMIDITY_DELTA = 10.0f; // Écart max accepté entre mesures (%)
  static const uint8_t MIN_VALID_READINGS = 3; // Nombre min de lectures valides
  static const uint8_t READINGS_COUNT = 5; // Nombre total de lectures pour médiane (DHT plus lent)
  
  // Configuration de récupération
  // Nombre max de tentatives de récupération (DHT plus lent)
  static const uint8_t MAX_RECOVERY_ATTEMPTS = 3;
  // Délai optimisé entre tentatives de récupération (réduit de 500ms à 300ms)
  static const uint16_t RECOVERY_DELAY_MS = 300;
  // Délai optimisé après reset matériel (réduit de 2000ms à 1000ms)
  static const uint16_t SENSOR_RESET_DELAY_MS = 1000;
  
  // Min interval + EMA
  unsigned long _lastDhtReadMs{0};
  bool _emaInit{false};
  float _emaTemp{NAN};
  float _emaHumidity{NAN};
  
  // v11.156: Gestion désactivation automatique après échecs répétés
  // Séparer les compteurs pour température et humidité pour éviter réinitialisation prématurée
  uint8_t _consecutiveTempFailures{0};
  uint8_t _consecutiveHumidityFailures{0};
  bool _sensorDisabled{false};
  bool _disableLogged{false};
  static constexpr uint8_t MAX_CONSECUTIVE_FAILURES = 10;
  
  // Réactivation automatique : tester périodiquement si le capteur redevient présent
  uint32_t _lastReactivationTestMs{0};
  uint8_t _consecutiveReactivationSuccesses{0};
  static constexpr uint32_t REACTIVATION_TEST_INTERVAL_MS = 60000; // Tester toutes les 60 secondes
  // 3 succès consécutifs pour réactiver
  static constexpr uint8_t REACTIVATION_SUCCESS_THRESHOLD = 3;
};

class WaterTempSensor {
 public:
  WaterTempSensor();
  void begin();
  float temperatureC();
  float filteredTemperatureC(); // Nouvelle méthode avec filtrage avancé
  float getTemperatureWithFallback(); // NOUVELLE MÉTHODE NON-BLOQUANTE (v11.50)
  void resetHistory(); // Reset l'historique en cas de problème
  bool isSensorConnected(); // Vérifie la connectivité du capteur
  void resetSensor(); // Reset matériel du capteur
  bool isSensorDisabled() const; // Vérifier si le capteur est désactivé
  
  // Méthodes NVS pour persistance
  void saveLastValidTempToNVS(float temp);
  float loadLastValidTempFromNVS();

 private:
  OneWire _oneWire{Pins::ONE_WIRE_BUS};
  DallasTemperature _sensors{&_oneWire};
  
  // Historique glissant pour détection d'aberrations
  static const uint8_t HISTORY_SIZE = 5;
  float _history[HISTORY_SIZE];
  uint8_t _historyIndex;
  uint8_t _historyCount;
  float _lastValidTemp;
  float _lastSavedTempToNVS{NAN};   // Dernière valeur écrite en NVS (debounce)
  uint32_t _lastNvsSaveMs{0};       // Dernière sauvegarde NVS (debounce temporel)

  // Configuration du filtrage
  static constexpr float MAX_TEMP_DELTA = SensorConfig::WaterTemp::MAX_DELTA;
  static constexpr uint8_t MIN_VALID_READINGS = SensorConfig::WaterTemp::MIN_READINGS;
  static constexpr uint8_t READINGS_COUNT = 2; // Optimisé : 2 lectures suffisent avec médiane
  
  // Configuration de récupération
  // réutilise comme nombre de tentatives
  static constexpr uint8_t MAX_RECOVERY_ATTEMPTS = SensorConfig::WaterTemp::MAX_RETRIES;
  static constexpr uint16_t RECOVERY_DELAY_MS = SensorConfig::WaterTemp::RETRY_DELAY_MS;
  static constexpr uint16_t SENSOR_RESET_DELAY_MS = 1000;
  
  // CONSTANTES OPTIMISÉES POUR ROBUSTESSE DS18B20
  // Utilise les constantes de SensorConfig::DS18B20 (config.h) pour cohérence
  static constexpr uint8_t DS18B20_RESOLUTION = SensorConfig::DS18B20::RESOLUTION_BITS;
  static constexpr uint16_t CONVERSION_DELAY_MS = SensorConfig::DS18B20::CONVERSION_DELAY_MS;
  static constexpr uint16_t READING_INTERVAL_MS = SensorConfig::DS18B20::READING_INTERVAL_MS;
  static constexpr uint8_t STABILIZATION_READINGS = SensorConfig::DS18B20::STABILIZATION_READINGS;
  static constexpr uint8_t MAX_READING_RETRIES = SensorConfig::WaterTemp::MAX_RETRIES;
  static constexpr uint16_t ONEWIRE_RESET_DELAY_MS = SensorConfig::DS18B20::ONEWIRE_RESET_DELAY_MS;
  
  // Non-bloquant
  unsigned long _lastRequestMs{0};
  bool _pipelineReady{false};
  bool _emaInit{false};
  float _emaTemp{NAN};
  
  // Gestion des défaillances
  SensorFailureManager _failureManager;
}; 