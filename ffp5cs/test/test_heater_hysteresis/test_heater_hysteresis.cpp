#include <unity.h>

// Tests de HeaterControl (automatism/heater_hysteresis.h), extrait de
// Automatism::handleAlerts (chantier C4 — découpe god-class). Régulation chauffage
// par hystérésis : un mauvais seuil/bande morte = battement du relais (usure) ou
// dérive thermique de l'aquarium.
#include "../../include/automatism/heater_hysteresis.h"

using namespace HeaterControl;

// Seuil représentatif : 24 °C, hystérésis 2 °C (OFF au-dessus de 26 °C).
static const float THRESHOLD = 24.0f;
static const float HYST = 2.0f;

void setUp(void) {}
void tearDown(void) {}

// Sous le seuil, chauffage éteint -> allumage.
void test_below_threshold_turns_on(void) {
  TEST_ASSERT_EQUAL(Decision::TurnOn, evaluate(23.5f, THRESHOLD, HYST, /*currentlyOn=*/false));
}

// Sous le seuil mais déjà allumé -> rien (pas de re-déclenchement).
void test_below_threshold_already_on_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(23.5f, THRESHOLD, HYST, /*currentlyOn=*/true));
}

// Au-dessus de seuil+hystérésis, chauffage allumé -> extinction.
void test_above_hyst_turns_off(void) {
  TEST_ASSERT_EQUAL(Decision::TurnOff, evaluate(26.5f, THRESHOLD, HYST, /*currentlyOn=*/true));
}

// Au-dessus de seuil+hystérésis mais déjà éteint -> rien.
void test_above_hyst_already_off_none(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(26.5f, THRESHOLD, HYST, /*currentlyOn=*/false));
}

// Bande morte (entre seuil et seuil+hyst), chauffage allumé -> reste allumé (None).
void test_deadband_stays_on(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(25.0f, THRESHOLD, HYST, /*currentlyOn=*/true));
}

// Bande morte, chauffage éteint -> reste éteint (None).
void test_deadband_stays_off(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(25.0f, THRESHOLD, HYST, /*currentlyOn=*/false));
}

// Bord bas : température == seuil -> pas d'allumage (comparaison stricte <).
void test_at_threshold_no_turn_on(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(24.0f, THRESHOLD, HYST, /*currentlyOn=*/false));
}

// Bord haut : température == seuil+hystérésis -> pas d'extinction (comparaison stricte >).
void test_at_upper_bound_no_turn_off(void) {
  TEST_ASSERT_EQUAL(Decision::None, evaluate(26.0f, THRESHOLD, HYST, /*currentlyOn=*/true));
}

// Cycle complet : froid -> ON, se réchauffe en bande morte -> ON maintenu, dépasse -> OFF.
void test_full_cycle(void) {
  bool on = false;
  Decision d = evaluate(22.0f, THRESHOLD, HYST, on);
  TEST_ASSERT_EQUAL(Decision::TurnOn, d);
  on = true;  // l'appelant a allumé
  TEST_ASSERT_EQUAL(Decision::None, evaluate(25.0f, THRESHOLD, HYST, on));   // bande morte
  d = evaluate(26.1f, THRESHOLD, HYST, on);
  TEST_ASSERT_EQUAL(Decision::TurnOff, d);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_below_threshold_turns_on);
  RUN_TEST(test_below_threshold_already_on_none);
  RUN_TEST(test_above_hyst_turns_off);
  RUN_TEST(test_above_hyst_already_off_none);
  RUN_TEST(test_deadband_stays_on);
  RUN_TEST(test_deadband_stays_off);
  RUN_TEST(test_at_threshold_no_turn_on);
  RUN_TEST(test_at_upper_bound_no_turn_off);
  RUN_TEST(test_full_cycle);
  return UNITY_END();
}
