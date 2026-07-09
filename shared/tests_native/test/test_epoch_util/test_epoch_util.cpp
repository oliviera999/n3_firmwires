#include <unity.h>
#include <ctime>

// Tests de EpochUtil (n3_epoch_util.h), mutualisé depuis ffp5cs (audit §3.8).
// POINT CRITIQUE : la validation se fait en comparaison NON SIGNÉE car
// EPOCH_MAX_VALID (2050) > INT32_MAX. On vérifie les bornes ET les valeurs
// au-delà de INT32_MAX (le bug que l'unsigned évite sur time_t 32-bit).
#include "n3_epoch_util.h"

using namespace EpochUtil;

// Bornes réelles du firmware (config_system.h).
static const time_t MIN = 1767225600;  // 2026-01-01 UTC
static const time_t MAX = 2524608000;  // 2050-01-01 UTC (> INT32_MAX = 2147483647)

void setUp(void) {}
void tearDown(void) {}

void test_zero_is_invalid(void) {
  TEST_ASSERT_FALSE(isValidEpoch(0, MIN, MAX));
}

void test_min_boundary_inclusive(void) {
  TEST_ASSERT_TRUE(isValidEpoch(MIN, MIN, MAX));
  TEST_ASSERT_FALSE(isValidEpoch(MIN - 1, MIN, MAX));
}

void test_max_boundary_inclusive(void) {
  TEST_ASSERT_TRUE(isValidEpoch(MAX, MIN, MAX));
  TEST_ASSERT_FALSE(isValidEpoch(MAX + 1, MIN, MAX));
}

void test_value_in_range(void) {
  TEST_ASSERT_TRUE(isValidEpoch(2000000000, MIN, MAX));  // < INT32_MAX, in range
}

void test_value_above_int32max_still_valid(void) {
  // 2.3e9 > INT32_MAX (2147483647) mais < MAX : DOIT être accepté.
  // C'est exactement ce que la comparaison non signée garantit.
  TEST_ASSERT_TRUE(isValidEpoch(2300000000u, MIN, MAX));
}

void test_value_above_max_rejected(void) {
  TEST_ASSERT_FALSE(isValidEpoch(3000000000u, MIN, MAX));  // > MAX
}

void test_negative_epoch_rejected(void) {
  // (unsigned long)(-1) est énorme -> > MAX -> rejeté (et pas accepté par erreur).
  TEST_ASSERT_FALSE(isValidEpoch((time_t)-1, MIN, MAX));
}

void test_abs_diff(void) {
  TEST_ASSERT_EQUAL_INT(60, (int)epochAbsDiff(100, 40));
  TEST_ASSERT_EQUAL_INT(60, (int)epochAbsDiff(40, 100));
  TEST_ASSERT_EQUAL_INT(0, (int)epochAbsDiff(50, 50));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_zero_is_invalid);
  RUN_TEST(test_min_boundary_inclusive);
  RUN_TEST(test_max_boundary_inclusive);
  RUN_TEST(test_value_in_range);
  RUN_TEST(test_value_above_int32max_still_valid);
  RUN_TEST(test_value_above_max_rejected);
  RUN_TEST(test_negative_epoch_rejected);
  RUN_TEST(test_abs_diff);
  return UNITY_END();
}
