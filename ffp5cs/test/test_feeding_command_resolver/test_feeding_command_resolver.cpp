#include <unity.h>

// Tests de FeedingCommandResolver (automatism/feeding_command_resolver.h).
// Cible le bug « petits + gros déclenchés en même temps » côté serveur distant :
// les deux fronts montants simultanés doivent produire UN cycle séquentiel, et un
// front non exécutable (cycle en cours) ne doit PAS être perdu.
#include "../../include/automatism/feeding_command_resolver.h"

using namespace FeedingCommandResolver;

void setUp(void) {}
void tearDown(void) {}

static Inputs base() {
  Inputs in;  // tout à false par défaut
  return in;
}

// --- Cas nominal : un seul canal monte -----------------------------------

void test_small_only_rising(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = true; in.prevSmall = false;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Small, d.action);
  TEST_ASSERT_TRUE(d.newPrevSmall);   // edge consommé
  TEST_ASSERT_FALSE(d.newPrevBig);
}

void test_big_only_rising(void) {
  Inputs in = base();
  in.bigPresent = true; in.bigLevel = true; in.prevBig = false;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d.action);
  TEST_ASSERT_TRUE(d.newPrevBig);
  TEST_ASSERT_FALSE(d.newPrevSmall);
}

// --- Cœur du correctif : les deux montent dans le même poll --------------

void test_both_rising_simultaneously(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = true; in.prevSmall = false;
  in.bigPresent = true;   in.bigLevel = true;   in.prevBig = false;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Both, d.action);   // un seul cycle séquentiel
  TEST_ASSERT_TRUE(d.newPrevSmall);            // les deux edges consommés
  TEST_ASSERT_TRUE(d.newPrevBig);
}

// --- Pas de front : états stables / absents -------------------------------

void test_no_keys_present_keeps_state(void) {
  Inputs in = base();
  in.prevSmall = true; in.prevBig = false;     // canaux absents du poll
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_TRUE(d.newPrevSmall);            // inchangé
  TEST_ASSERT_FALSE(d.newPrevBig);
}

void test_steady_high_no_retrigger(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = true; in.prevSmall = true;  // déjà 1
  in.bigPresent = true;   in.bigLevel = true;   in.prevBig = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_TRUE(d.newPrevSmall);
  TEST_ASSERT_TRUE(d.newPrevBig);
}

void test_falling_edge_resets_state(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = false; in.prevSmall = true;  // 1 -> 0
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_FALSE(d.newPrevSmall);           // réarmé pour un futur 0->1
}

// --- Anti-perte : cycle en cours -> commandes gardées en attente ----------

void test_in_progress_holds_both_pending(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = true; in.prevSmall = false;
  in.bigPresent = true;   in.bigLevel = true;   in.prevBig = false;
  in.feedingInProgress = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);   // rien maintenant
  TEST_ASSERT_FALSE(d.newPrevSmall);           // mais edges NON consommés (en attente)
  TEST_ASSERT_FALSE(d.newPrevBig);
}

void test_in_progress_then_executes_next_poll(void) {
  // 1er poll pendant un cycle : gardé en attente
  Inputs in = base();
  in.bigPresent = true; in.bigLevel = true; in.prevBig = false;
  in.feedingInProgress = true;
  Decision d1 = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d1.action);
  TEST_ASSERT_FALSE(d1.newPrevBig);

  // 2e poll, cycle terminé : le front est toujours là -> exécution
  in.feedingInProgress = false;
  in.prevBig = d1.newPrevBig;                   // report de l'état (toujours false)
  Decision d2 = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d2.action);
  TEST_ASSERT_TRUE(d2.newPrevBig);
}

void test_in_progress_tracks_low_channel(void) {
  // Canal présent à 0 pendant un cycle : suit le niveau (pas en attente)
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = false; in.prevSmall = true;
  in.feedingInProgress = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_FALSE(d.newPrevSmall);           // 1 -> 0 suivi normalement
}

// --- Priorité : un seul des deux monte, l'autre stable -------------------

void test_big_rising_small_steady(void) {
  Inputs in = base();
  in.smallPresent = true; in.smallLevel = true; in.prevSmall = true;   // stable
  in.bigPresent = true;   in.bigLevel = true;   in.prevBig = false;    // monte
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d.action);
  TEST_ASSERT_TRUE(d.newPrevBig);
  TEST_ASSERT_TRUE(d.newPrevSmall);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_small_only_rising);
  RUN_TEST(test_big_only_rising);
  RUN_TEST(test_both_rising_simultaneously);
  RUN_TEST(test_no_keys_present_keeps_state);
  RUN_TEST(test_steady_high_no_retrigger);
  RUN_TEST(test_falling_edge_resets_state);
  RUN_TEST(test_in_progress_holds_both_pending);
  RUN_TEST(test_in_progress_then_executes_next_poll);
  RUN_TEST(test_in_progress_tracks_low_channel);
  RUN_TEST(test_big_rising_small_steady);
  return UNITY_END();
}
