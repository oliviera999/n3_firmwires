#include <unity.h>

// Tests de n3clock (shared/n3_time/n3_sleep_clock.h) — correction d'horloge au réveil.
// Incident FFP3 06/11/2026 : +2 045 s puis +4 090 s d'avance (un cycle de veille compté deux
// fois), premières requêtes HMAC rejetées (401 « X-Sig invalide »). La règle : n'ajouter que ce
// que l'horloge système n'a PAS déjà compté pendant le light sleep.
#include "n3_sleep_clock.h"

using namespace n3clock;

void setUp(void) {}
void tearDown(void) {}

// Cas de l'incident : light sleep de 2045 s, l'horloge système a déjà avancé de 2045 s
// (ESP-IDF compense via le RTC). L'ancien code ajoutait encore 2045 s → double comptage.
void test_no_correction_when_clock_already_advanced(void) {
  TEST_ASSERT_EQUAL_INT32(0, sleepCorrectionSeconds(1700000000UL, 1700002045UL, 2045));
  TEST_ASSERT_EQUAL_INT32(0, sleepCorrectionSeconds(1700000000UL, 1700002044UL, 2045));  // deadband
}

// Horloge figée pendant la veille (cas où l'horloge n'est pas compensée) : on ajoute tout.
void test_full_correction_when_clock_frozen(void) {
  TEST_ASSERT_EQUAL_INT32(2045, sleepCorrectionSeconds(1700000000UL, 1700000000UL, 2045));
}

// Compensation partielle : seul le reste manquant est ajouté.
void test_partial_correction(void) {
  TEST_ASSERT_EQUAL_INT32(45, sleepCorrectionSeconds(1700000000UL, 1700002000UL, 2045));
}

// Horloge en retard de moins que la bande morte : pas de correction (gigue).
void test_deadband(void) {
  TEST_ASSERT_EQUAL_INT32(0, sleepCorrectionSeconds(1700000000UL, 1700002043UL, 2045));
  TEST_ASSERT_EQUAL_INT32(3, sleepCorrectionSeconds(1700000000UL, 1700002042UL, 2045));
}

// Horloge reculée pendant la veille (resynchro externe) : on ne touche à rien.
void test_clock_went_backwards_is_ignored(void) {
  TEST_ASSERT_EQUAL_INT32(0, sleepCorrectionSeconds(1700002000UL, 1700000000UL, 2045));
}

void test_clock_trusted_rules(void) {
  TEST_ASSERT_TRUE(clockTrusted(true, 0xFFFFFFFFUL, 3600));
  TEST_ASSERT_TRUE(clockTrusted(false, 120, 3600));
  TEST_ASSERT_FALSE(clockTrusted(false, 3601, 3600));
  TEST_ASSERT_FALSE(clockTrusted(false, 0xFFFFFFFFUL, 3600));  // jamais d'heure serveur
}

// Décalage module − serveur, latence compensée de moitié : module en avance de 2045 s.
void test_server_offset_and_apply_threshold(void) {
  TEST_ASSERT_EQUAL_INT32(2045, serverOffsetSeconds(1700002045UL, 1700000000UL, 0));
  TEST_ASSERT_EQUAL_INT32(2044, serverOffsetSeconds(1700002045UL, 1700000000UL, 2000));  // +1 s RTT/2
  TEST_ASSERT_EQUAL_INT32(-30, serverOffsetSeconds(1700000000UL, 1700000030UL, 0));
  TEST_ASSERT_TRUE(shouldApplyServerTime(2045, 30));
  TEST_ASSERT_TRUE(shouldApplyServerTime(-31, 30));
  TEST_ASSERT_FALSE(shouldApplyServerTime(5, 30));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_no_correction_when_clock_already_advanced);
  RUN_TEST(test_full_correction_when_clock_frozen);
  RUN_TEST(test_partial_correction);
  RUN_TEST(test_deadband);
  RUN_TEST(test_clock_went_backwards_is_ignored);
  RUN_TEST(test_clock_trusted_rules);
  RUN_TEST(test_server_offset_and_apply_threshold);
  return UNITY_END();
}
