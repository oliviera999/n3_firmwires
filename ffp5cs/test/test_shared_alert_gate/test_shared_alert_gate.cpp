#include <unity.h>

// Tests de SharedAlertGate (automatism/shared_alert_gate.h) — gate d'arbitrage
// Phase 3 : le serveur couvre les alertes partagées si GET config OK + POST frais.
#include "../../include/automatism/shared_alert_gate.h"

void setUp(void) {}
void tearDown(void) {}

// Serveur OK + POST frais -> couvert (l'ESP se tait sur les alertes partagées).
void test_covered_when_server_ok_and_post_fresh(void) {
  TEST_ASSERT_TRUE(SharedAlertGate::serverCovers(true, /*now=*/100000, /*lastPost=*/50000, /*max=*/90000));
}

// GET config KO -> failover, quelle que soit la fraîcheur du POST.
void test_failover_when_server_not_ok(void) {
  TEST_ASSERT_FALSE(SharedAlertGate::serverCovers(false, 100000, 99000, 90000));
}

// POST trop ancien -> failover (le serveur ne voit plus nos données).
void test_failover_when_post_stale(void) {
  TEST_ASSERT_FALSE(SharedAlertGate::serverCovers(true, /*now=*/200001, /*lastPost=*/110000, /*max=*/90000));
}

// Jamais posté depuis le boot (lastPost == 0) -> non couvert.
void test_failover_when_never_posted(void) {
  TEST_ASSERT_FALSE(SharedAlertGate::serverCovers(true, 100000, 0, 90000));
}

// Wrap millis() : now a bouclé après lastPost -> l'âge non signé reste correct.
void test_wrap_safe_age(void) {
  const unsigned long lastPost = 0xFFFFFFF0UL;  // juste avant le wrap
  const unsigned long now = 0x00000010UL;       // juste après (âge réel = 0x20)
  TEST_ASSERT_TRUE(SharedAlertGate::serverCovers(true, now, lastPost, 90000));
}

// Borne exacte : âge == maxAge -> encore couvert ; maxAge+1 -> failover.
void test_freshness_boundary(void) {
  TEST_ASSERT_TRUE(SharedAlertGate::serverCovers(true, 90000 + 1000, 1000, 90000));
  TEST_ASSERT_FALSE(SharedAlertGate::serverCovers(true, 90000 + 1001, 1000, 90000));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_covered_when_server_ok_and_post_fresh);
  RUN_TEST(test_failover_when_server_not_ok);
  RUN_TEST(test_failover_when_post_stale);
  RUN_TEST(test_failover_when_never_posted);
  RUN_TEST(test_wrap_safe_age);
  RUN_TEST(test_freshness_boundary);
  return UNITY_END();
}
