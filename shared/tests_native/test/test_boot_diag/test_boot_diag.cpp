#include <unity.h>
#include <cstring>

// Tests de n3boot (shared/n3_common/n3_boot_diag.h) : libellés d'état OTA alignés sur le
// serveur (HeartbeatExtras) et décision « mode sans échec » après K boots ratés consécutifs.
#include "n3_boot_diag.h"

void setUp(void) {}
void tearDown(void) {}

void test_ota_state_labels_match_server_vocabulary(void) {
  TEST_ASSERT_EQUAL_STRING("new", n3boot::otaStateLabel(0));
  TEST_ASSERT_EQUAL_STRING("pending", n3boot::otaStateLabel(1));
  TEST_ASSERT_EQUAL_STRING("valid", n3boot::otaStateLabel(2));
  TEST_ASSERT_EQUAL_STRING("invalid", n3boot::otaStateLabel(3));
  TEST_ASSERT_EQUAL_STRING("aborted", n3boot::otaStateLabel(4));
  TEST_ASSERT_EQUAL_STRING("undefined", n3boot::otaStateLabel(0xFFFFFFFFUL));
}

// Le compteur est incrémenté AVANT la décision : 3 boots ratés d'affilée → sans échec.
void test_safe_mode_after_three_failed_boots(void) {
  TEST_ASSERT_FALSE(n3boot::shouldEnterSafeMode(0));
  TEST_ASSERT_FALSE(n3boot::shouldEnterSafeMode(1));
  TEST_ASSERT_FALSE(n3boot::shouldEnterSafeMode(2));
  TEST_ASSERT_TRUE(n3boot::shouldEnterSafeMode(3));
  TEST_ASSERT_TRUE(n3boot::shouldEnterSafeMode(10));
}

void test_safe_mode_threshold_zero_disables(void) {
  TEST_ASSERT_FALSE(n3boot::shouldEnterSafeMode(50, 0));
  TEST_ASSERT_TRUE(n3boot::shouldEnterSafeMode(1, 1));
}

void test_crash_reset_classification(void) {
  TEST_ASSERT_TRUE(n3boot::isCrashReset(ESP_RST_PANIC));
  TEST_ASSERT_TRUE(n3boot::isCrashReset(ESP_RST_TASK_WDT));
  TEST_ASSERT_FALSE(n3boot::isCrashReset(ESP_RST_POWERON));
  TEST_ASSERT_FALSE(n3boot::isCrashReset(ESP_RST_SW));
  TEST_ASSERT_FALSE(n3boot::isCrashReset(ESP_RST_BROWNOUT));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ota_state_labels_match_server_vocabulary);
  RUN_TEST(test_safe_mode_after_three_failed_boots);
  RUN_TEST(test_safe_mode_threshold_zero_disables);
  RUN_TEST(test_crash_reset_classification);
  return UNITY_END();
}
