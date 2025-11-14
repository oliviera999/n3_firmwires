#pragma once

// =============================================================================
// CONFIGURATION CAPTEURS ET VALIDATION
// =============================================================================
// Module: Configuration complète des capteurs et ranges de validation
// Taille: ~75 lignes
// =============================================================================

#include <Arduino.h>

namespace SensorConfig {
    // Délais et stabilisation génériques
    constexpr uint32_t SENSOR_READ_DELAY_MS = 100;
    constexpr uint32_t I2C_STABILIZATION_DELAY_MS = 100;

    namespace DefaultValues {
        constexpr float TEMP_AIR_DEFAULT = 20.0f;
        constexpr float HUMIDITY_DEFAULT = 50.0f;
        constexpr float TEMP_WATER_DEFAULT = 20.0f;
    }

    namespace ValidationRanges {
        constexpr float TEMP_MIN = -50.0f;
        constexpr float TEMP_MAX = 100.0f;
        constexpr float HUMIDITY_MIN = 0.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }

    // DS18B20 (température eau)
    namespace DS18B20 {
        constexpr uint8_t RESOLUTION_BITS = 10;
        constexpr uint16_t CONVERSION_DELAY_MS = 250;
        constexpr uint16_t READING_INTERVAL_MS = 400;
        constexpr uint8_t STABILIZATION_READINGS = 1;
        constexpr uint16_t STABILIZATION_DELAY_MS = 50;
        constexpr uint16_t ONEWIRE_RESET_DELAY_MS = 100;
    }

    // Validation température eau (après filtrage)
    namespace WaterTemp {
        constexpr float MIN_VALID = 5.0f;
        constexpr float MAX_VALID = 60.0f;
        constexpr float MAX_DELTA = 3.0f;
        constexpr uint8_t MIN_READINGS = 4;
        constexpr uint8_t TOTAL_READINGS = 7;
        constexpr uint16_t RETRY_DELAY_MS = 200;
        constexpr uint8_t MAX_RETRIES = 5;
    }

    // DHT11/DHT22 (air)
    namespace AirSensor {
        constexpr float TEMP_MIN = 3.0f;
        constexpr float TEMP_MAX = 50.0f;
        constexpr float HUMIDITY_MIN = 10.0f;
        constexpr float HUMIDITY_MAX = 100.0f;
    }

    namespace DHT {
        constexpr uint32_t MIN_READ_INTERVAL_MS = 3000;
        constexpr uint32_t INIT_STABILIZATION_DELAY_MS = 2000;
        constexpr float TEMP_MAX_DELTA_C = 3.0f;
        constexpr float HUMIDITY_MAX_DELTA_PERCENT = 10.0f;
    }

    // Ultrasoniques (HC-SR04)
    namespace Ultrasonic {
        constexpr uint16_t MIN_DISTANCE_CM = 2;
        constexpr uint16_t MAX_DISTANCE_CM = 400;
        constexpr uint16_t MAX_DELTA_CM = 30;
        constexpr uint32_t TIMEOUT_US = 30000;
        constexpr uint8_t DEFAULT_SAMPLES = 5;
        constexpr uint32_t MIN_DELAY_MS = 60;
        constexpr uint16_t US_TO_CM_FACTOR = 58;
        constexpr uint8_t FILTERED_READINGS_COUNT = 3;
    }

    // Autres capteurs analogiques
    namespace Analog {
        constexpr uint16_t ADC_MAX_VALUE = 4095;
    }

    namespace History {
        constexpr uint8_t AQUA_HISTORY_SIZE = 16;
        constexpr uint8_t SENSOR_READINGS_COUNT = 3;
    }
}
