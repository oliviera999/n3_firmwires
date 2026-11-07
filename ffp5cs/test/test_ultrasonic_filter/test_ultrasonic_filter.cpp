// Profil de test (résout config.h via le stub FreeRTOS).
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS

#include <unity.h>
#include <cstdint>

// Tests du filtrage ultrason pur (ultrasonic_filter.h), extrait de
// sensor_ultrasonic.cpp pour testabilité (audit §3.8). Logique numérique
// bug-prone (médiane / détection bimodale / rejet d'outlier / acceptation de
// saut) jusque-là enfouie dans un .cpp couplé I2C/NVS.
#include "../../include/ultrasonic_filter.h"
#include "../../include/echo_diag.h"
#include <cstring>

using namespace UltrasonicFilter;

void setUp(void) {}
void tearDown(void) {}

// ---------------- sortU16 ----------------

void test_sort_ascending(void) {
  uint16_t a[5] = {5, 1, 3, 2, 4};
  sortU16(a, 5);
  for (uint8_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(a[i] <= a[i + 1]);
  TEST_ASSERT_EQUAL_UINT16(1, a[0]);
  TEST_ASSERT_EQUAL_UINT16(5, a[4]);
}

void test_sort_edge_sizes(void) {
  uint16_t one[1] = {7};
  sortU16(one, 1);  // n<2 : no-op
  TEST_ASSERT_EQUAL_UINT16(7, one[0]);
  sortU16(one, 0);  // n=0 : no-op, pas de crash
  TEST_ASSERT_TRUE(true);
}

// ---------------- pickTankDistanceFromBatch ----------------

void test_pick_empty_and_single(void) {
  uint16_t none[1] = {0};
  TEST_ASSERT_EQUAL_UINT16(0, pickTankDistanceFromBatch(none, 0));
  uint16_t one[1] = {123};
  TEST_ASSERT_EQUAL_UINT16(123, pickTankDistanceFromBatch(one, 1));
}

void test_pick_unimodal_median(void) {
  // Cluster serré (gaps < BIMODAL_GAP_MM=80) -> médiane = 102.
  uint16_t r[5] = {102, 100, 101, 104, 103};
  TEST_ASSERT_EQUAL_UINT16(102, pickTankDistanceFromBatch(r, 5));
}

void test_pick_rejects_outlier_within_cluster(void) {
  // 160 est à 58 mm de la médiane (>OUTLIER_SPREAD_MM=50) -> rejeté ;
  // gaps < 80 donc pas de bimodal. Médiane des retenus = 102.
  uint16_t r[5] = {100, 101, 102, 103, 160};
  TEST_ASSERT_EQUAL_UINT16(102, pickTankDistanceFromBatch(r, 5));
}

void test_pick_bimodal_selects_higher_cluster(void) {
  // Deux clusters séparés (gap=198 >= 80) -> choisit le cluster HAUT
  // (distance plus grande = moins d'eau = sécurité). Médiane = 301.
  uint16_t r[6] = {100, 101, 102, 300, 301, 302};
  TEST_ASSERT_EQUAL_UINT16(301, pickTankDistanceFromBatch(r, 6));
}

// ---------------- acceptTankJump (toutes les branches) ----------------

void test_jump_reference_zero_always_accepts(void) {
  TEST_ASSERT_TRUE(acceptTankJump(9999, 0, 1, 999, false));
}

void test_jump_drain_within_delta(void) {
  // drain, delta=300 <= DRAIN_MAX_DELTA_MM(400) -> true
  TEST_ASSERT_TRUE(acceptTankJump(500, 200, 1, 999, false));
}

void test_jump_drain_large_expectDrain_tight(void) {
  // drain, delta=600 >400 ; expectDrain + tight(<=40) + keptCount>=3 -> true
  TEST_ASSERT_TRUE(acceptTankJump(800, 200, 3, 20, true));
}

void test_jump_drain_large_strong_tight(void) {
  // drain, delta=600 >400 ; strong(keptCount>=4) + tight -> true (sans expectDrain)
  TEST_ASSERT_TRUE(acceptTankJump(800, 200, 4, 20, false));
}

void test_jump_drain_large_rejected(void) {
  // drain, delta=600 >400 ; ni expectDrain, ni strong(3<4), ni tight(100>40) -> false
  TEST_ASSERT_FALSE(acceptTankJump(800, 200, 3, 100, false));
}

void test_jump_refill_within_delta_strong_tight(void) {
  // refill (candidate<reference), delta=50 <= REFILL_MAX_DELTA_MM(100), strong+tight -> true
  TEST_ASSERT_TRUE(acceptTankJump(150, 200, 4, 20, false));
}

void test_jump_refill_large_delta_rejected(void) {
  // refill, delta=150 >100 -> false
  TEST_ASSERT_FALSE(acceptTankJump(50, 200, 4, 20, false));
}

void test_jump_refill_not_strong_rejected(void) {
  // refill, delta=50 mais keptCount=3 (<4 strong) -> false
  TEST_ASSERT_FALSE(acceptTankJump(150, 200, 3, 20, false));
}

// ---------------- reactiveMedianOrUnknown (v15.32) ----------------

// Aucun écho valide : 0 (inconnu), jamais une valeur antérieure.
void test_reactive_empty_batch_is_unknown(void) {
  uint16_t none[1] = {81};
  TEST_ASSERT_EQUAL_UINT16(0, reactiveMedianOrUnknown(none, 0));
}

void test_reactive_median_odd_and_even(void) {
  uint16_t five[5] = {120, 95, 101, 400, 99};
  TEST_ASSERT_EQUAL_UINT16(101, reactiveMedianOrUnknown(five, 5));
  uint16_t two[2] = {110, 90};
  TEST_ASSERT_EQUAL_UINT16(110, reactiveMedianOrUnknown(two, 2));  // index count/2 (parité historique)
  uint16_t one[1] = {87};
  TEST_ASSERT_EQUAL_UINT16(87, reactiveMedianOrUnknown(one, 1));
}

// ---------------- EchoDiag (v15.33) ----------------

void test_echo_classify_priorities(void) {
  TEST_ASSERT_EQUAL(EchoDiag::Status::Ok, EchoDiag::classify(1, 4, true));
  TEST_ASSERT_EQUAL(EchoDiag::Status::Disabled, EchoDiag::classify(0, 4, true));
  TEST_ASSERT_EQUAL(EchoDiag::Status::OutOfRange, EchoDiag::classify(0, 2, false));
  TEST_ASSERT_EQUAL(EchoDiag::Status::NoEcho, EchoDiag::classify(0, 0, false));
}

void test_echo_diag_format_contract(void) {
  char buf[32];  // = sizeof(FullUpdateValues::wlAquaDiag)
  EchoDiag::Diag d;
  TEST_ASSERT_EQUAL_INT(0, EchoDiag::format(buf, sizeof(buf), d));  // Unknown -> champ omis
  TEST_ASSERT_EQUAL_STRING("", buf);

  EchoDiag::begin(d);
  for (int i = 0; i < 5; ++i) EchoDiag::recordTimeout(d);
  d.status = EchoDiag::classify(0, d.rejected, false);
  EchoDiag::format(buf, sizeof(buf), d);
  TEST_ASSERT_EQUAL_STRING("no_echo,5,0,0", buf);

  EchoDiag::begin(d);
  EchoDiag::recordRejected(d, 12);
  EchoDiag::recordRejected(d, 15);
  EchoDiag::recordTimeout(d);
  d.status = EchoDiag::classify(0, d.rejected, false);
  EchoDiag::format(buf, sizeof(buf), d);
  TEST_ASSERT_EQUAL_STRING("out_of_range,1,2,15", buf);

  // Pire cas : le plus long statut + compteurs saturés tient dans le champ POST (32 o).
  d.status = EchoDiag::Status::OutOfRange;
  d.timeouts = 255;
  d.rejected = 255;
  d.lastRejectedMm = 65535;
  TEST_ASSERT_TRUE(EchoDiag::format(buf, sizeof(buf), d) > 0);
  TEST_ASSERT_TRUE(strlen(buf) < sizeof(buf));
}

void test_echo_counters_saturate(void) {
  EchoDiag::Diag d;
  d.timeouts = 255;
  EchoDiag::recordTimeout(d);
  TEST_ASSERT_EQUAL_UINT8(255, d.timeouts);
}

// v15.35 : borne de blocage — 2 timeouts consécutifs abandonnent la série, un écho remet à zéro.
void test_abort_series_after_consecutive_timeouts(void) {
  TEST_ASSERT_FALSE(UltrasonicFilter::shouldAbortSeries(0));
  TEST_ASSERT_FALSE(UltrasonicFilter::shouldAbortSeries(1));
  TEST_ASSERT_TRUE(UltrasonicFilter::shouldAbortSeries(2));
  TEST_ASSERT_TRUE(UltrasonicFilter::shouldAbortSeries(5));
  TEST_ASSERT_FALSE(UltrasonicFilter::shouldAbortSeries(7, 0));  // 0 = désactivé
  TEST_ASSERT_TRUE(UltrasonicFilter::shouldAbortSeries(3, 3));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_abort_series_after_consecutive_timeouts);
  RUN_TEST(test_sort_ascending);
  RUN_TEST(test_sort_edge_sizes);
  RUN_TEST(test_pick_empty_and_single);
  RUN_TEST(test_pick_unimodal_median);
  RUN_TEST(test_pick_rejects_outlier_within_cluster);
  RUN_TEST(test_pick_bimodal_selects_higher_cluster);
  RUN_TEST(test_jump_reference_zero_always_accepts);
  RUN_TEST(test_jump_drain_within_delta);
  RUN_TEST(test_jump_drain_large_expectDrain_tight);
  RUN_TEST(test_jump_drain_large_strong_tight);
  RUN_TEST(test_jump_drain_large_rejected);
  RUN_TEST(test_jump_refill_within_delta_strong_tight);
  RUN_TEST(test_jump_refill_large_delta_rejected);
  RUN_TEST(test_jump_refill_not_strong_rejected);
  RUN_TEST(test_reactive_empty_batch_is_unknown);
  RUN_TEST(test_reactive_median_odd_and_even);
  RUN_TEST(test_echo_classify_priorities);
  RUN_TEST(test_echo_diag_format_contract);
  RUN_TEST(test_echo_counters_saturate);
  return UNITY_END();
}
