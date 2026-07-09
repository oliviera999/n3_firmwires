#include <unity.h>

// Tests de SleepDecision (n3_sleep_decision.h), mutualisé depuis ffp5cs.
// Le délai de sommeil adaptatif pilote autonomie ET disponibilité : un mauvais
// délai = batterie vidée ou réveils manqués.
#include "n3_sleep_decision.h"

using namespace SleepDecision;

// Jeu de valeurs représentatif (secondes).
static const uint32_t NORMAL = 300, ERR = 120, NIGHT = 600, MIN = 60, MAX = 900;

void setUp(void) {}
void tearDown(void) {}

void test_adaptive_disabled_returns_normal(void) {
  // Désactivé : renvoie normalSleep quels que soient les autres signaux.
  TEST_ASSERT_EQUAL_UINT32(NORMAL,
    adaptiveSleepDelay(false, NORMAL, ERR, NIGHT, MIN, MAX, true, true, 5));
}

void test_normal_case(void) {
  TEST_ASSERT_EQUAL_UINT32(NORMAL,
    adaptiveSleepDelay(true, NORMAL, ERR, NIGHT, MIN, MAX, false, false, 0));
}

void test_error_shortens(void) {
  TEST_ASSERT_EQUAL_UINT32(ERR,
    adaptiveSleepDelay(true, NORMAL, ERR, NIGHT, MIN, MAX, true, false, 0));
}

void test_night_overrides_error(void) {
  // Nuit prime sur erreurs -> nightSleep.
  TEST_ASSERT_EQUAL_UINT32(NIGHT,
    adaptiveSleepDelay(true, NORMAL, ERR, NIGHT, MIN, MAX, true, true, 0));
}

void test_wakeup_failures_double(void) {
  // base=NORMAL(300) *2 = 600 <= MAX -> 600
  TEST_ASSERT_EQUAL_UINT32(600,
    adaptiveSleepDelay(true, NORMAL, ERR, NIGHT, MIN, MAX, false, false, 1));
}

void test_wakeup_failures_double_capped_at_max(void) {
  // base=NIGHT(600) *2 = 1200, plafonné à MAX(900)
  TEST_ASSERT_EQUAL_UINT32(MAX,
    adaptiveSleepDelay(true, NORMAL, ERR, NIGHT, MIN, MAX, false, true, 2));
}

void test_clamped_to_min(void) {
  // normalSleep 30 < MIN(60) -> remonté à MIN
  TEST_ASSERT_EQUAL_UINT32(MIN,
    adaptiveSleepDelay(true, 30, ERR, NIGHT, MIN, MAX, false, false, 0));
}

void test_isNightHour(void) {
  TEST_ASSERT_TRUE(isNightHour(19));
  TEST_ASSERT_TRUE(isNightHour(23));
  TEST_ASSERT_TRUE(isNightHour(0));
  TEST_ASSERT_TRUE(isNightHour(5));
  TEST_ASSERT_FALSE(isNightHour(6));   // 6h = jour
  TEST_ASSERT_FALSE(isNightHour(12));
  TEST_ASSERT_FALSE(isNightHour(18));  // 18h = encore jour
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_adaptive_disabled_returns_normal);
  RUN_TEST(test_normal_case);
  RUN_TEST(test_error_shortens);
  RUN_TEST(test_night_overrides_error);
  RUN_TEST(test_wakeup_failures_double);
  RUN_TEST(test_wakeup_failures_double_capped_at_max);
  RUN_TEST(test_clamped_to_min);
  RUN_TEST(test_isNightHour);
  return UNITY_END();
}
