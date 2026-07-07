// Tests Unity natifs pour n3_notify — taxonomie de notification partagee
// (severite P1..P4, mode de verbosite none/important/partial/full, parsing de
// la config distante retro-compatible "checked"/"unchecked", format de sujet).
//
//   pio test -c platformio-native.ini -e native -f test_notify
//
// Header-only et sans dependance Arduino : on l'inclut directement.

#include <unity.h>
#include <string.h>
#include "n3_notify.h"

void setUp() {}
void tearDown() {}

// ---------- severite ----------

void test_severity_codes() {
  TEST_ASSERT_EQUAL_STRING("P1", n3SeverityCode(N3Severity::Critical));
  TEST_ASSERT_EQUAL_STRING("P2", n3SeverityCode(N3Severity::Alert));
  TEST_ASSERT_EQUAL_STRING("P3", n3SeverityCode(N3Severity::Info));
  TEST_ASSERT_EQUAL_STRING("P4", n3SeverityCode(N3Severity::Diagnostic));
}

void test_severity_priority_ordered() {
  TEST_ASSERT_EQUAL_INT(1, n3SeverityPriority(N3Severity::Critical));
  TEST_ASSERT_EQUAL_INT(4, n3SeverityPriority(N3Severity::Diagnostic));
}

// ---------- mode x severite ----------

void test_mode_none_blocks_everything() {
  TEST_ASSERT_FALSE(n3NotifModeAllows(N3NotifMode::None, N3Severity::Critical));
  TEST_ASSERT_FALSE(n3NotifModeAllows(N3NotifMode::None, N3Severity::Diagnostic));
}

void test_mode_important_allows_p1_p2_only() {
  TEST_ASSERT_TRUE(n3NotifModeAllows(N3NotifMode::Important, N3Severity::Critical));
  TEST_ASSERT_TRUE(n3NotifModeAllows(N3NotifMode::Important, N3Severity::Alert));
  TEST_ASSERT_FALSE(n3NotifModeAllows(N3NotifMode::Important, N3Severity::Info));
  TEST_ASSERT_FALSE(n3NotifModeAllows(N3NotifMode::Important, N3Severity::Diagnostic));
}

void test_mode_partial_adds_info() {
  TEST_ASSERT_TRUE(n3NotifModeAllows(N3NotifMode::Partial, N3Severity::Info));
  TEST_ASSERT_FALSE(n3NotifModeAllows(N3NotifMode::Partial, N3Severity::Diagnostic));
}

void test_mode_full_allows_everything() {
  TEST_ASSERT_TRUE(n3NotifModeAllows(N3NotifMode::Full, N3Severity::Diagnostic));
}

// ---------- parsing config distante ----------

void test_from_string_legacy_compat() {
  TEST_ASSERT_TRUE(n3NotifModeFromString("checked") == N3NotifMode::Full);
  TEST_ASSERT_TRUE(n3NotifModeFromString("unchecked") == N3NotifMode::None);
}

void test_from_string_new_values_case_and_trim() {
  TEST_ASSERT_TRUE(n3NotifModeFromString(" IMPORTANT ") == N3NotifMode::Important);
  TEST_ASSERT_TRUE(n3NotifModeFromString("Partial") == N3NotifMode::Partial);
  TEST_ASSERT_TRUE(n3NotifModeFromString("full") == N3NotifMode::Full);
  TEST_ASSERT_TRUE(n3NotifModeFromString("none") == N3NotifMode::None);
}

void test_from_string_empty_and_unknown() {
  TEST_ASSERT_TRUE(n3NotifModeFromString("") == N3NotifMode::None);
  TEST_ASSERT_TRUE(n3NotifModeFromString(nullptr, N3NotifMode::Important) == N3NotifMode::Important);
  TEST_ASSERT_TRUE(n3NotifModeFromString("bogus", N3NotifMode::Important) == N3NotifMode::Important);
}

// ---------- format de sujet ----------

void test_subject_with_family() {
  char b[96];
  n3MailFormatSubject(b, sizeof(b), "N3PP", N3Severity::Critical, "Batterie faible");
  TEST_ASSERT_EQUAL_STRING("[N3PP][P1] Batterie faible", b);
}

void test_subject_without_family() {
  char b[96];
  n3MailFormatSubject(b, sizeof(b), "", N3Severity::Info, "x");
  TEST_ASSERT_EQUAL_STRING("[P3] x", b);
}

void test_subject_guard_null_and_zero() {
  char b[16];
  TEST_ASSERT_EQUAL_STRING("", n3MailFormatSubject(nullptr, sizeof(b), "N3PP", N3Severity::Info, "x"));
  TEST_ASSERT_EQUAL_STRING("", n3MailFormatSubject(b, 0, "N3PP", N3Severity::Info, "x"));
}

// Phase 3 arbitrage : le plafond failover ramene Partial/Full a Important (P1/P2)
// et preserve None (kill-switch) et Important.
void test_failover_cap() {
  TEST_ASSERT_EQUAL_INT((int)N3NotifMode::None, (int)n3NotifModeCapFailover(N3NotifMode::None));
  TEST_ASSERT_EQUAL_INT((int)N3NotifMode::Important, (int)n3NotifModeCapFailover(N3NotifMode::Important));
  TEST_ASSERT_EQUAL_INT((int)N3NotifMode::Important, (int)n3NotifModeCapFailover(N3NotifMode::Partial));
  TEST_ASSERT_EQUAL_INT((int)N3NotifMode::Important, (int)n3NotifModeCapFailover(N3NotifMode::Full));
  // Combine au filtrage : en failover Full, P3/P4 bloques, P1/P2 passent.
  TEST_ASSERT_TRUE(n3NotifModeAllows(n3NotifModeCapFailover(N3NotifMode::Full), N3Severity::Critical));
  TEST_ASSERT_TRUE(n3NotifModeAllows(n3NotifModeCapFailover(N3NotifMode::Full), N3Severity::Alert));
  TEST_ASSERT_FALSE(n3NotifModeAllows(n3NotifModeCapFailover(N3NotifMode::Full), N3Severity::Info));
  TEST_ASSERT_FALSE(n3NotifModeAllows(n3NotifModeCapFailover(N3NotifMode::Full), N3Severity::Diagnostic));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_failover_cap);
  RUN_TEST(test_severity_codes);
  RUN_TEST(test_severity_priority_ordered);
  RUN_TEST(test_mode_none_blocks_everything);
  RUN_TEST(test_mode_important_allows_p1_p2_only);
  RUN_TEST(test_mode_partial_adds_info);
  RUN_TEST(test_mode_full_allows_everything);
  RUN_TEST(test_from_string_legacy_compat);
  RUN_TEST(test_from_string_new_values_case_and_trim);
  RUN_TEST(test_from_string_empty_and_unknown);
  RUN_TEST(test_subject_with_family);
  RUN_TEST(test_subject_without_family);
  RUN_TEST(test_subject_guard_null_and_zero);
  return UNITY_END();
}
