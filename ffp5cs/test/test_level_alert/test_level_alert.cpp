#include <unity.h>

// Tests de LevelAlert (automatism/level_alert.h), extrait de Automatism::handleAlerts
// (chantier C4 — découpe god-class). Alerte niveau d'eau seuil + hystérésis, factorisée
// entre aquarium et réserve : un mauvais seuil = alerte manquée ou spam.
#include "../../include/automatism/level_alert.h"

using namespace LevelAlert;

// Seuils représentatifs : alerte au-dessus de 300 mm, fin d'alerte à <= 250 mm.
static const uint16_t ALERT = 300;
static const uint16_t CLEAR = 250;

void setUp(void) {}
void tearDown(void) {}

// Distance au-dessus du seuil, pas encore alerté -> Raise.
void test_above_threshold_raises(void) {
  TEST_ASSERT_EQUAL(Decision::Raise, evaluate(350, ALERT, CLEAR, /*alreadyAlerted=*/false));
}

// Distance au-dessus du seuil mais déjà alerté -> None (anti-spam).
void test_above_threshold_already_alerted_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(350, ALERT, CLEAR, /*alreadyAlerted=*/true));
}

// Distance revenue sous le seuil de fin, alerte active -> Clear.
void test_below_clear_when_alerted_clears(void) {
  TEST_ASSERT_EQUAL(Decision::Clear, evaluate(200, ALERT, CLEAR, /*alreadyAlerted=*/true));
}

// Distance sous le seuil de fin mais pas en alerte -> None.
void test_below_clear_not_alerted_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(200, ALERT, CLEAR, /*alreadyAlerted=*/false));
}

// Bande morte (entre fin et alerte), pas alerté -> None.
void test_deadband_not_alerted_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(275, ALERT, CLEAR, /*alreadyAlerted=*/false));
}

// Bande morte, déjà alerté -> reste alerté (None).
void test_deadband_alerted_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(275, ALERT, CLEAR, /*alreadyAlerted=*/true));
}

// Bord : distance == seuil d'alerte -> pas de Raise (comparaison stricte >).
void test_at_alert_threshold_no_raise(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(300, ALERT, CLEAR, /*alreadyAlerted=*/false));
}

// Bord : distance == seuil de fin -> Clear (comparaison <=).
void test_at_clear_threshold_clears(void) {
  TEST_ASSERT_EQUAL(Decision::Clear, evaluate(250, ALERT, CLEAR, /*alreadyAlerted=*/true));
}

// Cycle complet : montée -> Raise, bande morte -> maintien, retour -> Clear.
void test_full_cycle(void) {
  bool alerted = false;
  TEST_ASSERT_EQUAL(Decision::Raise, evaluate(320, ALERT, CLEAR, alerted));
  alerted = true;  // l'appelant a levé l'alerte
  TEST_ASSERT_EQUAL(Decision::None, evaluate(280, ALERT, CLEAR, alerted));  // bande morte
  TEST_ASSERT_EQUAL(Decision::Clear, evaluate(240, ALERT, CLEAR, alerted));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_above_threshold_raises);
  RUN_TEST(test_above_threshold_already_alerted_none);
  RUN_TEST(test_below_clear_when_alerted_clears);
  RUN_TEST(test_below_clear_not_alerted_none);
  RUN_TEST(test_deadband_not_alerted_none);
  RUN_TEST(test_deadband_alerted_none);
  RUN_TEST(test_at_alert_threshold_no_raise);
  RUN_TEST(test_at_clear_threshold_clears);
  RUN_TEST(test_full_cycle);
  return UNITY_END();
}
