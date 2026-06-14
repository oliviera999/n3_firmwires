#include <unity.h>
#include <cstring>

// Tests de SdQueuePath (sd_queue_path.h), extrait de sd_logger.cpp (audit §3.8) :
// nommage/parsing des entrées de la file SD (rotation des logs). Un mauvais
// parsing de séquence ou format de chemin -> entrées rejouées/perdues.
#include "../../include/sd_queue_path.h"

using namespace SdQueuePath;

void setUp(void) {}
void tearDown(void) {}

// ---------------- dateStr (epoch UTC -> YYYYMMDD) ----------------

void test_dateStr_epoch_zero(void) {
  char b[16] = {0};
  dateStr(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("19700101", b);
}

void test_dateStr_one_day(void) {
  char b[16] = {0};
  dateStr(86400, b, sizeof(b));  // +1 jour
  TEST_ASSERT_EQUAL_STRING("19700102", b);
}

void test_dateStr_known_2026(void) {
  char b[16] = {0};
  dateStr(1767225600u, b, sizeof(b));  // 2026-01-01 00:00:00 UTC (EPOCH_MIN_VALID)
  TEST_ASSERT_EQUAL_STRING("20260101", b);
}

void test_dateStr_truncation_no_overflow(void) {
  char b[5];
  memset(b, '#', sizeof(b));
  dateStr(0, b, sizeof(b));  // "19700101" tronqué à 4 + '\0'
  TEST_ASSERT_TRUE(strlen(b) < sizeof(b));
}

// ---------------- seqToPath ----------------

void test_seqToPath_padding(void) {
  char b[32] = {0};
  seqToPath(42, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/queue/000042.dat", b);
}

void test_seqToPath_zero(void) {
  char b[32] = {0};
  seqToPath(0, b, sizeof(b));
  TEST_ASSERT_EQUAL_STRING("/queue/000000.dat", b);
}

void test_seqToPath_large_seq_not_truncated_number(void) {
  char b[32] = {0};
  seqToPath(1234567, b, sizeof(b));  // %06 = largeur mini, 7 chiffres conservés
  TEST_ASSERT_EQUAL_STRING("/queue/1234567.dat", b);
}

// ---------------- seqFromEntryName ----------------

void test_seqFromEntryName_with_path(void) {
  TEST_ASSERT_EQUAL_UINT32(42, seqFromEntryName("/queue/000042.dat"));
}

void test_seqFromEntryName_basename_only(void) {
  TEST_ASSERT_EQUAL_UINT32(123, seqFromEntryName("000123.dat"));
}

void test_seqFromEntryName_zero_and_null(void) {
  TEST_ASSERT_EQUAL_UINT32(0, seqFromEntryName("/queue/000000.dat"));
  TEST_ASSERT_EQUAL_UINT32(0, seqFromEntryName(nullptr));
  TEST_ASSERT_EQUAL_UINT32(0, seqFromEntryName("abc.dat"));  // pas de chiffres en tête
}

// ---------------- round-trip ----------------

void test_roundtrip_seq_path_seq(void) {
  const uint32_t seqs[] = {0, 1, 42, 999999, 1234567};
  for (uint32_t s : seqs) {
    char path[32] = {0};
    seqToPath(s, path, sizeof(path));
    TEST_ASSERT_EQUAL_UINT32(s, seqFromEntryName(path));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dateStr_epoch_zero);
  RUN_TEST(test_dateStr_one_day);
  RUN_TEST(test_dateStr_known_2026);
  RUN_TEST(test_dateStr_truncation_no_overflow);
  RUN_TEST(test_seqToPath_padding);
  RUN_TEST(test_seqToPath_zero);
  RUN_TEST(test_seqToPath_large_seq_not_truncated_number);
  RUN_TEST(test_seqFromEntryName_with_path);
  RUN_TEST(test_seqFromEntryName_basename_only);
  RUN_TEST(test_seqFromEntryName_zero_and_null);
  RUN_TEST(test_roundtrip_seq_path_seq);
  return UNITY_END();
}
