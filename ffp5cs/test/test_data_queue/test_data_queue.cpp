#include <unity.h>
#include "test_mocks.h"  // mock Serial (HardwareSerial) utilisé par data_queue.cpp

// Test natif (hôte) de DataQueue : file circulaire RAM pour le rejeu des POST
// hors-ligne (résilience offline). Logique pure (pas de FreeRTOS), inclusion
// directe du .cpp comme test_post_body.
//
// Sémantique vérifiée/figée :
//   - FIFO ; capacité maxEntries bornée à [1..5] par le constructeur
//   - file PLEINE -> push écrase la PLUS ANCIENNE entrée (ring buffer écrasant)
//   - rejets : non initialisée, payload null/vide, payload >= 1024 octets
//   - peek ne retire pas ; pop retire ; lecture tronquée si buffer trop petit
#include "../../src/data_queue.cpp"

void setUp(void) {}
void tearDown(void) {}

void test_begin_then_empty(void) {
  DataQueue q;
  TEST_ASSERT_TRUE(q.begin());
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_push_without_begin_fails(void) {
  DataQueue q;
  TEST_ASSERT_FALSE(q.push("x"));
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_peek_pop_on_empty_fail(void) {
  DataQueue q;
  q.begin();
  char buf[32] = "sentinel";
  TEST_ASSERT_FALSE(q.peek(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("", buf);  // peek vide remet buffer[0]='\0'
  TEST_ASSERT_FALSE(q.pop(buf, sizeof(buf)));
}

void test_push_peek_pop_roundtrip(void) {
  DataQueue q;
  q.begin();
  TEST_ASSERT_TRUE(q.push("hello"));
  TEST_ASSERT_EQUAL_UINT16(1, q.size());

  char buf[32] = {0};
  TEST_ASSERT_TRUE(q.peek(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("hello", buf);
  TEST_ASSERT_EQUAL_UINT16(1, q.size());  // peek ne retire pas

  buf[0] = '\0';
  TEST_ASSERT_TRUE(q.pop(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("hello", buf);
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_fifo_order(void) {
  DataQueue q;
  q.begin();
  q.push("1"); q.push("2"); q.push("3");
  char buf[32];
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("1", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("2", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("3", buf);
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_overwrite_oldest_when_full(void) {
  DataQueue q;  // capacité par défaut = 5
  q.begin();
  q.push("1"); q.push("2"); q.push("3"); q.push("4"); q.push("5");
  TEST_ASSERT_EQUAL_UINT16(5, q.size());
  q.push("6");  // file pleine -> écrase la plus ancienne ("1")
  TEST_ASSERT_EQUAL_UINT16(5, q.size());

  char buf[32];
  // L'ordre restant doit être 2,3,4,5,6 (1 perdu).
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("2", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("3", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("4", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("5", buf);
  q.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("6", buf);
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_null_and_empty_payload_rejected(void) {
  DataQueue q;
  q.begin();
  TEST_ASSERT_FALSE(q.push(nullptr));
  TEST_ASSERT_FALSE(q.push(""));
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
}

void test_oversized_payload_rejected(void) {
  DataQueue q;
  q.begin();
  char big[1100];
  memset(big, 'A', sizeof(big));
  big[sizeof(big) - 1] = '\0';  // 1099 caractères >= PAYLOAD_SIZE (1024)
  TEST_ASSERT_FALSE(q.push(big));
  TEST_ASSERT_EQUAL_UINT16(0, q.size());

  // Juste sous la limite : accepté (1023 caractères).
  char ok[1024];
  memset(ok, 'B', sizeof(ok));
  ok[sizeof(ok) - 1] = '\0';  // 1023 caractères
  TEST_ASSERT_TRUE(q.push(ok));
  TEST_ASSERT_EQUAL_UINT16(1, q.size());
}

void test_pop_invalid_buffer_fails(void) {
  DataQueue q;
  q.begin();
  q.push("data");
  char buf[8];
  TEST_ASSERT_FALSE(q.pop(nullptr, sizeof(buf)));
  TEST_ASSERT_FALSE(q.pop(buf, 0));
  TEST_ASSERT_EQUAL_UINT16(1, q.size());  // échec lecture -> rien retiré
}

void test_peek_truncates_small_buffer(void) {
  // CARACTÉRISATION : lecture dans un buffer trop petit -> tronquée mais
  // renvoie true (perte de données silencieuse côté lecteur).
  DataQueue q;
  q.begin();
  q.push("abcdef");
  char buf[4] = {0};  // ne peut contenir que 3 chars + '\0'
  TEST_ASSERT_TRUE(q.peek(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("abc", buf);
}

void test_clear_empties_and_reusable(void) {
  DataQueue q;
  q.begin();
  q.push("a"); q.push("b");
  q.clear();
  TEST_ASSERT_EQUAL_UINT16(0, q.size());
  // Réutilisable après clear
  TEST_ASSERT_TRUE(q.push("c"));
  char buf[8];
  TEST_ASSERT_TRUE(q.pop(buf, sizeof(buf)));
  TEST_ASSERT_EQUAL_STRING("c", buf);
}

void test_ctor_clamps_max_entries(void) {
  // maxEntries=2 -> capacité 2 (3e push écrase la plus ancienne)
  DataQueue q2(2);
  q2.begin();
  q2.push("a"); q2.push("b"); q2.push("c");
  TEST_ASSERT_EQUAL_UINT16(2, q2.size());
  char buf[8];
  q2.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("b", buf);  // "a" perdu
  q2.pop(buf, sizeof(buf)); TEST_ASSERT_EQUAL_STRING("c", buf);

  // maxEntries=100 -> borné à 5 (6 push -> size reste 5)
  DataQueue qBig(100);
  qBig.begin();
  for (int i = 0; i < 6; ++i) qBig.push("x");
  TEST_ASSERT_EQUAL_UINT16(5, qBig.size());

  // maxEntries=0 -> défaut 5
  DataQueue qZero(0);
  qZero.begin();
  for (int i = 0; i < 6; ++i) qZero.push("y");
  TEST_ASSERT_EQUAL_UINT16(5, qZero.size());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_begin_then_empty);
  RUN_TEST(test_push_without_begin_fails);
  RUN_TEST(test_peek_pop_on_empty_fail);
  RUN_TEST(test_push_peek_pop_roundtrip);
  RUN_TEST(test_fifo_order);
  RUN_TEST(test_overwrite_oldest_when_full);
  RUN_TEST(test_null_and_empty_payload_rejected);
  RUN_TEST(test_oversized_payload_rejected);
  RUN_TEST(test_pop_invalid_buffer_fails);
  RUN_TEST(test_peek_truncates_small_buffer);
  RUN_TEST(test_clear_empties_and_reusable);
  RUN_TEST(test_ctor_clamps_max_entries);
  return UNITY_END();
}
