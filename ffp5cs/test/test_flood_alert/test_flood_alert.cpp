#include <unity.h>

// Tests de FloodAlert (automatism/flood_alert.h), extrait de Automatism::handleAlerts
// (chantier C4 — découpe god-class). Machine anti-spam « aquarium trop plein » :
// un mauvais debounce/cooldown/hystérésis = spam d'emails ou alerte manquée.
#include "../../include/automatism/flood_alert.h"

using namespace FloodAlert;

// Jeu de paramètres représentatif.
//  limFlood = 80 mm (sous cette distance = trop plein), hystérésis sortie à 100 mm,
//  debounce 120 s, cooldown 600 s, reset stable 180 s.
static Params makeParams() {
  Params p;
  p.limFloodMm = 80;
  p.resetThresholdMm = 100;
  p.debounceSec = 120;
  p.cooldownSec = 600;
  p.resetStableSec = 180;
  return p;
}

void setUp(void) {}
void tearDown(void) {}

// Premier passage sous le seuil : enregistre l'instant d'entrée, pas encore d'alerte (debounce).
void test_enter_sets_timestamp_no_email_yet(void) {
  State st;
  Params p = makeParams();
  Decision d = evaluate(st, /*wlAquaMm=*/50, /*nowEpoch=*/1000, p, /*mailEnabled=*/true);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_EQUAL_UINT32(1000, st.floodEnterSinceEpoch);
  TEST_ASSERT_FALSE(st.inFlood);
}

// Debounce non atteint -> pas d'email.
void test_debounce_not_met(void) {
  State st;
  Params p = makeParams();
  evaluate(st, 50, 1000, p, true);                 // entrée à t=1000
  Decision d = evaluate(st, 50, 1100, p, true);    // +100 s < debounce 120 s
  TEST_ASSERT_EQUAL(Decision::None, d);
}

// Debounce atteint, premier email (lastFloodEmailEpoch == 0 -> cooldown OK).
void test_debounce_met_first_email(void) {
  State st;
  Params p = makeParams();
  evaluate(st, 50, 1000, p, true);                 // entrée à t=1000
  Decision d = evaluate(st, 50, 1120, p, true);    // +120 s == debounce
  TEST_ASSERT_EQUAL(Decision::SendFloodEmail, d);
}

// mailEnabled=false : aucune alerte proposée, mais l'horodatage d'entrée évolue.
void test_mail_disabled_no_email_but_timestamp(void) {
  State st;
  Params p = makeParams();
  Decision d = evaluate(st, 50, 1000, p, false);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_EQUAL_UINT32(1000, st.floodEnterSinceEpoch);
}

// Cooldown non atteint après un email récent -> pas de second email.
void test_cooldown_blocks_second_email(void) {
  State st;
  Params p = makeParams();
  evaluate(st, 50, 1000, p, true);
  Decision d1 = evaluate(st, 50, 1120, p, true);
  TEST_ASSERT_EQUAL(Decision::SendFloodEmail, d1);
  markEmailSent(st, 1120);                          // email envoyé à t=1120
  TEST_ASSERT_TRUE(st.inFlood);
  // Toujours sous le seuil : inFlood empêche tout nouvel email tant qu'on n'est pas sorti.
  Decision d2 = evaluate(st, 50, 1200, p, true);
  TEST_ASSERT_EQUAL(Decision::None, d2);
}

// Après sortie (inFlood=false), le cooldown s'applique au prochain email.
void test_cooldown_window_after_exit(void) {
  State st;
  Params p = makeParams();
  st.inFlood = false;
  st.lastFloodEmailEpoch = 1000;     // dernier email à t=1000
  // Entrée sous seuil à t=1300, debounce OK à t=1420 mais cooldown (600 s) pas écoulé.
  evaluate(st, 50, 1300, p, true);
  Decision d = evaluate(st, 50, 1420, p, true);  // 1420-1000=420 < 600
  TEST_ASSERT_EQUAL(Decision::None, d);
  // À t=1600 : 1600-1000=600 >= cooldown ET debounce (1600-1300=300>=120) -> email.
  Decision d2 = evaluate(st, 50, 1600, p, true);
  TEST_ASSERT_EQUAL(Decision::SendFloodEmail, d2);
}

// Sortie d'état trop-plein : niveau au-dessus de limFlood+hyst, stable assez longtemps.
void test_exit_flood_after_stable_above(void) {
  State st;
  Params p = makeParams();
  st.inFlood = true;
  // wlAqua=120 mm >= resetThreshold(100). Première fois -> arme aboveResetSinceEpoch.
  Decision d0 = evaluate(st, 120, 2000, p, true);
  TEST_ASSERT_EQUAL(Decision::None, d0);
  TEST_ASSERT_EQUAL_UINT32(2000, st.aboveResetSinceEpoch);
  TEST_ASSERT_TRUE(st.inFlood);
  // +180 s stable -> sortie.
  Decision d1 = evaluate(st, 120, 2180, p, true);
  TEST_ASSERT_EQUAL(Decision::ExitFlood, d1);
  TEST_ASSERT_FALSE(st.inFlood);
}

// Niveau entre limFlood et limFlood+hyst (zone morte d'hystérésis) : pas de sortie, reset du compteur.
void test_hysteresis_deadband_no_exit(void) {
  State st;
  Params p = makeParams();
  st.inFlood = true;
  st.aboveResetSinceEpoch = 1500;
  // wlAqua=90 : >= limFlood(80) mais < resetThreshold(100) -> zone morte.
  Decision d = evaluate(st, 90, 3000, p, true);
  TEST_ASSERT_EQUAL(Decision::None, d);
  TEST_ASSERT_TRUE(st.inFlood);                    // toujours en flood
  TEST_ASSERT_EQUAL_UINT32(0, st.aboveResetSinceEpoch);  // compteur de sortie réinitialisé
  TEST_ASSERT_EQUAL_UINT32(0, st.floodEnterSinceEpoch);  // pas sous le seuil
}

// Repasser sous le seuil réinitialise aboveResetSinceEpoch.
void test_dip_below_resets_above_counter(void) {
  State st;
  Params p = makeParams();
  st.inFlood = true;
  st.aboveResetSinceEpoch = 1500;
  evaluate(st, 50, 4000, p, true);  // sous le seuil
  TEST_ASSERT_EQUAL_UINT32(0, st.aboveResetSinceEpoch);
  TEST_ASSERT_EQUAL_UINT32(4000, st.floodEnterSinceEpoch);
}

// markEmailSent arme l'anti-spam.
void test_mark_email_sent(void) {
  State st;
  markEmailSent(st, 12345);
  TEST_ASSERT_TRUE(st.inFlood);
  TEST_ASSERT_EQUAL_UINT32(12345, st.lastFloodEmailEpoch);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_enter_sets_timestamp_no_email_yet);
  RUN_TEST(test_debounce_not_met);
  RUN_TEST(test_debounce_met_first_email);
  RUN_TEST(test_mail_disabled_no_email_but_timestamp);
  RUN_TEST(test_cooldown_blocks_second_email);
  RUN_TEST(test_cooldown_window_after_exit);
  RUN_TEST(test_exit_flood_after_stable_above);
  RUN_TEST(test_hysteresis_deadband_no_exit);
  RUN_TEST(test_dip_below_resets_above_counter);
  RUN_TEST(test_mark_email_sent);
  return UNITY_END();
}
