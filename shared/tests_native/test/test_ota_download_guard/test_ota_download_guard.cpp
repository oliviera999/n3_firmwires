#include <unity.h>

#include "n3_ota_download_guard.h"

void setUp(void) {}
void tearDown(void) {}

void test_allows_active_download(void) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(N3OtaDownloadTimeout::None),
      static_cast<int>(n3OtaDownloadTimeout(20000, 0, 15000, 30000, 300000)));
}

void test_detects_idle_connection(void) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(N3OtaDownloadTimeout::Idle),
      static_cast<int>(n3OtaDownloadTimeout(45000, 0, 15000, 30000, 300000)));
}

void test_detects_slowloris_total_duration(void) {
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(N3OtaDownloadTimeout::Total),
      static_cast<int>(n3OtaDownloadTimeout(300000, 0, 299999, 30000, 300000)));
}

void test_handles_millis_wraparound(void) {
  const uint32_t startedAt = UINT32_MAX - 10000U;
  const uint32_t now = 25000U;
  TEST_ASSERT_EQUAL_INT(
      static_cast<int>(N3OtaDownloadTimeout::Idle),
      static_cast<int>(n3OtaDownloadTimeout(now, startedAt, startedAt, 30000, 300000)));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_allows_active_download);
  RUN_TEST(test_detects_idle_connection);
  RUN_TEST(test_detects_slowloris_total_duration);
  RUN_TEST(test_handles_millis_wraparound);
  return UNITY_END();
}
