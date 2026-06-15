#include <unity.h>
#include <cstring>

// Tests de ResetReason (reset_reason.h), extrait de diagnostics.cpp (audit §3.8).
// isCrash() pilote le logging de crash / alerting : la liste des causes « crash »
// doit rester exacte. L'enum esp_reset_reason_t vient du mock test/unit/esp_system.h.
#include "../../include/reset_reason.h"

using namespace ResetReason;

void setUp(void) {}
void tearDown(void) {}

void test_string_mapping_known(void) {
  TEST_ASSERT_EQUAL_STRING("POWERON",   resetReasonToString(ESP_RST_POWERON));
  TEST_ASSERT_EQUAL_STRING("SW",        resetReasonToString(ESP_RST_SW));
  TEST_ASSERT_EQUAL_STRING("PANIC",     resetReasonToString(ESP_RST_PANIC));
  TEST_ASSERT_EQUAL_STRING("INT_WDT",   resetReasonToString(ESP_RST_INT_WDT));
  TEST_ASSERT_EQUAL_STRING("TASK_WDT",  resetReasonToString(ESP_RST_TASK_WDT));
  TEST_ASSERT_EQUAL_STRING("WDT",       resetReasonToString(ESP_RST_WDT));
  TEST_ASSERT_EQUAL_STRING("DEEPSLEEP", resetReasonToString(ESP_RST_DEEPSLEEP));
  TEST_ASSERT_EQUAL_STRING("BROWNOUT",  resetReasonToString(ESP_RST_BROWNOUT));
  TEST_ASSERT_EQUAL_STRING("SDIO",      resetReasonToString(ESP_RST_SDIO));
}

void test_string_mapping_unmapped_is_other(void) {
  TEST_ASSERT_EQUAL_STRING("OTHER", resetReasonToString(ESP_RST_UNKNOWN));
  TEST_ASSERT_EQUAL_STRING("OTHER", resetReasonToString(ESP_RST_EXT));
}

void test_isCrash_true_cases(void) {
  TEST_ASSERT_TRUE(isCrash(ESP_RST_PANIC));
  TEST_ASSERT_TRUE(isCrash(ESP_RST_INT_WDT));
  TEST_ASSERT_TRUE(isCrash(ESP_RST_TASK_WDT));
  TEST_ASSERT_TRUE(isCrash(ESP_RST_WDT));
}

void test_isCrash_false_cases(void) {
  TEST_ASSERT_FALSE(isCrash(ESP_RST_POWERON));   // démarrage normal
  TEST_ASSERT_FALSE(isCrash(ESP_RST_SW));        // reboot logiciel volontaire
  TEST_ASSERT_FALSE(isCrash(ESP_RST_DEEPSLEEP)); // réveil
  TEST_ASSERT_FALSE(isCrash(ESP_RST_BROWNOUT));  // alim (pas un crash logiciel)
  TEST_ASSERT_FALSE(isCrash(ESP_RST_SDIO));
  TEST_ASSERT_FALSE(isCrash(ESP_RST_EXT));
  TEST_ASSERT_FALSE(isCrash(ESP_RST_UNKNOWN));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_string_mapping_known);
  RUN_TEST(test_string_mapping_unmapped_is_other);
  RUN_TEST(test_isCrash_true_cases);
  RUN_TEST(test_isCrash_false_cases);
  return UNITY_END();
}
