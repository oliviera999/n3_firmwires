#include <unity.h>

// Tests de RefillDuration (automatism/refill_duration.h), décision de timing extraite de
// Automatism::handleRefillMaxDurationStop (chantier C4). Sécurité : mauvais arbitrage =
// pompe qui tourne au-delà de la durée max (débordement) ou chrono jamais reset sur anomalie.
#include "../../include/automatism/refill_duration.h"

using namespace RefillDuration;

static const uint32_t MAX = 60000UL;          // durée max remplissage : 60 s
static const uint32_t ANOM = DEFAULT_ANOMALY_MS;  // 3 000 000 ms

void setUp(void) {}
void tearDown(void) {}

// Avant la durée max -> Continue.
void test_continue_before_max(void) {
  TEST_ASSERT_EQUAL(Decision::Continue, evaluate(0, MAX));
  TEST_ASSERT_EQUAL(Decision::Continue, evaluate(MAX - 1, MAX));
}

// Durée max atteinte (==) -> ForcedStop (comparaison stricte `< max`).
void test_forced_stop_at_max(void) {
  TEST_ASSERT_EQUAL(Decision::ForcedStop, evaluate(MAX, MAX));
  TEST_ASSERT_EQUAL(Decision::ForcedStop, evaluate(MAX + 1, MAX));
}

// Durée invraisemblable (> seuil anomalie) -> AnomalyReset.
void test_anomaly_reset_above_threshold(void) {
  TEST_ASSERT_EQUAL(Decision::AnomalyReset, evaluate(ANOM + 1, MAX));
  TEST_ASSERT_EQUAL(Decision::AnomalyReset, evaluate(5000000UL, MAX));
}

// Pile au seuil d'anomalie (==) -> pas d'anomalie (strict >) ; ici > max -> ForcedStop.
void test_at_anomaly_threshold_is_forced_stop(void) {
  TEST_ASSERT_EQUAL(Decision::ForcedStop, evaluate(ANOM, MAX));
}

// L'anomalie est prioritaire même si la durée max est dépassée.
void test_anomaly_takes_priority_over_forced_stop(void) {
  // elapsed (4 000 000) > max ET > anomalie -> AnomalyReset (pas ForcedStop).
  TEST_ASSERT_EQUAL(Decision::AnomalyReset, evaluate(4000000UL, MAX));
}

// Seuil d'anomalie paramétrable (injection pour test).
void test_custom_anomaly_threshold(void) {
  TEST_ASSERT_EQUAL(Decision::AnomalyReset, evaluate(101, MAX, /*anomalyThresholdMs=*/100));
  TEST_ASSERT_EQUAL(Decision::Continue, evaluate(50, MAX, /*anomalyThresholdMs=*/100));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_continue_before_max);
  RUN_TEST(test_forced_stop_at_max);
  RUN_TEST(test_anomaly_reset_above_threshold);
  RUN_TEST(test_at_anomaly_threshold_is_forced_stop);
  RUN_TEST(test_anomaly_takes_priority_over_forced_stop);
  RUN_TEST(test_custom_anomaly_threshold);
  return UNITY_END();
}
