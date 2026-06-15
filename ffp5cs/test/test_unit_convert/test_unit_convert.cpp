// Profil de test (résout config.h via le stub FreeRTOS).
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS

#include <unity.h>
#include "config.h"

// Tests des conversions d'unités SensorConfig::Ultrasonic (config_sensors.h),
// non testées alors qu'elles sous-tendent TOUS les seuils d'alerte de niveau
// d'eau (comparaisons mm capteur <-> cm config dans automatism_display.cpp).
// mmToCmRounded a un arrondi (mm+5)/10 bug-prone (direction d'arrondi).

namespace U = SensorConfig::Ultrasonic;

void setUp(void) {}
void tearDown(void) {}

void test_cmToMm(void) {
  TEST_ASSERT_EQUAL_UINT16(0,     U::cmToMm(0));
  TEST_ASSERT_EQUAL_UINT16(10,    U::cmToMm(1));
  TEST_ASSERT_EQUAL_UINT16(180,   U::cmToMm(18));   // seuil aquarium typique
  TEST_ASSERT_EQUAL_UINT16(800,   U::cmToMm(80));   // seuil réservoir typique
  TEST_ASSERT_EQUAL_UINT16(12340, U::cmToMm(1234));
}

void test_mmToCmRounded_boundaries(void) {
  TEST_ASSERT_EQUAL_UINT16(0, U::mmToCmRounded(0));
  TEST_ASSERT_EQUAL_UINT16(0, U::mmToCmRounded(4));   // 4 -> 0 (arrondi bas)
  TEST_ASSERT_EQUAL_UINT16(1, U::mmToCmRounded(5));   // 5 -> 1 (arrondi haut au .5)
  TEST_ASSERT_EQUAL_UINT16(1, U::mmToCmRounded(9));
  TEST_ASSERT_EQUAL_UINT16(1, U::mmToCmRounded(14));  // 14 -> 1
  TEST_ASSERT_EQUAL_UINT16(2, U::mmToCmRounded(15));  // 15 -> 2
  TEST_ASSERT_EQUAL_UINT16(18, U::mmToCmRounded(180));
  TEST_ASSERT_EQUAL_UINT16(18, U::mmToCmRounded(184)); // 184 -> 18
  TEST_ASSERT_EQUAL_UINT16(19, U::mmToCmRounded(185)); // 185 -> 19
}

void test_roundtrip_cm_mm_cm(void) {
  const uint16_t cms[] = {0, 1, 18, 80, 121, 650};
  for (uint16_t cm : cms) {
    TEST_ASSERT_EQUAL_UINT16(cm, U::mmToCmRounded(U::cmToMm(cm)));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_cmToMm);
  RUN_TEST(test_mmToCmRounded_boundaries);
  RUN_TEST(test_roundtrip_cm_mm_cm);
  return UNITY_END();
}
