#include <unity.h>

// Tests de RefillStart (automatism/refill_start.h), décision de démarrage du remplissage
// extraite de Automatism::handleRefillAutomaticStart (chantier C4). Chemin de SÉCURITÉ :
// un mauvais gating = pompe qui démarre à sec (réserve basse non bloquée) ou aquarium
// jamais rempli (démarrage refusé à tort).
#include "../../include/automatism/refill_start.h"

using namespace RefillStart;

// Seuils représentatifs (mm) : aquarium bas si distance > 300 ; réserve basse si > 400.
static const uint16_t AQ_THR = 300;
static const uint16_t TANK_THR = 400;
static const uint8_t MAXR = 5;

// Raccourci : cas « tout permet le démarrage » sauf paramètre testé.
static Decision call(bool known, uint16_t aqua, uint16_t tank, bool locked,
                     uint8_t retries, bool manual, bool running) {
  return evaluate(known, aqua, tank, AQ_THR, TANK_THR, locked, retries, MAXR, manual, running);
}

void setUp(void) {}
void tearDown(void) {}

// Conditions réunies, réserve OK -> Start.
void test_start_when_all_ok(void) {
  TEST_ASSERT_EQUAL(Decision::Start, call(true, 350, 200, false, 0, false, false));
}

// Conditions réunies mais réserve basse (tank > seuil) -> Block.
void test_block_when_reserve_low(void) {
  TEST_ASSERT_EQUAL(Decision::BlockReserveLow, call(true, 350, 450, false, 0, false, false));
}

// Niveau aquarium inconnu -> None.
void test_none_when_level_unknown(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(false, 350, 200, false, 0, false, false));
}

// Aquarium pas assez bas (aqua <= seuil) -> None.
void test_none_when_aqua_not_low(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(true, 300, 200, false, 0, false, false));  // == seuil, strict >
  TEST_ASSERT_EQUAL(Decision::None, call(true, 250, 200, false, 0, false, false));
}

// Pompe verrouillée -> None.
void test_none_when_locked(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(true, 350, 200, true, 0, false, false));
}

// Trop d'essais consécutifs -> None.
void test_none_when_retries_exhausted(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(true, 350, 200, false, MAXR, false, false));
  TEST_ASSERT_EQUAL(Decision::Start, call(true, 350, 200, false, MAXR - 1, false, false));  // limite
}

// Mode manuel actif -> None (l'auto ne prend pas la main).
void test_none_when_manual_override(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(true, 350, 200, false, 0, true, false));
}

// Pompe déjà en marche -> None (pas de re-démarrage).
void test_none_when_pump_running(void) {
  TEST_ASSERT_EQUAL(Decision::None, call(true, 350, 200, false, 0, false, true));
}

// Bord : réserve exactement au seuil -> pas de blocage -> Start (comparaison stricte >).
void test_start_when_reserve_at_threshold(void) {
  TEST_ASSERT_EQUAL(Decision::Start, call(true, 350, 400, false, 0, false, false));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_start_when_all_ok);
  RUN_TEST(test_block_when_reserve_low);
  RUN_TEST(test_none_when_level_unknown);
  RUN_TEST(test_none_when_aqua_not_low);
  RUN_TEST(test_none_when_locked);
  RUN_TEST(test_none_when_retries_exhausted);
  RUN_TEST(test_none_when_manual_override);
  RUN_TEST(test_none_when_pump_running);
  RUN_TEST(test_start_when_reserve_at_threshold);
  return UNITY_END();
}
