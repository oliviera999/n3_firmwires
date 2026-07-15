#include <unity.h>

// Tests du séquenceur PUR du cycle de réveil deep-sleep (T6.0, lib n3_app,
// n3_app_seq.h) : n3AppNextStep décide de l'enchaînement des étapes à partir du
// contexte N3AppContext. Aucune dépendance Arduino/ESP — logique déterministe.
// Assertions couvrant : ordre nominal complet, saut de OTA, sorties anticipées
// (restart / sleep immédiat), idempotence de DONE, non-bouclage à SLEEP/DONE.
#include "n3_app_seq.h"

// Contexte neutre : aucun flag armé (déroulé nominal).
static N3AppContext freshCtx(void) {
  N3AppContext ctx;
  ctx.wakeupCause = 0;
  ctx.wifiOk = false;
  ctx.clockValid = false;
  ctx.epochSeconds = 0;
  ctx.bootCount = 1;
  ctx.postOkThisWake = false;
  ctx.sleepSeconds = 0;
  ctx.requestRestart = false;
  ctx.requestSleepNow = false;
  ctx.user = nullptr;
  return ctx;
}

void setUp(void) {}
void tearDown(void) {}

// Ordre nominal complet WAKE→WIFI→CLOCK→REMOTE_CFG→OTA→SENSE→PAYLOAD→
// AUTOMATION→REPORTS→SLEEP→DONE quand otaDue=true et aucun flag.
void test_nominal_full_order_with_ota(void) {
  N3AppContext ctx = freshCtx();
  TEST_ASSERT_EQUAL_INT(N3_STEP_WIFI,       n3AppNextStep(N3_STEP_WAKE, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_CLOCK,      n3AppNextStep(N3_STEP_WIFI, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_REMOTE_CFG, n3AppNextStep(N3_STEP_CLOCK, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_OTA,        n3AppNextStep(N3_STEP_REMOTE_CFG, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_SENSE,      n3AppNextStep(N3_STEP_OTA, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_PAYLOAD,    n3AppNextStep(N3_STEP_SENSE, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_AUTOMATION, n3AppNextStep(N3_STEP_PAYLOAD, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_REPORTS,    n3AppNextStep(N3_STEP_AUTOMATION, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP,      n3AppNextStep(N3_STEP_REPORTS, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE,       n3AppNextStep(N3_STEP_SLEEP, ctx, true));
}

// Le cycle nominal complet parcouru par itération atteint exactement DONE, dans
// le bon ordre, sans jamais boucler.
void test_nominal_iterated_reaches_done(void) {
  N3AppContext ctx = freshCtx();
  const N3AppStep expected[] = {
    N3_STEP_WAKE, N3_STEP_WIFI, N3_STEP_CLOCK, N3_STEP_REMOTE_CFG, N3_STEP_OTA,
    N3_STEP_SENSE, N3_STEP_PAYLOAD, N3_STEP_AUTOMATION, N3_STEP_REPORTS,
    N3_STEP_SLEEP, N3_STEP_DONE
  };
  N3AppStep step = N3_STEP_WAKE;
  int i = 0;
  // Borne de sécurité : le cycle doit converger bien avant 50 itérations.
  for (; i < 50 && step != N3_STEP_DONE; ++i) {
    TEST_ASSERT_EQUAL_INT(expected[i], step);
    step = n3AppNextStep(step, ctx, true);
  }
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, step);
  TEST_ASSERT_EQUAL_INT(10, i);  // 10 transitions WAKE..SLEEP -> DONE
}

// Saut de OTA quand otaDue=false : REMOTE_CFG enchaîne directement sur SENSE.
void test_ota_skipped_when_not_due(void) {
  N3AppContext ctx = freshCtx();
  TEST_ASSERT_EQUAL_INT(N3_STEP_SENSE, n3AppNextStep(N3_STEP_REMOTE_CFG, ctx, false));
  // Le reste de l'ordre est inchangé.
  TEST_ASSERT_EQUAL_INT(N3_STEP_REMOTE_CFG, n3AppNextStep(N3_STEP_CLOCK, ctx, false));
  TEST_ASSERT_EQUAL_INT(N3_STEP_PAYLOAD, n3AppNextStep(N3_STEP_SENSE, ctx, false));
}

// requestRestart=true à une étape du milieu (REMOTE_CFG) -> prochaine = SLEEP.
void test_request_restart_shortcuts_to_sleep(void) {
  N3AppContext ctx = freshCtx();
  ctx.requestRestart = true;
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP, n3AppNextStep(N3_STEP_REMOTE_CFG, ctx, true));
  // Court-circuit effectif dès le début du cycle aussi.
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP, n3AppNextStep(N3_STEP_WAKE, ctx, true));
  // Le saut prime sur la logique OTA (otaDue indifférent).
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP, n3AppNextStep(N3_STEP_CLOCK, ctx, false));
}

// requestSleepNow=true au milieu (AUTOMATION) -> prochaine = SLEEP.
void test_request_sleep_now_shortcuts_to_sleep(void) {
  N3AppContext ctx = freshCtx();
  ctx.requestSleepNow = true;
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP, n3AppNextStep(N3_STEP_AUTOMATION, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_SLEEP, n3AppNextStep(N3_STEP_SENSE, ctx, true));
}

// Idempotence de DONE : DONE -> DONE, quels que soient les flags.
void test_done_is_idempotent(void) {
  N3AppContext ctx = freshCtx();
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, n3AppNextStep(N3_STEP_DONE, ctx, true));
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, n3AppNextStep(N3_STEP_DONE, ctx, false));
  ctx.requestRestart = true;
  ctx.requestSleepNow = true;
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, n3AppNextStep(N3_STEP_DONE, ctx, true));
}

// Flags ignorés une fois à SLEEP/DONE : pas de boucle infinie sur SLEEP.
void test_flags_ignored_at_or_after_sleep(void) {
  N3AppContext ctx = freshCtx();
  ctx.requestRestart = true;
  ctx.requestSleepNow = true;
  // À SLEEP, malgré les flags armés, on progresse vers DONE (pas de re-SLEEP).
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, n3AppNextStep(N3_STEP_SLEEP, ctx, true));
  // À DONE, on reste à DONE.
  TEST_ASSERT_EQUAL_INT(N3_STEP_DONE, n3AppNextStep(N3_STEP_DONE, ctx, true));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_nominal_full_order_with_ota);
  RUN_TEST(test_nominal_iterated_reaches_done);
  RUN_TEST(test_ota_skipped_when_not_due);
  RUN_TEST(test_request_restart_shortcuts_to_sleep);
  RUN_TEST(test_request_sleep_now_shortcuts_to_sleep);
  RUN_TEST(test_done_is_idempotent);
  RUN_TEST(test_flags_ignored_at_or_after_sleep);
  return UNITY_END();
}
