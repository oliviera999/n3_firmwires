#include <unity.h>

// Tests de LoginThrottle (login_throttle.h) — anti-brute-force du login web local
// (audit sécurité 2026-06). Sans throttle, POST /api/login (web_server.cpp) accepte
// un nombre illimité de tentatives -> brute-force / dictionnaire possible sur le LAN.
//
// La logique est PURE et FAIL-SAFE : le temps est injecté (nowMs), aucune dépendance
// matériel. On valide : pas de lockout sous le seuil, lockout après N échecs
// rapprochés, expiration du lockout, reset sur succès, fenêtre glissante.
#include "n3_login_throttle.h"

// Politique de test à petits nombres pour des bornes faciles à asserter :
//   3 échecs dans une fenêtre de 1000 ms -> verrou de 5000 ms.
struct TestPolicy {
  static constexpr uint8_t  kMaxFailures = 3;
  static constexpr uint32_t kWindowMs    = 1000UL;
  static constexpr uint32_t kLockoutMs   = 5000UL;
};

using Throttle = LoginThrottle::Throttle<TestPolicy>;

void setUp(void) {}
void tearDown(void) {}

// Sous le seuil : 2 échecs (< 3) ne verrouillent jamais -> un utilisateur légitime
// qui se trompe une ou deux fois n'est PAS bloqué (fail-safe).
void test_no_lockout_below_threshold(void) {
  Throttle t;
  TEST_ASSERT_FALSE(t.isLockedOut(0));
  t.registerFailure(0);
  TEST_ASSERT_FALSE(t.isLockedOut(0));
  t.registerFailure(100);
  TEST_ASSERT_FALSE(t.isLockedOut(100));
  TEST_ASSERT_EQUAL_UINT8(2, t.failureCount());
}

// Seuil atteint : le 3e échec rapproché (dans la fenêtre) arme le verrou.
void test_lockout_after_threshold(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(100);
  TEST_ASSERT_FALSE(t.isLockedOut(100));
  t.registerFailure(200);  // 3e échec dans la fenêtre -> verrou
  TEST_ASSERT_TRUE(t.isLockedOut(200));
  TEST_ASSERT_EQUAL_UINT8(3, t.failureCount());
}

// Le verrou reste actif pendant toute la durée kLockoutMs (verrou armé à t=200,
// donc actif jusqu'à 200 + 5000 = 5200 exclus).
void test_lockout_active_during_window(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(100);
  t.registerFailure(200);  // verrou armé -> fin à 5200
  TEST_ASSERT_TRUE(t.isLockedOut(200));
  TEST_ASSERT_TRUE(t.isLockedOut(3000));
  TEST_ASSERT_TRUE(t.isLockedOut(5199));  // juste avant la fin
}

// Expiration : passé kLockoutMs, l'accès est de nouveau autorisé.
void test_lockout_expires_after_duration(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(100);
  t.registerFailure(200);  // verrou jusqu'à 5200
  TEST_ASSERT_TRUE(t.isLockedOut(5199));
  TEST_ASSERT_FALSE(t.isLockedOut(5200));  // borne exacte de fin -> déverrouillé
  TEST_ASSERT_FALSE(t.isLockedOut(6000));
}

// remainingLockoutMs : informatif (Retry-After). 0 si non verrouillé.
void test_remaining_lockout_ms(void) {
  Throttle t;
  TEST_ASSERT_EQUAL_UINT32(0, t.remainingLockoutMs(0));
  t.registerFailure(0);
  t.registerFailure(100);
  t.registerFailure(200);  // verrou jusqu'à 5200
  TEST_ASSERT_EQUAL_UINT32(5000, t.remainingLockoutMs(200));
  TEST_ASSERT_EQUAL_UINT32(1000, t.remainingLockoutMs(4200));
  TEST_ASSERT_EQUAL_UINT32(0, t.remainingLockoutMs(5200));
}

