#pragma once
// config_sensors.h — capteurs (ultrason/DS18B20/DHT/BME280), helpers de
// validation, actionneurs. Extrait de config.h (découpe).
#include <Arduino.h>
#include <cmath>            // isnan() dans SensorValidation
#include "gpio_mapping.h"   // GPIODefaults (ActuatorConfig)

// -----------------------------------------------------------------------------
// 5. CAPTEURS ET ACTIONNEURS
// -----------------------------------------------------------------------------
namespace SensorConfig {
    inline constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    inline constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;
    // Test de connectivité capteur (DHT, DS18B20)
    inline constexpr uint32_t CONNECTIVITY_TEST_TIMEOUT_MS = 2000;
    // Debounce pour sauvegarde température NVS
    inline constexpr uint32_t NVS_TEMP_DEBOUNCE_MS = 60000;

    namespace DefaultValues {
        inline constexpr float TEMP_AIR_DEFAULT = 20.0f;
        inline constexpr float HUMIDITY_DEFAULT = 50.0f;
        inline constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }
    
    // Valeurs fallback pour l'API JSON (affichage quand capteur invalide)
    // Ces valeurs sont utilisées dans /json, WebSocket, etc.
    namespace Fallback {
        inline constexpr float TEMP_WATER = 25.5f;
        inline constexpr float TEMP_AIR = 22.3f;
        inline constexpr float HUMIDITY = 65.0f;
        inline constexpr float WATER_LEVEL_AQUA = 152.0f;  // mm
        inline constexpr float WATER_LEVEL_TANK = 87.0f;   // mm
        inline constexpr float WATER_LEVEL_POTA = 121.0f;  // mm
        inline constexpr uint16_t LUMINOSITY = 450;
    }

    // Politique fallback ultrason (compile-time). false = wl*=0, champ POST omis, NULL BDD.
    namespace WaterLevelFallbackPolicy {
        inline constexpr bool USE_FALLBACK_AQUA = false;
        inline constexpr bool USE_FALLBACK_TANK = true;
        inline constexpr bool USE_FALLBACK_POTA = true;
    }

    namespace WaterTemp {
        inline constexpr float MIN_VALID = 5.0f;
        inline constexpr float MAX_VALID = 60.0f;
        inline constexpr float MAX_DELTA = 3.0f;
        inline constexpr uint8_t MIN_READINGS = 4;
        inline constexpr uint8_t TOTAL_READINGS = 7;
        inline constexpr uint16_t RETRY_DELAY_MS = 200;
        inline constexpr uint8_t MAX_RETRIES = 5;
    }

    namespace AirSensor {
        inline constexpr float TEMP_MIN = 3.0f;
        inline constexpr float TEMP_MAX = 50.0f;
        inline constexpr float HUMIDITY_MIN = 10.0f;
        inline constexpr float HUMIDITY_MAX = 100.0f;
    }

    namespace DHT {
        // Type: DHT22 uniquement en wroom-prod (USE_DHT22), DHT11 pour tous les autres envs (sensors.cpp).
        // Délai minimum entre lectures: 2500ms (compromis entre 2000ms datasheet et stabilité)
        // DHT11: 1s min, DHT22: 2s min (datasheet). On utilise 2.5s pour les deux.
        inline constexpr uint32_t MIN_READ_INTERVAL_MS = 2500;
        inline constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
        // Timeout récupération temp/hum (robustTemperatureC, robustHumidityC) - évite INT_WDT
        inline constexpr uint32_t RECOVERY_TIMEOUT_MS = 2000;
    }

    // Alternative DHT pour envs S3 (USE_AIR_SENSOR_BME280). I2C, plus rapide que DHT.
    namespace BME280 {
        inline constexpr uint32_t MIN_READ_INTERVAL_MS = 500;
        inline constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 100;
        inline constexpr uint8_t I2C_ADDRESS = 0x76;  // SDO à GND (0x77 si SDO à VDD)
    }

