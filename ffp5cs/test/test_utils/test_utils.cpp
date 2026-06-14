// Profil de test (évite le static_assert PROD de config_secrets.h).
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS

#include <unity.h>
#include <cstring>
#include <cstdint>

// Tests des primitives utilitaires pures de Utils:: (config_system.h), désormais
// accessibles en natif grâce au stub FreeRTOS (test/unit/freertos/).
// safeStrncpy est utilisé dans tout le firmware : sa null-termination garantie
// et sa troncature sont critiques (corruption mémoire / strings non terminées).
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------- Utils::safeStrncpy ----------------

void test_safeStrncpy_exact_and_room(void) {
  char dest[16];
  Utils::safeStrncpy(dest, "hello", sizeof(dest));
  TEST_ASSERT_EQUAL_STRING("hello", dest);
}

void test_safeStrncpy_truncates_with_null_term(void) {
  char dest[4];  // 3 chars utiles + '\0'
  Utils::safeStrncpy(dest, "hello", sizeof(dest));
  TEST_ASSERT_EQUAL_STRING("hel", dest);
  TEST_ASSERT_EQUAL_size_t(3, strlen(dest));  // toujours terminé
}

void test_safeStrncpy_size_one_yields_empty(void) {
  char dest[2] = "X";
  Utils::safeStrncpy(dest, "hello", 1);  // destSize=1 -> uniquement '\0'
  TEST_ASSERT_EQUAL_STRING("", dest);
}

void test_safeStrncpy_size_zero_is_noop(void) {
  char dest[8] = "ABC";
  Utils::safeStrncpy(dest, "hello", 0);  // ne doit pas toucher dest
  TEST_ASSERT_EQUAL_STRING("ABC", dest);
}

void test_safeStrncpy_empty_source(void) {
  char dest[8] = "ZZZ";
  Utils::safeStrncpy(dest, "", sizeof(dest));
  TEST_ASSERT_EQUAL_STRING("", dest);
}

// ---------------- Utils::formatIP ----------------

void test_formatIP_typical(void) {
  uint8_t ip[4] = {192, 168, 1, 42};
  char buf[16] = {0};
  Utils::formatIP(ip, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("192.168.1.42", buf);
}

void test_formatIP_bounds(void) {
  uint8_t zero[4] = {0, 0, 0, 0};
  uint8_t max[4] = {255, 255, 255, 255};
  char buf[16] = {0};
  Utils::formatIP(zero, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0.0.0.0", buf);
  Utils::formatIP(max, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("255.255.255.255", buf);
}

void test_formatIP_truncation_no_overflow(void) {
  uint8_t ip[4] = {255, 255, 255, 255};
  char buf[8];
  memset(buf, '#', sizeof(buf));
  Utils::formatIP(ip, buf, sizeof(buf));
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));  // terminé, pas de débordement
}

// ---------------- Utils::getProfileName / getSystemInfo ----------------

void test_getProfileName_reflects_build_profile(void) {
  // Build natif compilé avec PROFILE_BETA.
  TEST_ASSERT_EQUAL_STRING("BETA", Utils::getProfileName());
}

void test_getSystemInfo_contains_version_board_profile(void) {
  char buf[64] = {0};
  Utils::getSystemInfo(buf, sizeof(buf));
  TEST_ASSERT_NOT_NULL(strstr(buf, "FFP5CS v"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "BETA"));
  TEST_ASSERT_NOT_NULL(strstr(buf, "esp32-wroom"));  // build natif = WROOM
}

void test_getSystemInfo_truncation_no_overflow(void) {
  char buf[10];
  memset(buf, '#', sizeof(buf));
  Utils::getSystemInfo(buf, sizeof(buf));
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_safeStrncpy_exact_and_room);
  RUN_TEST(test_safeStrncpy_truncates_with_null_term);
  RUN_TEST(test_safeStrncpy_size_one_yields_empty);
  RUN_TEST(test_safeStrncpy_size_zero_is_noop);
  RUN_TEST(test_safeStrncpy_empty_source);
  RUN_TEST(test_formatIP_typical);
  RUN_TEST(test_formatIP_bounds);
  RUN_TEST(test_formatIP_truncation_no_overflow);
  RUN_TEST(test_getProfileName_reflects_build_profile);
  RUN_TEST(test_getSystemInfo_contains_version_board_profile);
  RUN_TEST(test_getSystemInfo_truncation_no_overflow);
  return UNITY_END();
}
