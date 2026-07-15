#include <unity.h>

// Tests de la décision pure du rollback OTA au premier boot (T4.4, opt-in).
// OtaRollback::decide arbitre entre valider l'image (auto-test OK), la rejeter
// (échec persistant après la fenêtre de grâce) ou attendre. Une erreur ici =
// soit une image saine condamnée (rollback intempestif), soit une image
// défaillante validée (perte du filet de sécurité).
#include "n3_ota_rollback.h"

using OtaRollback::Action;
using OtaRollback::decide;

static const uint32_t GRACE = 120000;  // 2 min de fenêtre d'auto-test

void setUp(void) {}
void tearDown(void) {}

// Image déjà validée (pas de PENDING_VERIFY) : toujours None, quoi qu'il arrive.
void test_not_pending_is_always_none(void) {
  TEST_ASSERT_TRUE(Action::None == decide(false, false, 0, GRACE));
  TEST_ASSERT_TRUE(Action::None == decide(false, true, 0, GRACE));
  TEST_ASSERT_TRUE(Action::None == decide(false, false, GRACE + 1, GRACE));
}

// Auto-test réussi -> validation immédiate, dès le début de la fenêtre.
void test_selftest_ok_marks_valid(void) {
  TEST_ASSERT_TRUE(Action::MarkValid == decide(true, true, 0, GRACE));
  TEST_ASSERT_TRUE(Action::MarkValid == decide(true, true, GRACE / 2, GRACE));
}

// Auto-test réussi APRÈS l'expiration de la fenêtre : on valide quand même
// (un WiFi lent ne doit pas condamner une image saine déjà connectée).
void test_selftest_ok_after_grace_still_valid(void) {
  TEST_ASSERT_TRUE(Action::MarkValid == decide(true, true, GRACE + 5000, GRACE));
}

// Échec pendant la fenêtre de grâce : on attend (None), on retentera.
void test_failure_within_grace_waits(void) {
  TEST_ASSERT_TRUE(Action::None == decide(true, false, 0, GRACE));
  TEST_ASSERT_TRUE(Action::None == decide(true, false, GRACE - 1, GRACE));
}

// Échec persistant à l'expiration de la fenêtre : rollback.
void test_failure_at_grace_rolls_back(void) {
  TEST_ASSERT_TRUE(Action::Rollback == decide(true, false, GRACE, GRACE));
  TEST_ASSERT_TRUE(Action::Rollback == decide(true, false, GRACE + 1, GRACE));
}

// Fenêtre nulle : verdict immédiat (OK -> valide, KO -> rollback).
void test_zero_grace_immediate_verdict(void) {
  TEST_ASSERT_TRUE(Action::MarkValid == decide(true, true, 0, 0));
  TEST_ASSERT_TRUE(Action::Rollback == decide(true, false, 0, 0));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_not_pending_is_always_none);
  RUN_TEST(test_selftest_ok_marks_valid);
  RUN_TEST(test_selftest_ok_after_grace_still_valid);
  RUN_TEST(test_failure_within_grace_waits);
  RUN_TEST(test_failure_at_grace_rolls_back);
  RUN_TEST(test_zero_grace_immediate_verdict);
  return UNITY_END();
}
