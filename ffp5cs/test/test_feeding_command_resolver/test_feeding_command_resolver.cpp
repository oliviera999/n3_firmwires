#include <unity.h>

// Tests de FeedingCommandResolver (automatism/feeding_command_resolver.h).
// v15.0 : protocole COMPTEUR MONOTONE. 108/109 = total des commandes émises par le
// serveur (entier croissant, jamais remis à zéro). Le firmware compare au compteur
// exécuté persisté et distribue au plus 1 repas/poll/canal, avec un cap de sécurité
// MAX_FEED_CATCHUP = 5 (anti-surdosage après une longue coupure).
#include "../../include/automatism/feeding_command_resolver.h"

using namespace FeedingCommandResolver;

void setUp(void) {}
void tearDown(void) {}

static Inputs base() {
  Inputs in;  // tout à 0/false par défaut
  return in;
}

// --- Amorçage one-shot au 1er poll (flash neuf) : ADOPTER sans distribuer ------

void test_seed_on_first_sight_no_feed(void) {
  Inputs in = base();
  in.seeded = false;
  in.smallPresent = true; in.smallCounter = 7;   // compteur serveur déjà élevé
  in.bigPresent = true;   in.bigCounter = 3;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);     // RIEN distribué
  TEST_ASSERT_TRUE(d.newSeeded);
  TEST_ASSERT_TRUE(d.persist);
  TEST_ASSERT_EQUAL_UINT32(7, d.newExecSmall);   // adopté = compteur serveur
  TEST_ASSERT_EQUAL_UINT32(3, d.newExecBig);
}

void test_seed_only_present_channels(void) {
  // Seul "big" présent au 1er poll : on n'adopte/seed que ce qui est vu.
  Inputs in = base();
  in.seeded = false;
  in.bigPresent = true; in.bigCounter = 42;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_TRUE(d.newSeeded);
  TEST_ASSERT_EQUAL_UINT32(42, d.newExecBig);
  TEST_ASSERT_EQUAL_UINT32(0, d.newExecSmall);   // small absent, inchangé
}

// --- Distribution nominale : un incrément => un repas, un canal -------------

void test_small_single_increment(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 5; in.execSmall = 4;  // pending = 1
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Small, d.action);
  TEST_ASSERT_EQUAL_UINT32(5, d.newExecSmall);   // exec += 1
  TEST_ASSERT_TRUE(d.persist);
}

void test_big_single_increment(void) {
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 10; in.execBig = 9;       // pending = 1
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d.action);
  TEST_ASSERT_EQUAL_UINT32(10, d.newExecBig);
}

void test_no_pending_no_action(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 5; in.execSmall = 5;  // pending = 0
  in.bigPresent = true;   in.bigCounter = 9;   in.execBig = 9;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_FALSE(d.persist);
}

// --- Un seul repas par poll même si pending=2 -------------------------------

void test_one_meal_per_poll(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 6; in.execSmall = 4;  // pending = 2
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Small, d.action);
  TEST_ASSERT_EQUAL_UINT32(5, d.newExecSmall);   // +1 seulement, reste 1 en attente
}

// --- Cap de sécurité MAX_FEED_CATCHUP = 5 -----------------------------------

void test_catchup_cap_big_jump(void) {
  // Serveur saute de +50 (longue coupure) : exec saute à server-5, puis +1 ce poll.
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 50; in.execBig = 0;       // pending = 50
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d.action);
  // cap: exec -> 50-5 = 45, puis distribution +1 -> 46. Reste pending = 4.
  TEST_ASSERT_EQUAL_UINT32(46, d.newExecBig);
  TEST_ASSERT_TRUE(d.persist);
}

void test_catchup_cap_total_dispensed_is_five(void) {
  // Simule la séquence de polls après un saut +50 : au plus 5 repas distribués.
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 50; in.execBig = 0;
  uint32_t exec = 0;
  int dispensed = 0;
  for (int poll = 0; poll < 20; ++poll) {
    in.execBig = exec;
    Decision d = resolve(in);
    if (d.action == Action::Big) dispensed++;
    exec = d.newExecBig;
  }
  TEST_ASSERT_EQUAL_INT(5, dispensed);            // jamais plus de 5 repas
  TEST_ASSERT_EQUAL_UINT32(50, exec);             // rattrapé jusqu'au compteur serveur
}

