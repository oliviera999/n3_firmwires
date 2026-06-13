#include <unity.h>
#include <cstring>  // strlen

// Test natif (hôte) de la logique de fallback des niveaux d'eau.
//
// SensorReadingFallback (header pur, stdint/stdio) est utilisé :
//   - system_sensors.cpp     → resolveWaterLevel() pour wlPota/wlAqua/wlTank
//   - automatism_sync.cpp     → formatWaterLevelPost() pour body.eauPotager/eauAquarium
//
// formatWaterLevelPost() alimente le corps POST signé HMAC : la distinction
// « 0 → chaîne vide (champ omis) » vs « valeur → décimal » fait partie du contrat
// firmware↔serveur (cf. test_post_body). Une régression ici fausse les données
// postées ou casse la signature. D'où ces tests sur les cas limites.

#include "../../include/sensor_reading_fallback.h"

using namespace SensorReadingFallback;

void setUp(void) {}
void tearDown(void) {}

// ---- waterLevel : chaîne de priorité current > lastValid > fallback ----

void test_waterLevel_uses_current_when_present(void) {
  TEST_ASSERT_EQUAL_UINT16(100, waterLevel(100, 50, 10));
}

void test_waterLevel_falls_back_to_lastValid_when_current_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(50, waterLevel(0, 50, 10));
}

void test_waterLevel_falls_back_to_default_when_current_and_lastValid_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(10, waterLevel(0, 0, 10));
}

void test_waterLevel_returns_zero_when_all_zero(void) {
  TEST_ASSERT_EQUAL_UINT16(0, waterLevel(0, 0, 0));
}

// ---- resolveWaterLevel : ajoute le drapeau useFallback ----

void test_resolve_returns_current_even_if_fallback_disabled(void) {
  TEST_ASSERT_EQUAL_UINT16(100, resolveWaterLevel(100, 50, 10, false));
}

void test_resolve_returns_zero_when_current_zero_and_fallback_disabled(void) {
  // Cas « mesure absente » : pas de lastValid ni défaut injectés.
  TEST_ASSERT_EQUAL_UINT16(0, resolveWaterLevel(0, 50, 10, false));
}

void test_resolve_uses_lastValid_when_fallback_enabled(void) {
  TEST_ASSERT_EQUAL_UINT16(50, resolveWaterLevel(0, 50, 10, true));
}

void test_resolve_uses_default_when_fallback_enabled_and_no_lastValid(void) {
  TEST_ASSERT_EQUAL_UINT16(10, resolveWaterLevel(0, 0, 10, true));
}

// ---- formatWaterLevelPost : 0 → "" ; valeur → décimal ; gardes buffer ----

void test_format_zero_yields_empty_string(void) {
  char buf[8] = "XXXX";  // pré-rempli pour vérifier l'écrasement
  formatWaterLevelPost(buf, sizeof(buf), 0);
  TEST_ASSERT_EQUAL_STRING("", buf);
}

void test_format_value_yields_decimal(void) {
  char buf[8] = {0};
  formatWaterLevelPost(buf, sizeof(buf), 123);
  TEST_ASSERT_EQUAL_STRING("123", buf);
}

void test_format_max_uint16(void) {
  char buf[8] = {0};
  formatWaterLevelPost(buf, sizeof(buf), 65535);
  TEST_ASSERT_EQUAL_STRING("65535", buf);
}

void test_format_null_buffer_does_not_crash(void) {
  // Ne doit rien faire (garde !buf) — l'absence de crash valide le test.
  formatWaterLevelPost(nullptr, 8, 123);
  TEST_ASSERT_TRUE(true);
}

void test_format_zero_size_does_not_write(void) {
  char buf[4] = "ABC";  // doit rester inchangé
  formatWaterLevelPost(buf, 0, 123);
  TEST_ASSERT_EQUAL_STRING("ABC", buf);
}

void test_format_truncates_safely_in_small_buffer(void) {
  // bufSize=3 ne peut contenir "123" (+ '\0') : snprintf doit tronquer
  // en gardant la terminaison nulle (pas de débordement).
  char buf[3] = {0};
  formatWaterLevelPost(buf, sizeof(buf), 123);
  TEST_ASSERT_EQUAL_size_t(2, strlen(buf));   // 2 chars + '\0'
  TEST_ASSERT_EQUAL_STRING("12", buf);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_waterLevel_uses_current_when_present);
  RUN_TEST(test_waterLevel_falls_back_to_lastValid_when_current_zero);
  RUN_TEST(test_waterLevel_falls_back_to_default_when_current_and_lastValid_zero);
  RUN_TEST(test_waterLevel_returns_zero_when_all_zero);
  RUN_TEST(test_resolve_returns_current_even_if_fallback_disabled);
  RUN_TEST(test_resolve_returns_zero_when_current_zero_and_fallback_disabled);
  RUN_TEST(test_resolve_uses_lastValid_when_fallback_enabled);
  RUN_TEST(test_resolve_uses_default_when_fallback_enabled_and_no_lastValid);
  RUN_TEST(test_format_zero_yields_empty_string);
  RUN_TEST(test_format_value_yields_decimal);
  RUN_TEST(test_format_max_uint16);
  RUN_TEST(test_format_null_buffer_does_not_crash);
  RUN_TEST(test_format_zero_size_does_not_write);
  RUN_TEST(test_format_truncates_safely_in_small_buffer);
  return UNITY_END();
}
