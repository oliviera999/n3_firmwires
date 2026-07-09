#include <cstdint>  // types entiers AVANT tout en-tête (pitfall)
#include <unity.h>

// Tests de UptimeFormat (uptime_format.h), extrait de mailer.cpp
// (helper formatUptime du bloc d'infos système des e-mails). Logique pure :
// conversion d'une durée en millisecondes vers "Jd HH:MM:SS".
// On vérifie aussi la PARITÉ avec une transcription du code inline d'origine
// (brute-force sur de nombreuses durées).
#include "n3_uptime_format.h"

#include <cstdio>
#include <cstring>

using namespace UptimeFormat;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Transcription de référence du code inline d'origine (mailer.cpp avant
// extraction) — sert d'oracle pour les boucles de parité.
// =============================================================================
static void refFormatUptime(unsigned long ms, char* buf, size_t bufSize) {
  unsigned long totalSec = ms / 1000UL;
  unsigned int days = totalSec / 86400UL;
  totalSec %= 86400UL;
  unsigned int hours = totalSec / 3600UL;
  totalSec %= 3600UL;
  unsigned int mins = totalSec / 60UL;
  unsigned int secs = totalSec % 60UL;
  snprintf(buf, bufSize, "%ud %02u:%02u:%02u", days, hours, mins, secs);
}

// =============================================================================
// Cas nominaux
// =============================================================================

void test_zero(void) {
  char buf[48];
  formatUptime(0, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:00:00", buf);
}

void test_under_one_second(void) {
  // < 1000 ms -> 0 seconde (division entière).
  char buf[48];
  formatUptime(999, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:00:00", buf);
}

void test_one_second(void) {
  char buf[48];
  formatUptime(1000, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:00:01", buf);
}

void test_seconds_padding(void) {
  char buf[48];
  formatUptime(5000, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:00:05", buf);
}

void test_one_minute(void) {
  char buf[48];
  formatUptime(60UL * 1000UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:01:00", buf);
}

void test_one_hour(void) {
  char buf[48];
  formatUptime(3600UL * 1000UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 01:00:00", buf);
}

void test_one_day(void) {
  char buf[48];
  formatUptime(86400UL * 1000UL, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1d 00:00:00", buf);
}

void test_mixed_components(void) {
  // 1 jour, 2h, 3min, 4s = 93784 s.
  char buf[48];
  unsigned long ms = (86400UL + 2UL * 3600UL + 3UL * 60UL + 4UL) * 1000UL;
  formatUptime(ms, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("1d 02:03:04", buf);
}

void test_two_digit_hms(void) {
  // 23h 59min 59s, 0 jour.
  char buf[48];
  unsigned long ms = (23UL * 3600UL + 59UL * 60UL + 59UL) * 1000UL;
  formatUptime(ms, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 23:59:59", buf);
}

void test_multi_digit_days(void) {
  // 123 jours.
  char buf[48];
  unsigned long ms = 123UL * 86400UL * 1000UL;
  formatUptime(ms, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("123d 00:00:00", buf);
}

void test_millis_truncated_to_second(void) {
  // Les millisecondes < 1000 sont ignorées (division par 1000).
  char buf[48];
  formatUptime(1999, buf, sizeof(buf));
  TEST_ASSERT_EQUAL_STRING("0d 00:00:01", buf);
}

// =============================================================================
// Cas limites du buffer / garde-fous
// =============================================================================

void test_null_buffer_safe(void) {
  // buf == nullptr -> renvoie nullptr, pas de crash.
  TEST_ASSERT_NULL(formatUptime(123456, nullptr, 48));
}

void test_zero_size_buffer_safe(void) {
  // bufSize == 0 -> ne touche pas le buffer, renvoie buf inchangé.
  char buf[4] = {'X', 'Y', 'Z', '\0'};
  char* r = formatUptime(123456, buf, 0);
  TEST_ASSERT_EQUAL_PTR(buf, r);
  TEST_ASSERT_EQUAL_STRING("XYZ", buf);  // buffer non modifié
}

void test_small_buffer_truncates_and_terminates(void) {
  // snprintf tronque proprement et termine par '\0' (parité libc).
  char buf[5];
  formatUptime(86400UL * 1000UL, buf, sizeof(buf));  // "1d 00:00:00"
  TEST_ASSERT_EQUAL_CHAR('\0', buf[4]);
  TEST_ASSERT_EQUAL_STRING("1d 0", buf);
}

// =============================================================================
// Parité brute-force avec la transcription d'origine
// =============================================================================

void test_parity_bruteforce(void) {
  static const unsigned long cases[] = {
    0UL, 1UL, 999UL, 1000UL, 1500UL, 5000UL,
    60UL * 1000UL,
    3600UL * 1000UL,
    86400UL * 1000UL,
    (86400UL + 2UL * 3600UL + 3UL * 60UL + 4UL) * 1000UL,
    (23UL * 3600UL + 59UL * 60UL + 59UL) * 1000UL,
    123UL * 86400UL * 1000UL,
    4294967295UL,  // ULONG_MAX possible sur plateforme 32 bits (rollover millis)
  };
  const int n = (int)(sizeof(cases) / sizeof(cases[0]));
  for (int i = 0; i < n; ++i) {
    char got[48];
    char ref[48];
    formatUptime(cases[i], got, sizeof(got));
    refFormatUptime(cases[i], ref, sizeof(ref));
    TEST_ASSERT_EQUAL_STRING(ref, got);
  }
  // Balaye un éventail de secondes et compare au modèle d'origine.
  for (unsigned long s = 0; s <= 200000UL; s += 137UL) {
    char got[48];
    char ref[48];
    unsigned long ms = s * 1000UL;
    formatUptime(ms, got, sizeof(got));
    refFormatUptime(ms, ref, sizeof(ref));
    TEST_ASSERT_EQUAL_STRING(ref, got);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_zero);
  RUN_TEST(test_under_one_second);
  RUN_TEST(test_one_second);
  RUN_TEST(test_seconds_padding);
  RUN_TEST(test_one_minute);
  RUN_TEST(test_one_hour);
  RUN_TEST(test_one_day);
  RUN_TEST(test_mixed_components);
  RUN_TEST(test_two_digit_hms);
  RUN_TEST(test_multi_digit_days);
  RUN_TEST(test_millis_truncated_to_second);
  RUN_TEST(test_null_buffer_safe);
  RUN_TEST(test_zero_size_buffer_safe);
  RUN_TEST(test_small_buffer_truncates_and_terminates);
  RUN_TEST(test_parity_bruteforce);
  return UNITY_END();
}
