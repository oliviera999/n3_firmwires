// v13.70 (audit) - Tests Unity pour SensorValidation (config.h)
// Compilable via `pio test -c platformio-native.ini -e native -f test_sensor_validation`.
//
// Couvre:
//   - isValidWaterTemp (DS18B20 plage 5-60, -127 rejeté, NaN rejeté)
//   - isValidAirTemp (DHT plage 3-50, NaN rejeté)
//   - isValidHumidity (DHT plage 10-100, NaN rejeté)
//   - isValidDistance (HC-SR04 plage 20-4000 mm)
//   - sanitize* (fallback aux defaults sûrs si invalide)
//   - waterLevel timeout fallback (dernière valeur valide puis défaut sûr)

#include <unity.h>
#include <cmath>
#include "sensor_reading_fallback.h"

// Stubs minimalistes pour ne pas inclure tout config.h en native (qui pull Arduino, etc.)
namespace SensorConfig {
namespace WaterTemp {
    constexpr float MIN_VALID = 5.0f;
    constexpr float MAX_VALID = 60.0f;
}

namespace AirSensor {
    constexpr float TEMP_MIN = 3.0f;
    constexpr float TEMP_MAX = 50.0f;
    constexpr float HUMIDITY_MIN = 10.0f;
    constexpr float HUMIDITY_MAX = 100.0f;
}
namespace Ultrasonic {
    constexpr uint16_t MIN_DISTANCE_MM = 20;
    constexpr uint16_t MAX_DISTANCE_MM = 4000;
}
namespace DefaultValues {
    constexpr float TEMP_AIR_DEFAULT = 20.0f;
    constexpr float HUMIDITY_DEFAULT = 50.0f;
    constexpr float TEMP_WATER_DEFAULT = 20.0f;
}
}

// Copies des inlines de config.h (gardées synchronisées par revue manuelle).
namespace SensorValidation {
inline bool isValidWaterTemp(float temp) {
    return !std::isnan(temp) && temp != -127.0f
        && temp >= SensorConfig::WaterTemp::MIN_VALID
        && temp <= SensorConfig::WaterTemp::MAX_VALID;
}
inline bool isValidAirTemp(float temp) {
    return !std::isnan(temp)
        && temp >= SensorConfig::AirSensor::TEMP_MIN
        && temp <= SensorConfig::AirSensor::TEMP_MAX;
}
inline bool isValidHumidity(float h) {
    return !std::isnan(h)
        && h >= SensorConfig::AirSensor::HUMIDITY_MIN
        && h <= SensorConfig::AirSensor::HUMIDITY_MAX;
}
inline bool isValidDistance(uint16_t d) {
    return d >= SensorConfig::Ultrasonic::MIN_DISTANCE_MM
        && d <= SensorConfig::Ultrasonic::MAX_DISTANCE_MM;
}
inline float sanitizeWaterTemp(float t) {
    return isValidWaterTemp(t) ? t : SensorConfig::DefaultValues::TEMP_WATER_DEFAULT;
}
inline float sanitizeAirTemp(float t) {
    return isValidAirTemp(t) ? t : SensorConfig::DefaultValues::TEMP_AIR_DEFAULT;
}
inline float sanitizeHumidity(float h) {
    return isValidHumidity(h) ? h : SensorConfig::DefaultValues::HUMIDITY_DEFAULT;
}
}

void setUp(void) {}
void tearDown(void) {}

// --- isValidWaterTemp ---
void test_water_temp_in_range() {
    TEST_ASSERT_TRUE(SensorValidation::isValidWaterTemp(5.0f));
    TEST_ASSERT_TRUE(SensorValidation::isValidWaterTemp(20.5f));
    TEST_ASSERT_TRUE(SensorValidation::isValidWaterTemp(60.0f));
}
void test_water_temp_out_of_range() {
    TEST_ASSERT_FALSE(SensorValidation::isValidWaterTemp(4.99f));
    TEST_ASSERT_FALSE(SensorValidation::isValidWaterTemp(60.01f));
    TEST_ASSERT_FALSE(SensorValidation::isValidWaterTemp(-127.0f));
    TEST_ASSERT_FALSE(SensorValidation::isValidWaterTemp(NAN));
}

// --- isValidAirTemp ---
void test_air_temp_in_range() {
    TEST_ASSERT_TRUE(SensorValidation::isValidAirTemp(3.0f));
    TEST_ASSERT_TRUE(SensorValidation::isValidAirTemp(22.0f));
    TEST_ASSERT_TRUE(SensorValidation::isValidAirTemp(50.0f));
}
void test_air_temp_out_of_range() {
    TEST_ASSERT_FALSE(SensorValidation::isValidAirTemp(2.99f));
    TEST_ASSERT_FALSE(SensorValidation::isValidAirTemp(50.01f));
    TEST_ASSERT_FALSE(SensorValidation::isValidAirTemp(NAN));
}

