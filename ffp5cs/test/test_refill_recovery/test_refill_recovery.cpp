#include <unity.h>

// Tests de RefillRecovery (automatism/refill_recovery.h), décision de récupération auto
// extraite de Automatism::handleRefillAutomaticRecovery (chantier C4). Sécurité : un
// mauvais arbitrage = pompe débloquée trop tôt (réserve encore basse -> à sec) ou jamais
// débloquée (remplissage définitivement perdu après inefficacité).
#include "../../include/automatism/refill_recovery.h"

using namespace RefillRecovery;

static const uint8_t MAXR = 5;
static const uint32_t DEB = 30000UL;     // debounce 30 s
static const uint16_t OK_THR = 240;      // distance réserve OK si < 240 mm

void setUp(void) {}
void tearDown(void) {}

// Cas nominal : verrouillé, essais épuisés, debounce écoulé, réserve OK -> recover.
void test_recover_when_all_conditions(void) {
  TEST_ASSERT_TRUE(shouldRecover(true, MAXR, MAXR, 100000, 0, DEB, 200, OK_THR));
}

// Pas verrouillé -> non.
void test_no_recover_when_not_locked(void) {
  TEST_ASSERT_FALSE(shouldRecover(false, MAXR, MAXR, 100000, 0, DEB, 200, OK_THR));
}

// Essais pas épuisés -> non (la récup ne concerne que le verrou « inefficace »).
void test_no_recover_when_retries_below_max(void) {
  TEST_ASSERT_FALSE(shouldRecover(true, MAXR - 1, MAXR, 100000, 0, DEB, 200, OK_THR));
}

// Debounce non écoulé -> non.
void test_no_recover_within_debounce(void) {
  // now - last = 20 s <= 30 s
  TEST_ASSERT_FALSE(shouldRecover(true, MAXR, MAXR, 50000, 30000, DEB, 200, OK_THR));
  // bord strict : exactement 30 s -> pas encore (> requis)
  TEST_ASSERT_FALSE(shouldRecover(true, MAXR, MAXR, 30000, 0, DEB, 200, OK_THR));
}

// Réserve pas assez basse (distance >= seuil OK) -> non.
void test_no_recover_when_reserve_not_ok(void) {
  TEST_ASSERT_FALSE(shouldRecover(true, MAXR, MAXR, 100000, 0, DEB, 240, OK_THR));  // == seuil
  TEST_ASSERT_FALSE(shouldRecover(true, MAXR, MAXR, 100000, 0, DEB, 300, OK_THR));
}

// Juste après le debounce (31 s) avec réserve OK -> recover.
void test_recover_just_after_debounce(void) {
  TEST_ASSERT_TRUE(shouldRecover(true, MAXR, MAXR, 31001, 0, DEB, 239, OK_THR));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_recover_when_all_conditions);
  RUN_TEST(test_no_recover_when_not_locked);
  RUN_TEST(test_no_recover_when_retries_below_max);
  RUN_TEST(test_no_recover_within_debounce);
  RUN_TEST(test_no_recover_when_reserve_not_ok);
  RUN_TEST(test_recover_just_after_debounce);
  return UNITY_END();
}