// Reset sur succès : après quelques échecs (sous le seuil), un succès remet le
// compteur à zéro -> il faut de nouveau N échecs pour reverrouiller.
void test_success_resets_counter(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(100);
  TEST_ASSERT_EQUAL_UINT8(2, t.failureCount());
  t.registerSuccess();
  TEST_ASSERT_EQUAL_UINT8(0, t.failureCount());
  TEST_ASSERT_FALSE(t.isLockedOut(100));
  // Deux nouveaux échecs ne suffisent toujours pas (compteur reparti de 0).
  t.registerFailure(200);
  t.registerFailure(300);
  TEST_ASSERT_FALSE(t.isLockedOut(300));
}

// Reset sur succès même APRÈS verrouillage : un login réussi (ex. fenêtre de
// lockout expirée puis bons identifiants) lève le verrou.
void test_success_clears_active_lockout(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(100);
  t.registerFailure(200);  // verrou armé
  TEST_ASSERT_TRUE(t.isLockedOut(200));
  t.registerSuccess();
  TEST_ASSERT_FALSE(t.isLockedOut(200));
  TEST_ASSERT_EQUAL_UINT8(0, t.failureCount());
}

// Fenêtre glissante : des échecs ESPACÉS de plus de kWindowMs ne s'accumulent pas.
// Ici chaque échec est à +1500 ms (> 1000 ms) -> la série redémarre à 1 à chaque
// fois, jamais de verrou (protège l'utilisateur légitime qui réessaie lentement).
void test_sliding_window_spaced_failures_no_lockout(void) {
  Throttle t;
  t.registerFailure(0);
  TEST_ASSERT_EQUAL_UINT8(1, t.failureCount());
  t.registerFailure(1500);  // > 1000 ms après -> série expirée, repart à 1
  TEST_ASSERT_EQUAL_UINT8(1, t.failureCount());
  t.registerFailure(3000);  // idem
  TEST_ASSERT_EQUAL_UINT8(1, t.failureCount());
  t.registerFailure(4500);
  TEST_ASSERT_EQUAL_UINT8(1, t.failureCount());
  TEST_ASSERT_FALSE(t.isLockedOut(4500));
}

// Fenêtre glissante (bord) : deux échecs à exactement kWindowMs d'écart restent
// dans la même série (comparaison stricte > kWindowMs pour casser la série).
void test_sliding_window_boundary_counts(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(1000);  // delta == kWindowMs -> PAS expiré (pas > 1000)
  TEST_ASSERT_EQUAL_UINT8(2, t.failureCount());
  t.registerFailure(2000);  // delta == 1000 de nouveau -> 3e dans la série -> verrou
  TEST_ASSERT_TRUE(t.isLockedOut(2000));
}

// Mélange réaliste : 2 échecs rapprochés, longue pause (série cassée), puis il faut
// de nouveau 3 échecs rapprochés pour verrouiller.
void test_failures_then_pause_then_lockout(void) {
  Throttle t;
  t.registerFailure(0);
  t.registerFailure(200);
  TEST_ASSERT_FALSE(t.isLockedOut(200));
  // Longue pause (> fenêtre) : la prochaine tentative repart d'une série neuve.
  t.registerFailure(5000);  // série repart à 1
  TEST_ASSERT_EQUAL_UINT8(1, t.failureCount());
  t.registerFailure(5100);  // 2
  TEST_ASSERT_FALSE(t.isLockedOut(5100));
  t.registerFailure(5200);  // 3 -> verrou
  TEST_ASSERT_TRUE(t.isLockedOut(5200));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_lockout_below_threshold);
  RUN_TEST(test_lockout_after_threshold);
  RUN_TEST(test_lockout_active_during_window);
  RUN_TEST(test_lockout_expires_after_duration);
  RUN_TEST(test_remaining_lockout_ms);
  RUN_TEST(test_success_resets_counter);
  RUN_TEST(test_success_clears_active_lockout);
  RUN_TEST(test_sliding_window_spaced_failures_no_lockout);
  RUN_TEST(test_sliding_window_boundary_counts);
  RUN_TEST(test_failures_then_pause_then_lockout);
  return UNITY_END();
}