// --- isValidHumidity ---
void test_humidity_in_range() {
    TEST_ASSERT_TRUE(SensorValidation::isValidHumidity(10.0f));
    TEST_ASSERT_TRUE(SensorValidation::isValidHumidity(65.0f));
    TEST_ASSERT_TRUE(SensorValidation::isValidHumidity(100.0f));
}
void test_humidity_out_of_range() {
    TEST_ASSERT_FALSE(SensorValidation::isValidHumidity(9.99f));
    TEST_ASSERT_FALSE(SensorValidation::isValidHumidity(100.01f));
    TEST_ASSERT_FALSE(SensorValidation::isValidHumidity(NAN));
}

// --- isValidDistance ---
void test_distance_in_range() {
    TEST_ASSERT_TRUE(SensorValidation::isValidDistance(20));
    TEST_ASSERT_TRUE(SensorValidation::isValidDistance(500));
    TEST_ASSERT_TRUE(SensorValidation::isValidDistance(4000));
}
void test_distance_out_of_range() {
    TEST_ASSERT_FALSE(SensorValidation::isValidDistance(19));
    TEST_ASSERT_FALSE(SensorValidation::isValidDistance(4001));
    TEST_ASSERT_FALSE(SensorValidation::isValidDistance(0));
}

// --- sanitize* (audit 2026-05) ---
void test_sanitize_water_invalid_uses_default() {
    TEST_ASSERT_EQUAL_FLOAT(SensorConfig::DefaultValues::TEMP_WATER_DEFAULT,
                             SensorValidation::sanitizeWaterTemp(NAN));
    TEST_ASSERT_EQUAL_FLOAT(SensorConfig::DefaultValues::TEMP_WATER_DEFAULT,
                             SensorValidation::sanitizeWaterTemp(-127.0f));
    TEST_ASSERT_EQUAL_FLOAT(SensorConfig::DefaultValues::TEMP_WATER_DEFAULT,
                             SensorValidation::sanitizeWaterTemp(100.0f));
}
void test_sanitize_water_valid_preserves_value() {
    TEST_ASSERT_EQUAL_FLOAT(25.5f, SensorValidation::sanitizeWaterTemp(25.5f));
}
void test_sanitize_air_invalid_uses_default() {
    TEST_ASSERT_EQUAL_FLOAT(SensorConfig::DefaultValues::TEMP_AIR_DEFAULT,
                             SensorValidation::sanitizeAirTemp(NAN));
}
void test_sanitize_humidity_invalid_uses_default() {
    TEST_ASSERT_EQUAL_FLOAT(SensorConfig::DefaultValues::HUMIDITY_DEFAULT,
                             SensorValidation::sanitizeHumidity(NAN));
}

// --- waterLevel timeout fallback (audit 2026-05) ---
void test_water_level_fallback_preserves_current_reading() {
    TEST_ASSERT_EQUAL_UINT16(123, SensorReadingFallback::waterLevel(123, 222, 333));
}

void test_water_level_fallback_uses_last_valid_when_current_missing() {
    TEST_ASSERT_EQUAL_UINT16(222, SensorReadingFallback::waterLevel(0, 222, 333));
}

void test_water_level_fallback_uses_safe_default_when_no_history() {
    TEST_ASSERT_EQUAL_UINT16(333, SensorReadingFallback::waterLevel(0, 0, 333));
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_water_temp_in_range);
    RUN_TEST(test_water_temp_out_of_range);
    RUN_TEST(test_air_temp_in_range);
    RUN_TEST(test_air_temp_out_of_range);
    RUN_TEST(test_humidity_in_range);
    RUN_TEST(test_humidity_out_of_range);
    RUN_TEST(test_distance_in_range);
    RUN_TEST(test_distance_out_of_range);
    RUN_TEST(test_sanitize_water_invalid_uses_default);
    RUN_TEST(test_sanitize_water_valid_preserves_value);
    RUN_TEST(test_sanitize_air_invalid_uses_default);
    RUN_TEST(test_sanitize_humidity_invalid_uses_default);
    RUN_TEST(test_water_level_fallback_preserves_current_reading);
    RUN_TEST(test_water_level_fallback_uses_last_valid_when_current_missing);
    RUN_TEST(test_water_level_fallback_uses_safe_default_when_no_history);
    return UNITY_END();
}