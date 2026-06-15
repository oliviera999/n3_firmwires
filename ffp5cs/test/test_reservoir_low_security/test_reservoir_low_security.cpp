#include <unity.h>

// Tests de ReservoirLowSecurity (automatism/reservoir_low_security.h), extrait de
// Automatism::handleRefillReservoirLowSecurity (chantier C4 — découpe god-class).
// Machine de debounce + hystérésis « réserve basse » : un mauvais comptage =
// verrouillage pompe intempestif (réserve OK marquée basse) ou non-protection
// (réserve vide non détectée -> pompe à sec).
#include "../../include/automatism/reservoir_low_security.h"

using namespace ReservoirLowSecurity;

// Seuils représentatifs (déjà en mm, comme passés par l'appelant) :
//  lowThr = 300 mm (au-dessus = réserve basse), okThr = 250 mm (hystérésis 5 cm).
static const uint16_t LOW = 300;  // > LOW  => basse
static const uint16_t OK  = 250;  // < OK   => OK
static const uint16_t LOW_DIST = 350;   // mesure « réserve basse »
static const uint16_t OK_DIST  = 200;   // mesure « réserve OK »
static const uint16_t DEAD     = 275;   // entre OK et LOW => zone morte

void setUp(void) {}
void tearDown(void) {}

// Une seule mesure basse ne verrouille pas (debounce = 2).
void test_first_low_no_lock(void) {
  State st;
  Decision d = evaluate(st, LOW_DIST, LOW, OK, /*currentlyLocked=*/false);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_EQUAL_UINT8(1, st.aboveCount);
}

// Deux mesures basses consécutives -> verrouillage.
void test_second_low_locks(void) {
  State st;
  evaluate(st, LOW_DIST, LOW, OK, false);            // above=1
  Decision d = evaluate(st, LOW_DIST, LOW, OK, false); // above=2 -> Lock
  TEST_ASSERT_EQUAL(Decision::Lock, d);
  TEST_ASSERT_EQUAL_UINT8(2, st.aboveCount);
}

// Pompe déjà verrouillée : pas de re-verrouillage (None) même si réserve basse.
void test_already_locked_no_relock(void) {
  State st;
  evaluate(st, LOW_DIST, LOW, OK, /*currentlyLocked=*/true);
  Decision d = evaluate(st, LOW_DIST, LOW, OK, /*currentlyLocked=*/true);
  TEST_ASSERT_EQUAL(Decision::None, d);
}

// Deux mesures OK ne suffisent pas à déverrouiller (debounce sortie = 3).
void test_two_ok_not_enough_to_unlock(void) {
  State st;
  evaluate(st, OK_DIST, LOW, OK, /*currentlyLocked=*/true);
  Decision d = evaluate(st, OK_DIST, LOW, OK, /*currentlyLocked=*/true);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_EQUAL_UINT8(2, st.belowCount);
}

// Trois mesures OK consécutives (pompe verrouillée) -> déverrouillage.
void test_three_ok_unlocks(void) {
  State st;
  evaluate(st, OK_DIST, LOW, OK, true);            // below=1
  evaluate(st, OK_DIST, LOW, OK, true);            // below=2
  Decision d = evaluate(st, OK_DIST, LOW, OK, true); // below=3 -> Unlock
  TEST_ASSERT_EQUAL(Decision::Unlock, d);
  TEST_ASSERT_EQUAL_UINT8(3, st.belowCount);
}

// Déverrouillage seulement si la pompe est verrouillée.
void test_unlock_only_if_locked(void) {
  State st;
  evaluate(st, OK_DIST, LOW, OK, false);
  evaluate(st, OK_DIST, LOW, OK, false);
  Decision d = evaluate(st, OK_DIST, LOW, OK, false); // below=3 mais non verrouillée
  TEST_ASSERT_EQUAL(Decision::None, d);
}

// Zone morte (entre OK et LOW) : aucune bascule, compteurs bornés à 2.
void test_deadband_no_change_clamps_counters(void) {
  State st;
  st.aboveCount = 3;
  st.belowCount = 3;
  Decision d = evaluate(st, DEAD, LOW, OK, /*currentlyLocked=*/true);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_EQUAL_UINT8(2, st.aboveCount);
  TEST_ASSERT_EQUAL_UINT8(2, st.belowCount);
}

// aboveCount plafonne à 3 (pas de débordement sur basse prolongée).
void test_above_count_clamped_at_3(void) {
  State st;
  for (int i = 0; i < 5; ++i) evaluate(st, LOW_DIST, LOW, OK, false);
  TEST_ASSERT_EQUAL_UINT8(3, st.aboveCount);
}

// Une mesure basse réinitialise le compteur de sortie (belowCount).
void test_low_resets_below_count(void) {
  State st;
  st.belowCount = 2;
  evaluate(st, LOW_DIST, LOW, OK, /*currentlyLocked=*/true);
  TEST_ASSERT_EQUAL_UINT8(0, st.belowCount);
  TEST_ASSERT_EQUAL_UINT8(1, st.aboveCount);
}

// Une mesure OK réinitialise le compteur d'entrée (aboveCount).
void test_ok_resets_above_count(void) {
  State st;
  st.aboveCount = 2;
  evaluate(st, OK_DIST, LOW, OK, /*currentlyLocked=*/true);
  TEST_ASSERT_EQUAL_UINT8(0, st.aboveCount);
  TEST_ASSERT_EQUAL_UINT8(1, st.belowCount);
}

// Cycle complet : verrouillage (réserve vide) puis déverrouillage (réserve remplie).
void test_full_lock_then_unlock_cycle(void) {
  State st;
  evaluate(st, LOW_DIST, LOW, OK, false);
  TEST_ASSERT_EQUAL(Decision::Lock, evaluate(st, LOW_DIST, LOW, OK, false));
  // L'appelant a verrouillé ; la réserve se remplit (distance diminue).
  evaluate(st, OK_DIST, LOW, OK, true);
  evaluate(st, OK_DIST, LOW, OK, true);
  TEST_ASSERT_EQUAL(Decision::Unlock, evaluate(st, OK_DIST, LOW, OK, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_first_low_no_lock);
  RUN_TEST(test_second_low_locks);
  RUN_TEST(test_already_locked_no_relock);
  RUN_TEST(test_two_ok_not_enough_to_unlock);
  RUN_TEST(test_three_ok_unlocks);
  RUN_TEST(test_unlock_only_if_locked);
  RUN_TEST(test_deadband_no_change_clamps_counters);
  RUN_TEST(test_above_count_clamped_at_3);
  RUN_TEST(test_low_resets_below_count);
  RUN_TEST(test_ok_resets_above_count);
  RUN_TEST(test_full_lock_then_unlock_cycle);
  return UNITY_END();
}
