#include <unity.h>

// Tests du cœur pur de n3_log (T5) : échelle des niveaux et libellés.
// L'échelle numérique DOIT rester alignée à la fois sur ffp5cs
// (LogConfig::LogLevel) et sur CORE_DEBUG_LEVEL / ARDUHAL_LOG_LEVEL_*
// d'arduino-esp32 (0=None … 5=Verbose) : c'est le contrat qui permet de
// piloter le niveau via le flag de build standard et de câbler ffp5cs
// sans changement observable. Les libellés sont ceux des logs historiques.
#include "n3_log_core.h"

void setUp(void) {}
void tearDown(void) {}

// Échelle 0..5 identique à LogConfig::LogLevel (ffp5cs) et CORE_DEBUG_LEVEL.
void test_level_scale_matches_core_debug_level(void) {
  TEST_ASSERT_EQUAL_INT(0, N3LOG_NONE);
  TEST_ASSERT_EQUAL_INT(1, N3LOG_ERROR);
  TEST_ASSERT_EQUAL_INT(2, N3LOG_WARN);
  TEST_ASSERT_EQUAL_INT(3, N3LOG_INFO);
  TEST_ASSERT_EQUAL_INT(4, N3LOG_DEBUG);
  TEST_ASSERT_EQUAL_INT(5, N3LOG_VERBOSE);
}

// Libellés identiques à ffp5cs logLevelStr (parité de logs au câblage).
void test_level_labels_match_ffp5cs(void) {
  TEST_ASSERT_EQUAL_STRING("ERROR", n3LogLevelStr(N3LOG_ERROR));
  TEST_ASSERT_EQUAL_STRING("WARN", n3LogLevelStr(N3LOG_WARN));
  TEST_ASSERT_EQUAL_STRING("INFO", n3LogLevelStr(N3LOG_INFO));
  TEST_ASSERT_EQUAL_STRING("DEBUG", n3LogLevelStr(N3LOG_DEBUG));
  TEST_ASSERT_EQUAL_STRING("VERB", n3LogLevelStr(N3LOG_VERBOSE));
  TEST_ASSERT_EQUAL_STRING("NONE", n3LogLevelStr(N3LOG_NONE));
}

// Valeur hors échelle -> repli "NONE" (comportement historique du switch).
void test_out_of_range_falls_back_to_none(void) {
  TEST_ASSERT_EQUAL_STRING("NONE", n3LogLevelStr(static_cast<N3LogLevel>(42)));
}

// La comparaison d'émission est `level <= N3_LOG_LEVEL` : un niveau INFO
// laisse passer ERROR/WARN/INFO et filtre DEBUG/VERBOSE.
void test_emission_threshold_semantics(void) {
  const int threshold = N3LOG_INFO;
  TEST_ASSERT_TRUE(N3LOG_ERROR <= threshold);
  TEST_ASSERT_TRUE(N3LOG_WARN <= threshold);
  TEST_ASSERT_TRUE(N3LOG_INFO <= threshold);
  TEST_ASSERT_FALSE(N3LOG_DEBUG <= threshold);
  TEST_ASSERT_FALSE(N3LOG_VERBOSE <= threshold);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_level_scale_matches_core_debug_level);
  RUN_TEST(test_level_labels_match_ffp5cs);
  RUN_TEST(test_out_of_range_falls_back_to_none);
  RUN_TEST(test_emission_threshold_semantics);
  return UNITY_END();
}