void test_catchup_exactly_five_no_cap(void) {
  // pending == 5 : pas de saut (cap = strictement > 5), distribue 1.
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 5; in.execBig = 0;        // pending = 5
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d.action);
  TEST_ASSERT_EQUAL_UINT32(1, d.newExecBig);      // pas de saut, juste +1
}

// --- Cycle en cours : ne pas avancer exec, re-tenter plus tard --------------

void test_feeding_in_progress_defers(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 5; in.execSmall = 4;  // pending = 1
  in.feedingInProgress = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_EQUAL_UINT32(4, d.newExecSmall);    // exec NON avancé
}

void test_feeding_in_progress_then_executes(void) {
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 8; in.execBig = 7;
  in.feedingInProgress = true;
  Decision d1 = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d1.action);
  TEST_ASSERT_EQUAL_UINT32(7, d1.newExecBig);

  // cycle terminé : le pending est toujours là -> exécution
  in.feedingInProgress = false;
  in.execBig = d1.newExecBig;
  Decision d2 = resolve(in);
  TEST_ASSERT_EQUAL(Action::Big, d2.action);
  TEST_ASSERT_EQUAL_UINT32(8, d2.newExecBig);
}

void test_in_progress_still_caps(void) {
  // Même cycle en cours, le cap de sécurité s'applique (persist du resync), mais
  // aucune distribution ce poll.
  Inputs in = base();
  in.seeded = true;
  in.bigPresent = true; in.bigCounter = 100; in.execBig = 0;
  in.feedingInProgress = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_EQUAL_UINT32(95, d.newExecBig);     // cap appliqué (100-5), pas de +1
  TEST_ASSERT_TRUE(d.persist);
}

// --- Les DEUX canaux en attente => Both, les deux exec +1 -------------------

void test_both_pending(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 3; in.execSmall = 2;  // pending = 1
  in.bigPresent = true;   in.bigCounter = 9;   in.execBig = 8;    // pending = 1
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::Both, d.action);
  TEST_ASSERT_EQUAL_UINT32(3, d.newExecSmall);
  TEST_ASSERT_EQUAL_UINT32(9, d.newExecBig);
}

// --- Recul du compteur (reset BDD) : resync sans distribuer -----------------

void test_counter_backwards_resync(void) {
  Inputs in = base();
  in.seeded = true;
  in.smallPresent = true; in.smallCounter = 2; in.execSmall = 10; // recul
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);      // aucun repas
  TEST_ASSERT_EQUAL_UINT32(2, d.newExecSmall);    // resync au compteur serveur
  TEST_ASSERT_TRUE(d.persist);
}

// --- Aucun canal présent : rien, pas de persist -----------------------------

void test_no_channel_present(void) {
  Inputs in = base();
  in.seeded = true;
  Decision d = resolve(in);
  TEST_ASSERT_EQUAL(Action::None, d.action);
  TEST_ASSERT_FALSE(d.persist);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_seed_on_first_sight_no_feed);
  RUN_TEST(test_seed_only_present_channels);
  RUN_TEST(test_small_single_increment);
  RUN_TEST(test_big_single_increment);
  RUN_TEST(test_no_pending_no_action);
  RUN_TEST(test_one_meal_per_poll);
  RUN_TEST(test_catchup_cap_big_jump);
  RUN_TEST(test_catchup_cap_total_dispensed_is_five);
  RUN_TEST(test_catchup_exactly_five_no_cap);
  RUN_TEST(test_feeding_in_progress_defers);
  RUN_TEST(test_feeding_in_progress_then_executes);
  RUN_TEST(test_in_progress_still_caps);
  RUN_TEST(test_both_pending);
  RUN_TEST(test_counter_backwards_resync);
  RUN_TEST(test_no_channel_present);
  return UNITY_END();
}
