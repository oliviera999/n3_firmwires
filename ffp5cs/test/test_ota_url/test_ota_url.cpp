#include <unity.h>
#include "test_mocks.h"  // mock Serial (utilisé par ota_url.h)
#include <cstring>

// Tests de OtaUrl::downgradeToHttp (ota_url.h), extrait de ota_manager.cpp
// (audit §3.8). Manipulation in-place via memmove : un off-by-one corromprait
// chaque URL OTA. Couvre conversion, no-ops et gardes.
#include "../../include/ota_url.h"

void setUp(void) {}
void tearDown(void) {}

void test_https_to_http_realistic_url(void) {
  char url[64] = "https://iot.olution.info:8080/ota/firmware.bin";
  OtaUrl::downgradeToHttp(url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("http://iot.olution.info:8080/ota/firmware.bin", url);
}

void test_https_bare_scheme(void) {
  char url[16] = "https://";
  OtaUrl::downgradeToHttp(url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("http://", url);
  TEST_ASSERT_EQUAL_size_t(7, strlen(url));  // un octet de moins
}

void test_http_unchanged(void) {
  char url[32] = "http://host/path";
  OtaUrl::downgradeToHttp(url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("http://host/path", url);
}

void test_other_scheme_unchanged(void) {
  char url[32] = "ftp://host";
  OtaUrl::downgradeToHttp(url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("ftp://host", url);
}

void test_null_buffer_no_crash(void) {
  OtaUrl::downgradeToHttp(nullptr, 32);
  TEST_ASSERT_TRUE(true);
}

void test_too_small_buffer_is_noop(void) {
  char url[8] = "https:/";  // 7 chars, bufSize 8 -> bufSize>=8 mais pas https:// complet
  OtaUrl::downgradeToHttp(url, sizeof(url));
  TEST_ASSERT_EQUAL_STRING("https:/", url);  // pas de match "https://" -> inchangé

  char tiny[4] = "abc";
  OtaUrl::downgradeToHttp(tiny, sizeof(tiny));  // bufSize 4 < 8 -> garde
  TEST_ASSERT_EQUAL_STRING("abc", tiny);
}

void test_idempotent(void) {
  char url[32] = "https://host";
  OtaUrl::downgradeToHttp(url, sizeof(url));
  OtaUrl::downgradeToHttp(url, sizeof(url));  // 2e passe : déjà http -> no-op
  TEST_ASSERT_EQUAL_STRING("http://host", url);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_https_to_http_realistic_url);
  RUN_TEST(test_https_bare_scheme);
  RUN_TEST(test_http_unchanged);
  RUN_TEST(test_other_scheme_unchanged);
  RUN_TEST(test_null_buffer_no_crash);
  RUN_TEST(test_too_small_buffer_is_noop);
  RUN_TEST(test_idempotent);
  return UNITY_END();
}
