// Tests natifs : debounce global + helpers poll (US, PIR, promotion US runtime).

#include <unity.h>

#include "../../include/pgl_detection.h"
#include "../../include/config.h"

void setUp(void) {}
void tearDown(void) {}

void test_debounce_first_count_allowed(void) {
  TEST_ASSERT_TRUE(pglDetectionDebounceAllowsCount(1000, 0, 600));
}

void test_debounce_within_window_blocked(void) {
  TEST_ASSERT_FALSE(pglDetectionDebounceAllowsCount(1500, 1000, 600));
}

void test_debounce_after_window_allowed(void) {
  TEST_ASSERT_TRUE(pglDetectionDebounceAllowsCount(1601, 1000, 600));
}

void test_debounce_exact_boundary_allowed(void) {
  TEST_ASSERT_TRUE(pglDetectionDebounceAllowsCount(1600, 1000, 600));
}

void test_debounce_millis_wraparound(void) {
  constexpr uint32_t last = UINT32_MAX - 100;
  constexpr uint32_t now = 500;
  TEST_ASSERT_TRUE(pglDetectionDebounceAllowsCount(now, last, 600));
}

void test_us_trigger_requires_two_polls(void) {
  uint8_t below = 0;
  TEST_ASSERT_FALSE(pglDetectionCheckUsTrigger(below, 20, 25, 2));
  TEST_ASSERT_EQUAL_UINT8(1, below);
  TEST_ASSERT_TRUE(pglDetectionCheckUsTrigger(below, 18, 25, 2));
  TEST_ASSERT_EQUAL_UINT8(0, below);
}

void test_us_trigger_resets_when_out_of_range(void) {
  uint8_t below = 1;
  TEST_ASSERT_FALSE(pglDetectionCheckUsTrigger(below, 80, 25, 2));
  TEST_ASSERT_EQUAL_UINT8(0, below);
}

void test_us_zone_clear_for_rearm(void) {
  TEST_ASSERT_TRUE(pglDetectionUsZoneClear(0, 25));
  TEST_ASSERT_TRUE(pglDetectionUsZoneClear(30, 25));
  TEST_ASSERT_FALSE(pglDetectionUsZoneClear(20, 25));
  TEST_ASSERT_FALSE(pglDetectionUsZoneClear(25, 25));
}

void test_pir_guard_blocks_retrigger(void) {
  TEST_ASSERT_FALSE(pglDetectionCheckPirTrigger(true, true, 1000, 1500, 1000));
  TEST_ASSERT_TRUE(pglDetectionCheckPirTrigger(false, true, 1000, 2000, 1000));
  TEST_ASSERT_FALSE(pglDetectionCheckPirTrigger(false, true, 1000, 1500, 1000));
}

void test_us_runtime_promotion_after_valid_reads(void) {
  bool usPresent = false;
  uint8_t valid = 0;
  uint8_t invalid = 0;
  for (uint8_t i = 0; i < 2; ++i) {
    pglDetectionUpdateUsRuntimePresence(usPresent, valid, invalid, 40, 3, 5);
    TEST_ASSERT_FALSE(usPresent);
  }
  pglDetectionUpdateUsRuntimePresence(usPresent, valid, invalid, 35, 3, 5);
  TEST_ASSERT_TRUE(usPresent);
}

void test_us_runtime_no_reset_on_single_timeout(void) {
  bool usPresent = false;
  uint8_t valid = 2;
  uint8_t invalid = 0;
  pglDetectionUpdateUsRuntimePresence(usPresent, valid, invalid, 0, 3, 5);
  TEST_ASSERT_EQUAL_UINT8(2, valid);
  TEST_ASSERT_EQUAL_UINT8(1, invalid);
  TEST_ASSERT_FALSE(usPresent);
}

void test_ir_trigger_on_high_to_low(void) {
  TEST_ASSERT_TRUE(pglDetectionCheckIrTrigger(true, false));
  TEST_ASSERT_FALSE(pglDetectionCheckIrTrigger(false, false));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_debounce_first_count_allowed);
  RUN_TEST(test_debounce_within_window_blocked);
  RUN_TEST(test_debounce_after_window_allowed);
  RUN_TEST(test_debounce_exact_boundary_allowed);
  RUN_TEST(test_debounce_millis_wraparound);
  RUN_TEST(test_us_trigger_requires_two_polls);
  RUN_TEST(test_us_trigger_resets_when_out_of_range);
  RUN_TEST(test_us_zone_clear_for_rearm);
  RUN_TEST(test_pir_guard_blocks_retrigger);
  RUN_TEST(test_us_runtime_promotion_after_valid_reads);
  RUN_TEST(test_us_runtime_no_reset_on_single_timeout);
  RUN_TEST(test_ir_trigger_on_high_to_low);
  return UNITY_END();
}
