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

int main(void) {
  UNITY_BEGIN();
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
  return UNITY_END();
}
