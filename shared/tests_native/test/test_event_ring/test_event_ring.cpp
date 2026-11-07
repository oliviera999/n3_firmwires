#include <unity.h>
#include <cstring>
#include <cstdio>

// Tests de n3evt::Ring (shared/n3_common/n3_event_ring.h) — journal de bord persistant
// (incident FFP3 06/11/2026 : aucune trace n'avait survécu au reboot). Logique PURE :
// séquence monotone, écrasement du plus ancien, acquittement par séquence, JSON compact
// pour /ota-report et le champ `log` du heartbeat, aller-retour slot texte (NVS).
#include "n3_event_ring.h"

using Ring4 = n3evt::Ring<4>;

void setUp(void) {}
void tearDown(void) {}

void test_push_assigns_increasing_sequences(void) {
  Ring4 r;
  TEST_ASSERT_EQUAL_UINT32(1, r.push("boot", "POWERON", 1700000000UL, 1));
  TEST_ASSERT_EQUAL_UINT32(2, r.push("wifi_ok", "", 1700000005UL, 6));
  TEST_ASSERT_EQUAL_UINT8(2, r.pendingCount());
  TEST_ASSERT_EQUAL_UINT32(2, r.maxSeq());
}

void test_overflow_overwrites_oldest(void) {
  Ring4 r;
  for (int i = 1; i <= 6; ++i) r.push("e", "", 0, (uint32_t)i);
  TEST_ASSERT_EQUAL_UINT8(4, r.pendingCount());
  char json[512];
  uint32_t last = 0;
  TEST_ASSERT_EQUAL_UINT8(4, r.toJson(json, sizeof(json), &last));
  // Les séquences 1 et 2 ont été écrasées, l'ordre est chronologique (3,4,5,6).
  TEST_ASSERT_NOT_NULL(strstr(json, "{\"q\":3,"));
  TEST_ASSERT_NULL(strstr(json, "{\"q\":1,"));
  TEST_ASSERT_TRUE(strstr(json, "\"q\":3") < strstr(json, "\"q\":6"));
  TEST_ASSERT_EQUAL_UINT32(6, last);
}

void test_ack_clears_up_to_sequence(void) {
  Ring4 r;
  r.push("a", "", 0, 0); r.push("b", "", 0, 0); r.push("c", "", 0, 0);
  TEST_ASSERT_EQUAL_UINT8(2, r.ackUpTo(2));
  TEST_ASSERT_EQUAL_UINT8(1, r.pendingCount());
  char json[256];
  r.toJson(json, sizeof(json));
  TEST_ASSERT_EQUAL_STRING("[{\"q\":3,\"t\":0,\"u\":0,\"e\":\"c\",\"d\":\"\"}]", json);
  // Un nouvel événement après ack continue la séquence (pas de réutilisation de 1..3).
  TEST_ASSERT_EQUAL_UINT32(4, r.push("d", "", 0, 0));
}

void test_json_escapes_detail_and_is_form_safe(void) {
  Ring4 r;
  r.push("ota_fail", "Update.end \"MD5\" a&b=c+d\\", 1700000000UL, 42);
  char json[256];
  r.toJson(json, sizeof(json));
  TEST_ASSERT_NOT_NULL(strstr(json, "\\\"MD5\\\""));
  TEST_ASSERT_NOT_NULL(strstr(json, "\\\\\"}"));
  // Le JSON voyage dans un corps x-www-form-urlencoded : jamais de '&' ni '+' bruts.
  TEST_ASSERT_NULL(strchr(json, '&'));
  TEST_ASSERT_NULL(strchr(json, '+'));
}

void test_json_truncates_to_buffer_keeping_oldest(void) {
  Ring4 r;
  for (int i = 0; i < 4; ++i) r.push("evenement_long", "detail assez long pour tester", 1700000000UL, 10);
  char json[120];
  uint32_t last = 0;
  uint8_t n = r.toJson(json, sizeof(json), &last);
  TEST_ASSERT_TRUE(n >= 1 && n < 4);
  TEST_ASSERT_EQUAL_UINT32(n, last);  // le dernier écrit est le n-ième plus ancien
  TEST_ASSERT_EQUAL_CHAR(']', json[strlen(json) - 1]);
  TEST_ASSERT_TRUE(strlen(json) < sizeof(json));
}

void test_empty_ring_is_empty_array(void) {
  Ring4 r;
  char json[16];
  TEST_ASSERT_EQUAL_UINT8(0, r.toJson(json, sizeof(json)));
  TEST_ASSERT_EQUAL_STRING("[]", json);
}

void test_slot_roundtrip_persists_and_restores_sequence(void) {
  Ring4 src;
  src.push("boot", "PANIC|cause", 1700000000UL, 1);
  src.push("wifi_fail", "reason=201", 1700000030UL, 31);
  src.ackUpTo(1);
  char slots[4][n3evt::kSlotChars];
  for (uint8_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(src.encodeSlot(i, slots[i], sizeof(slots[i])));
  TEST_ASSERT_EQUAL_STRING("", slots[0]);  // acquitté → vide
  TEST_ASSERT_EQUAL_STRING("2|1700000030|31|wifi_fail|reason=201", slots[1]);

  Ring4 dst;
  for (uint8_t i = 0; i < 4; ++i) TEST_ASSERT_TRUE(dst.decodeSlot(i, slots[i]));
  dst.recomputeHead();
  TEST_ASSERT_EQUAL_UINT8(1, dst.pendingCount());
  TEST_ASSERT_EQUAL_UINT32(3, dst.nextSeq());  // reprend après la plus haute séquence vue
  TEST_ASSERT_EQUAL_UINT32(3, dst.push("ntp_ok", "", 0, 0));
  TEST_ASSERT_EQUAL_UINT8(2, dst.pendingCount());
  // Le slot vide 0 a été réutilisé, le slot 1 (seq 2) conservé.
  TEST_ASSERT_EQUAL_UINT32(2, dst.slot(1).seq);
}

void test_decode_rejects_garbage_as_empty(void) {
  Ring4 r;
  TEST_ASSERT_FALSE(r.decodeSlot(0, "garbage"));
  TEST_ASSERT_EQUAL_UINT32(0, r.slot(0).seq);
  TEST_ASSERT_TRUE(r.decodeSlot(1, ""));
  TEST_ASSERT_FALSE(r.decodeSlot(9, ""));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_push_assigns_increasing_sequences);
  RUN_TEST(test_overflow_overwrites_oldest);
  RUN_TEST(test_ack_clears_up_to_sequence);
  RUN_TEST(test_json_escapes_detail_and_is_form_safe);
  RUN_TEST(test_json_truncates_to_buffer_keeping_oldest);
  RUN_TEST(test_empty_ring_is_empty_array);
  RUN_TEST(test_slot_roundtrip_persists_and_restores_sequence);
  RUN_TEST(test_decode_rejects_garbage_as_empty);
  return UNITY_END();
}
