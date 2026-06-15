#include <unity.h>

// Tests de RefillEfficiency (automatism/refill_efficiency.h), décision d'évaluation
// d'efficacité extraite de Automatism::handleRefillMaxDurationStop (chantier C4).
// Sécurité : un mauvais arbitrage = pompe jamais verrouillée malgré inefficacité
// répétée (usure / pompe à sec) ou verrou prématuré (remplissage bloqué à tort).
#include "../../include/automatism/refill_efficiency.h"

using namespace RefillEfficiency;

static const uint8_t MAXR = 5;

void setUp(void) {}
void tearDown(void) {}

// Niveau inconnu -> NoEval (pas d'évaluation).
void test_no_eval_when_level_unknown(void) {
  TEST_ASSERT_EQUAL(Decision::NoEval, evaluate(false, 10, 0, MAXR));
  TEST_ASSERT_EQUAL(Decision::NoEval, evaluate(false, -5, 4, MAXR));
}

// Amélioration suffisante (>=1) -> Effective.
void test_effective_when_improved(void) {
  TEST_ASSERT_EQUAL(Decision::Effective, evaluate(true, 1, 0, MAXR));   // == 1, pas < 1
  TEST_ASSERT_EQUAL(Decision::Effective, evaluate(true, 50, 3, MAXR));
}

// Pas d'amélioration, essais pas épuisés -> Inefficient (sans verrou).
void test_inefficient_when_retries_left(void) {
  TEST_ASSERT_EQUAL(Decision::Inefficient, evaluate(true, 0, 0, MAXR));   // 0+1=1 < 5
  TEST_ASSERT_EQUAL(Decision::Inefficient, evaluate(true, 0, 3, MAXR));   // 3+1=4 < 5
  TEST_ASSERT_EQUAL(Decision::Inefficient, evaluate(true, -2, 1, MAXR));  // amélioration négative
}

// Pas d'amélioration, l'incrément atteint le plafond -> InefficientLock.
void test_lock_when_increment_reaches_max(void) {
  TEST_ASSERT_EQUAL(Decision::InefficientLock, evaluate(true, 0, 4, MAXR));  // 4+1=5 >= 5
  TEST_ASSERT_EQUAL(Decision::InefficientLock, evaluate(true, 0, 5, MAXR));  // déjà au max
}

// Bord : amélioration nulle est inefficace (comparaison stricte < 1).
void test_zero_improvement_is_inefficient(void) {
  TEST_ASSERT_EQUAL(Decision::Inefficient, evaluate(true, 0, 0, MAXR));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_eval_when_level_unknown);
  RUN_TEST(test_effective_when_improved);
  RUN_TEST(test_inefficient_when_retries_left);
  RUN_TEST(test_lock_when_increment_reaches_max);
  RUN_TEST(test_zero_improvement_is_inefficient);
  return UNITY_END();
}