    namespace Ultrasonic {
        // v13.53 (audit): MIN/MAX_DISTANCE = bornes physiques HC-SR04 (datasheet ~2 cm à ~400 cm).
        //                 MAX_VALID_LEVEL = borne « niveau eau » au sens application (cuves plus
        //                 profondes, marge mesure off-axis). Les deux sont volontairement distincts.
        //                 Documenter l'écart pour éviter ré-alignement intempestif.
        inline constexpr uint16_t MIN_DISTANCE_MM = 20;    // 2 cm  (datasheet HC-SR04)
        inline constexpr uint16_t MAX_DISTANCE_MM = 4000;  // 400 cm (datasheet HC-SR04)
        // Compat héritée pour code non migré (cm)
        inline constexpr uint16_t MIN_DISTANCE_CM = 2;
        inline constexpr uint16_t MAX_DISTANCE_CM = 400;
        // Plage validée dans system_sensors pour niveaux eau (potager, aquarium, réservoir).
        // Volontairement plus large que MAX_DISTANCE_MM : marge cuves profondes / lectures off-axis.
        inline constexpr uint16_t MAX_VALID_LEVEL_MM = 5000;
        inline constexpr uint16_t MAX_VALID_LEVEL_CM = 500;
        inline constexpr uint16_t MAX_DELTA_MM = 300;
        inline constexpr uint16_t MAX_DELTA_CM = 30;
        inline constexpr uint32_t TIMEOUT_US = 30000;
        inline constexpr uint8_t DEFAULT_SAMPLES = 5;
        inline constexpr uint32_t MIN_DELAY_MS = 60;
        inline constexpr uint16_t US_TO_CM_FACTOR = 58;
        inline constexpr uint8_t FILTERED_READINGS_COUNT = 3;

        // Plage métier réservoir (readAdvancedFiltered / wlTank uniquement)
        // Cuve étroite : échos multipath donnent des distances trop courtes (faux « plein »).
        // L'algo privilégie le cluster haut en cas de bimodalité et assouplit les sauts
        // vers le haut (vidage) par rapport aux sauts vers le bas (remplissage).
        namespace Tank {
            inline constexpr uint16_t MIN_OPERATIONAL_MM = 15;
            inline constexpr uint16_t MAX_OPERATIONAL_MM = 1000;
            inline constexpr uint16_t MIN_RAW_MM = 15;  // cuve pleine : sous la spec HC-SR04 (20 mm)
            inline constexpr uint8_t SAMPLES_COUNT = 7;
            inline constexpr uint8_t ADVANCED_MIN_VALID_READINGS = 3;
            inline constexpr uint8_t STRONG_BATCH_MIN_READINGS = 4;
            inline constexpr uint16_t OUTLIER_SPREAD_MM = 50;
            inline constexpr uint16_t TIGHT_BATCH_SPREAD_MM = 40;
            inline constexpr uint16_t BIMODAL_GAP_MM = 80;
            inline constexpr uint16_t DRAIN_MAX_DELTA_MM = 400;
            inline constexpr uint16_t REFILL_MAX_DELTA_MM = 100;

            inline constexpr bool isOperationalMm(uint16_t mm) {
                return mm >= MIN_OPERATIONAL_MM && mm <= MAX_OPERATIONAL_MM;
            }
            inline constexpr bool isRawReadingMm(uint16_t mm) {
                return mm >= MIN_RAW_MM && mm < MAX_DISTANCE_MM;
            }
        }

        inline constexpr uint16_t cmToMm(uint16_t cm) { return static_cast<uint16_t>(cm * 10U); }
        inline constexpr uint16_t mmToCmRounded(uint16_t mm) { return static_cast<uint16_t>((mm + 5U) / 10U); }
    }

    namespace History {
        inline constexpr uint8_t AQUA_HISTORY_SIZE = 16;
        inline constexpr uint8_t SENSOR_READINGS_COUNT = 3;
    }
    
    namespace DS18B20 {
        inline constexpr uint8_t RESOLUTION_BITS = 10;
        // 220ms = 187.5ms (datasheet) + 17% marge (recommandation: +10-20%)
        inline constexpr uint16_t CONVERSION_DELAY_MS = 220;
        inline constexpr uint16_t READING_INTERVAL_MS = 400;
        inline constexpr uint8_t STABILIZATION_READINGS = 1;
        inline constexpr uint16_t STABILIZATION_DELAY_MS = 50;
        inline constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;
        // Timeout global lecture DS18B20 (ex-GlobalTimeouts::DS18B20_MAX_MS)
        inline constexpr uint32_t TIMEOUT_MS = 1000;
    }
    
