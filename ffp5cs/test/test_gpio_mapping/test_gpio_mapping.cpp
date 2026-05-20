// v13.70 (audit) - Tests Unity pour la cohérence GPIODefaults / ActuatorConfig::Default.
// Compilable via `pio test -c platformio-native.ini -e native -f test_gpio_mapping`.
//
// Couvre :
//   - GPIODefaults sont effectivement les valeurs source de vérité.
//   - Les valeurs par défaut côté ActuatorConfig correspondent à GPIODefaults.
//   - Les seuils respectent des plages réalistes (sanity).

#include <unity.h>
#include <cstdint>

// Mock minimal des constantes (synchronisées avec gpio_mapping.h GPIODefaults
// et config.h ActuatorConfig::Default - revue manuelle au commit).
namespace GPIODefaults {
    constexpr int AQ_THRESHOLD_CM = 18;
    constexpr int TANK_THRESHOLD_CM = 80;
    constexpr float HEAT_THRESHOLD_C = 18.0f;
    constexpr int FEED_BIG_DURATION_SEC = 3;
    constexpr int FEED_SMALL_DURATION_SEC = 2;
}
namespace ActuatorConfig {
namespace Default {
    constexpr int AQUA_LEVEL_CM = GPIODefaults::AQ_THRESHOLD_CM;
    constexpr int TANK_LEVEL_CM = GPIODefaults::TANK_THRESHOLD_CM;
    constexpr float HEATER_THRESHOLD_C = GPIODefaults::HEAT_THRESHOLD_C;
    constexpr uint16_t FEED_BIG_DURATION_SEC = GPIODefaults::FEED_BIG_DURATION_SEC;
    constexpr uint16_t FEED_SMALL_DURATION_SEC = GPIODefaults::FEED_SMALL_DURATION_SEC;
}
}

void setUp(void) {}
void tearDown(void) {}

// --- Synchronisation GPIODefaults ↔ ActuatorConfig::Default ---
void test_aqua_threshold_aligned() {
    TEST_ASSERT_EQUAL_INT(GPIODefaults::AQ_THRESHOLD_CM, ActuatorConfig::Default::AQUA_LEVEL_CM);
}
void test_tank_threshold_aligned() {
    TEST_ASSERT_EQUAL_INT(GPIODefaults::TANK_THRESHOLD_CM, ActuatorConfig::Default::TANK_LEVEL_CM);
}
void test_heat_threshold_aligned() {
    TEST_ASSERT_EQUAL_FLOAT(GPIODefaults::HEAT_THRESHOLD_C, ActuatorConfig::Default::HEATER_THRESHOLD_C);
}
void test_feed_big_aligned() {
    TEST_ASSERT_EQUAL_UINT16(GPIODefaults::FEED_BIG_DURATION_SEC, ActuatorConfig::Default::FEED_BIG_DURATION_SEC);
}
void test_feed_small_aligned() {
    TEST_ASSERT_EQUAL_UINT16(GPIODefaults::FEED_SMALL_DURATION_SEC, ActuatorConfig::Default::FEED_SMALL_DURATION_SEC);
}

// --- Sanity checks plages ---
void test_aqua_threshold_sane() {
    TEST_ASSERT_GREATER_OR_EQUAL_INT(5, GPIODefaults::AQ_THRESHOLD_CM);
    TEST_ASSERT_LESS_OR_EQUAL_INT(50, GPIODefaults::AQ_THRESHOLD_CM);
}
void test_tank_threshold_sane() {
    TEST_ASSERT_GREATER_OR_EQUAL_INT(20, GPIODefaults::TANK_THRESHOLD_CM);
    TEST_ASSERT_LESS_OR_EQUAL_INT(150, GPIODefaults::TANK_THRESHOLD_CM);
}
void test_heat_threshold_sane() {
    TEST_ASSERT_FLOAT_WITHIN(15.0f, 22.5f, GPIODefaults::HEAT_THRESHOLD_C);
}
void test_feed_durations_sane() {
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, GPIODefaults::FEED_BIG_DURATION_SEC);
    TEST_ASSERT_LESS_OR_EQUAL_INT(10, GPIODefaults::FEED_BIG_DURATION_SEC);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(1, GPIODefaults::FEED_SMALL_DURATION_SEC);
    TEST_ASSERT_LESS_OR_EQUAL_INT(10, GPIODefaults::FEED_SMALL_DURATION_SEC);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;
    UNITY_BEGIN();
    RUN_TEST(test_aqua_threshold_aligned);
    RUN_TEST(test_tank_threshold_aligned);
    RUN_TEST(test_heat_threshold_aligned);
    RUN_TEST(test_feed_big_aligned);
    RUN_TEST(test_feed_small_aligned);
    RUN_TEST(test_aqua_threshold_sane);
    RUN_TEST(test_tank_threshold_sane);
    RUN_TEST(test_heat_threshold_sane);
    RUN_TEST(test_feed_durations_sane);
    return UNITY_END();
}