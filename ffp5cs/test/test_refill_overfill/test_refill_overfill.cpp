#include <unity.h>

// Tests de RefillOverfill (automatism/refill_overfill.h), décision de sécurité « aquarium
// trop plein » extraite de Automatism::handleRefillAquariumOverfillSecurity (chantier C4).
// Sécurité : un mauvais arbitrage = remplissage autorisé alors que l'aquarium déborde, ou
// pompe verrouillée à tort (jamais de remplissage).
#include "../../include/automatism/refill_overfill.h"

using namespace RefillOverfill;

// limFlood = 80 mm : distance < 80 = trop plein (niveau haut).
static const uint16_t LIMF = 80;

void setUp(void) {}
void tearDown(void) {}

// Niveau inconnu -> None.
void test_none_when_unknown(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(false, 50, LIMF, false, false));
  TEST_ASSERT_EQUAL(Decision::None, evaluate(false, 200, LIMF, true, false));
}

// Trop plein (distance < limFlood), pas encore verrouillé -> Lock.
void test_lock_when_overfull_and_not_locked(void) {
  TEST_ASSERT_EQUAL(Decision::Lock, evaluate(true, 50, LIMF, /*lockedForOverfill=*/false, false));
}

// Trop plein mais déjà verrouillé pour ce motif -> None (pas de re-lock).
void test_none_when_overfull_already_locked(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(true, 50, LIMF, /*lockedForOverfill=*/true, false));
}

// Niveau revenu OK, verrou trop-plein actif, plus en flood -> Unlock.
void test_unlock_when_level_ok(void) {
  TEST_ASSERT_EQUAL(Decision::Unlock, evaluate(true, 200, LIMF, /*lockedForOverfill=*/true, /*inFlood=*/false));
}

// Niveau OK mais flood encore actif -> None (on ne déverrouille pas).
void test_none_when_ok_but_in_flood(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(true, 200, LIMF, /*lockedForOverfill=*/true, /*inFlood=*/true));
}

// Niveau OK mais pas verrouillé pour trop-plein -> None.
void test_none_when_ok_and_not_locked(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(true, 200, LIMF, /*lockedForOverfill=*/false, false));
}

// Bord : distance == limFlood -> pas trop plein (comparaison stricte <) -> pas de Lock.
void test_at_threshold_not_overfull(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(true, 80, LIMF, false, false));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_none_when_unknown);
  RUN_TEST(test_lock_when_overfull_and_not_locked);
  RUN_TEST(test_none_when_overfull_already_locked);
  RUN_TEST(test_unlock_when_level_ok);
  RUN_TEST(test_none_when_ok_but_in_flood);
  RUN_TEST(test_none_when_ok_and_not_locked);
  RUN_TEST(test_at_threshold_not_overfull);
  return UNITY_END();
}
