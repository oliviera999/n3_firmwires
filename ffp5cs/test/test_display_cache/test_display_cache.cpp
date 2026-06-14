#include <unity.h>
#include <cstring>
#include "../../include/display_cache.h"

// Test natif (hôte) de DisplayCache : détection de changement pour éviter les
// redraws OLED inutiles (optimisation de rendu). Header pur (Arduino mocké).
// Un bug ici fige l'affichage (changement non détecté) ou gaspille du CPU
// (toujours « changé »). On vérifie les 3 caches et leurs subtilités :
// drapeau force, gestion timeStr (null/strcmp/troncature), reset().

void setUp(void) {}
void tearDown(void) {}

// ---------------- StatusCache ----------------

void test_status_first_update_is_changed(void) {
  DisplayCache::StatusCache s;
  // Diffère des sentinelles par défaut (-2/-128/...) -> changé.
  TEST_ASSERT_TRUE(s.update(1, 1, -50, false, 1, 10));
}

void test_status_identical_update_not_changed(void) {
  DisplayCache::StatusCache s;
  s.update(1, 1, -50, false, 1, 10);
  TEST_ASSERT_FALSE(s.update(1, 1, -50, false, 1, 10));
}

void test_status_single_field_change_detected(void) {
  DisplayCache::StatusCache s;
  s.update(1, 1, -50, false, 1, 10);
  TEST_ASSERT_TRUE(s.update(1, 1, -50, true, 1, 10));   // mailBlink change
  TEST_ASSERT_TRUE(s.update(1, 1, -49, true, 1, 10));   // rssi change
  TEST_ASSERT_FALSE(s.update(1, 1, -49, true, 1, 10));  // stable de nouveau
}

void test_status_force_always_changed(void) {
  DisplayCache::StatusCache s;
  s.update(1, 1, -50, false, 1, 10);
  // Mêmes valeurs mais force=true -> changé.
  TEST_ASSERT_TRUE(s.update(1, 1, -50, false, 1, 10, true));
}

void test_status_reset_then_changed(void) {
  DisplayCache::StatusCache s;
  s.update(1, 1, -50, false, 1, 10);
  s.reset();
  // Après reset (sentinelles), la même valeur redevient « changée ».
  TEST_ASSERT_TRUE(s.update(1, 1, -50, false, 1, 10));
}

// ---------------- MainCache (timeStr) ----------------

void test_main_first_update_changed_then_stable(void) {
  DisplayCache::MainCache m;
  TEST_ASSERT_TRUE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00"));
  TEST_ASSERT_FALSE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00"));
}

void test_main_numeric_change_detected(void) {
  DisplayCache::MainCache m;
  m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00");
  TEST_ASSERT_TRUE(m.update(20.5f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00"));
}

void test_main_timestr_change_detected(void) {
  DisplayCache::MainCache m;
  m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00");
  TEST_ASSERT_TRUE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:01"));
}

void test_main_timestr_null_handling(void) {
  DisplayCache::MainCache m;
  m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00");
  // null après une valeur non vide -> changé (timeStr vidé).
  TEST_ASSERT_TRUE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, nullptr));
  // null de nouveau, reste numérique stable -> non changé.
  TEST_ASSERT_FALSE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, nullptr));
}

void test_main_reset_then_changed(void) {
  DisplayCache::MainCache m;
  m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00");
  m.reset();
  TEST_ASSERT_TRUE(m.update(20.0f, 21.0f, 50.0f, 100, 110, 120, 400, "12:00"));
}

// ---------------- VariablesCache ----------------

void test_variables_first_update_changed_then_stable(void) {
  DisplayCache::VariablesCache v;
  TEST_ASSERT_TRUE(v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2));
  TEST_ASSERT_FALSE(v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2));
}

void test_variables_single_field_change_detected(void) {
  DisplayCache::VariablesCache v;
  v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2);
  TEST_ASSERT_TRUE(v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.5f, 2));  // thHeat
}

void test_variables_force_always_changed(void) {
  DisplayCache::VariablesCache v;
  v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2);
  TEST_ASSERT_TRUE(v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2, true));
}

void test_variables_reset_then_changed(void) {
  DisplayCache::VariablesCache v;
  v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2);
  v.reset();
  TEST_ASSERT_TRUE(v.update(true, false, true, false, 8, 12, 19, 3, 5, 40, 60, 25.0f, 2));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_status_first_update_is_changed);
  RUN_TEST(test_status_identical_update_not_changed);
  RUN_TEST(test_status_single_field_change_detected);
  RUN_TEST(test_status_force_always_changed);
  RUN_TEST(test_status_reset_then_changed);
  RUN_TEST(test_main_first_update_changed_then_stable);
  RUN_TEST(test_main_numeric_change_detected);
  RUN_TEST(test_main_timestr_change_detected);
  RUN_TEST(test_main_timestr_null_handling);
  RUN_TEST(test_main_reset_then_changed);
  RUN_TEST(test_variables_first_update_changed_then_stable);
  RUN_TEST(test_variables_single_field_change_detected);
  RUN_TEST(test_variables_force_always_changed);
  RUN_TEST(test_variables_reset_then_changed);
  return UNITY_END();
}
