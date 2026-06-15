#include <unity.h>

// Tests de FeedingTiming (automatism/feeding_timing.h), formule de durée de cycle de
// nourrissage extraite (chantier C4) et dédupliquée de 6 sites. Un mauvais calcul =
// phase de nourrissage trop courte/longue (servo coupé en plein cycle, ou compte à
// rebours OLED faux).
#include "../../include/automatism/feeding_timing.h"

using namespace FeedingTiming;

void setUp(void) {}
void tearDown(void) {}

// Parité avec l'ancienne formule inline (durSec + durSec/2) * 1000, division entière.
void test_known_values(void) {
  TEST_ASSERT_EQUAL_UINT32(0, cycleDurationMs(0));          // 0 + 0
  TEST_ASSERT_EQUAL_UINT32(1000, cycleDurationMs(1));       // 1 + 0 (1/2=0) = 1
  TEST_ASSERT_EQUAL_UINT32(3000, cycleDurationMs(2));       // 2 + 1 = 3
  TEST_ASSERT_EQUAL_UINT32(4000, cycleDurationMs(3));       // 3 + 1 = 4 (3/2=1)
  TEST_ASSERT_EQUAL_UINT32(7000, cycleDurationMs(5));       // 5 + 2 = 7 (5/2=2)
  TEST_ASSERT_EQUAL_UINT32(15000, cycleDurationMs(10));     // 10 + 5 = 15
  TEST_ASSERT_EQUAL_UINT32(90000, cycleDurationMs(60));     // 60 + 30 = 90
}

// La division entière tronque (impair) — comportement historique conservé.
void test_odd_truncation(void) {
  TEST_ASSERT_EQUAL_UINT32(10000, cycleDurationMs(7));      // 7 + 3 = 10 (7/2=3)
  TEST_ASSERT_EQUAL_UINT32(13000, cycleDurationMs(9));      // 9 + 4 = 13 (9/2=4)
}

// Croissance monotone (sécurité : plus de durée ⇒ cycle plus long).
void test_monotonic(void) {
  uint32_t prev = 0;
  for (uint16_t d = 1; d <= 120; ++d) {
    uint32_t cur = cycleDurationMs(d);
    TEST_ASSERT_TRUE(cur >= prev);
    prev = cur;
  }
}

// Pas de débordement pour une grande valeur plausible (uint32_t large).
void test_large_value_no_overflow(void) {
  // 600 s -> 900 s -> 900000 ms, tient largement dans uint32_t.
  TEST_ASSERT_EQUAL_UINT32(900000UL, cycleDurationMs(600));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_known_values);
  RUN_TEST(test_odd_truncation);
  RUN_TEST(test_monotonic);
  RUN_TEST(test_large_value_no_overflow);
  return UNITY_END();
}