    // Timeouts de test de réactivation des capteurs désactivés
    namespace Reactivation {
        // Ultrasonic: timeout court car lecture rapide
        inline constexpr uint32_t ULTRASONIC_TIMEOUT_MS = 500;
        // Temperature sensors (WaterTemp, AirSensor): timeout plus long
        inline constexpr uint32_t TEMPERATURE_TIMEOUT_MS = 1000;
    }
}

// -----------------------------------------------------------------------------
// 5.1 HELPERS VALIDATION CAPTEURS (v11.176 - audit élimination duplications)
// -----------------------------------------------------------------------------
// Ces fonctions inline remplacent le pattern répété isnan() + range check
// utilisé dans sensors.cpp, automatism.cpp, app_tasks.cpp, web_client.cpp
namespace SensorValidation {
    // Valide une température d'eau (DS18B20)
    // Retourne true si la valeur est valide, false si NaN ou hors plage
    // Note: -127.0f est le code erreur DallasTemperature
    inline bool isValidWaterTemp(float temp) {
        return !isnan(temp) && 
               temp != -127.0f &&  // Code erreur Dallas
               temp >= SensorConfig::WaterTemp::MIN_VALID && 
               temp <= SensorConfig::WaterTemp::MAX_VALID;
    }
    
    // Valide une température d'air (DHT22)
    inline bool isValidAirTemp(float temp) {
        return !isnan(temp) && 
               temp >= SensorConfig::AirSensor::TEMP_MIN && 
               temp <= SensorConfig::AirSensor::TEMP_MAX;
    }
    
    // Valide une humidité (DHT22)
    inline bool isValidHumidity(float humidity) {
        return !isnan(humidity) && 
               humidity >= SensorConfig::AirSensor::HUMIDITY_MIN && 
               humidity <= SensorConfig::AirSensor::HUMIDITY_MAX;
    }
    
    // Valide une distance ultrasonique
    inline bool isValidDistance(uint16_t distance) {
        return distance >= SensorConfig::Ultrasonic::MIN_DISTANCE_MM && 
               distance <= SensorConfig::Ultrasonic::MAX_DISTANCE_MM;
    }

    // Niveau d'eau connu (lecture ultrason valide pour l'automatisme / POST)
    inline bool isWaterLevelKnown(uint16_t wl) {
        return wl > 0 && wl <= SensorConfig::Ultrasonic::MAX_VALID_LEVEL_MM;
    }
    
    // Applique une valeur par défaut si la température d'eau est invalide
    inline float sanitizeWaterTemp(float temp) {
        return isValidWaterTemp(temp) ? temp : SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
    }
    
    // Applique une valeur par défaut si la température d'air est invalide
    inline float sanitizeAirTemp(float temp) {
        return isValidAirTemp(temp) ? temp : SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
    }
    
    // Applique une valeur par défaut si l'humidité est invalide
    inline float sanitizeHumidity(float humidity) {
        return isValidHumidity(humidity) ? humidity : SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
    }
}

namespace ActuatorConfig {
    // Valeurs par défaut - référencent GPIODefaults (gpio_mapping.h) comme source de vérité
    namespace Default {
        inline constexpr int AQUA_LEVEL_CM = GPIODefaults::AQ_THRESHOLD_CM;
        inline constexpr int TANK_LEVEL_CM = GPIODefaults::TANK_THRESHOLD_CM;
        inline constexpr float HEATER_THRESHOLD_C = GPIODefaults::HEAT_THRESHOLD_C;
        inline constexpr uint16_t FEED_BIG_DURATION_SEC = GPIODefaults::FEED_BIG_DURATION_SEC;
        inline constexpr uint16_t FEED_SMALL_DURATION_SEC = GPIODefaults::FEED_SMALL_DURATION_SEC;
    }
}
