// Tests Unity natifs pour n3_wifi_reconnect (noyau pur du fast-reconnect WiFi).
//
//   pio test -c platformio-native.ini -e native -f test_wifi_reconnect
//
// Ce noyau décide si l'on peut ré-associer directement au dernier AP connu
// (au lieu d'un scan complet) après un deep sleep. Un bug ici = tentative sur un
// AP obsolète / mauvais credential, ou perte du raccourci → reconnexions lentes.
// Chaque règle (validation du record, magic par firmware, correspondance SSID,
// politique de timeout) est vérifiée.

#include <unity.h>

#include <cstring>

#include "n3_wifi_reconnect.cpp"  // implémentation incluse (TU isolée)

using namespace N3WifiReconnect;

// Magics distincts façon n3_wifi ("N3WG") et poissonglouton ("PGLW").
static constexpr uint32_t kMagicA = 0x4E335747UL;
static constexpr uint32_t kMagicB = 0x50474C57UL;

void setUp() {}
void tearDown() {}

namespace {
const uint8_t kBssid[6] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
}

// --- store : cas nominal ---

void test_store_ok() {
  LastGoodAp rec{};
  const bool ok = store(rec, kMagicA, "HomeNet", kBssid, 6);

  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_UINT32(kMagicA, rec.magic);
  TEST_ASSERT_EQUAL_UINT8(6, rec.channel);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(kBssid, rec.bssid, 6);
  TEST_ASSERT_EQUAL_STRING("HomeNet", rec.ssid);
}

// --- store : troncature du SSID à 32 caractères ---

void test_store_truncates_long_ssid() {
  LastGoodAp rec{};
  const char* longSsid = "0123456789ABCDEF0123456789ABCDEF_EXTRA";  // > 32
  store(rec, kMagicA, longSsid, kBssid, 11);

  TEST_ASSERT_EQUAL_UINT(32, strlen(rec.ssid));
  TEST_ASSERT_EQUAL_STRING("0123456789ABCDEF0123456789ABCDEF", rec.ssid);
}

// --- store : entrées invalides -> magic remis à 0, false ---

void test_store_rejects_invalid_channel() {
  LastGoodAp rec{};
  rec.magic = kMagicA;  // valeur préexistante à invalider
  TEST_ASSERT_FALSE(store(rec, kMagicA, "Net", kBssid, 0));
  TEST_ASSERT_EQUAL_UINT32(0, rec.magic);

  rec.magic = kMagicA;
  TEST_ASSERT_FALSE(store(rec, kMagicA, "Net", kBssid, -1));
  TEST_ASSERT_EQUAL_UINT32(0, rec.magic);
}

void test_store_rejects_null_bssid_and_empty_ssid() {
  LastGoodAp rec{};
  TEST_ASSERT_FALSE(store(rec, kMagicA, "Net", nullptr, 6));
  TEST_ASSERT_EQUAL_UINT32(0, rec.magic);

  TEST_ASSERT_FALSE(store(rec, kMagicA, "", kBssid, 6));
  TEST_ASSERT_EQUAL_UINT32(0, rec.magic);

  TEST_ASSERT_FALSE(store(rec, kMagicA, nullptr, kBssid, 6));
  TEST_ASSERT_EQUAL_UINT32(0, rec.magic);
}

// --- valid : magic + canal ---

void test_valid() {
  LastGoodAp rec{};
  store(rec, kMagicA, "Net", kBssid, 6);

  TEST_ASSERT_TRUE(valid(rec, kMagicA));
  // Mauvais magic (record d'un autre firmware / cold boot) -> invalide.
  TEST_ASSERT_FALSE(valid(rec, kMagicB));

  // Canal 0 -> invalide même si magic correct.
  rec.channel = 0;
  TEST_ASSERT_FALSE(valid(rec, kMagicA));
}

void test_valid_cold_boot_zeroed() {
  LastGoodAp rec{};  // RTC à froid = zéro
  TEST_ASSERT_FALSE(valid(rec, kMagicA));
}

// --- matchIndex : SSID mémorisé vs credentials configurés ---

void test_match_found() {
  LastGoodAp rec{};
  store(rec, kMagicA, "NetB", kBssid, 6);
  const char* ssids[] = {"NetA", "NetB", "NetC"};

  TEST_ASSERT_EQUAL_INT(1, matchIndex(rec, kMagicA, ssids, 3));
}

void test_match_not_configured() {
  LastGoodAp rec{};
  store(rec, kMagicA, "Ghost", kBssid, 6);
  const char* ssids[] = {"NetA", "NetB"};

  TEST_ASSERT_EQUAL_INT(-1, matchIndex(rec, kMagicA, ssids, 2));
}

void test_match_invalid_record() {
  LastGoodAp rec{};  // magic 0
  const char* ssids[] = {"NetA"};
  TEST_ASSERT_EQUAL_INT(-1, matchIndex(rec, kMagicA, ssids, 1));

  // Bon SSID mais mauvais magic -> pas de match.
  store(rec, kMagicB, "NetA", kBssid, 6);
  TEST_ASSERT_EQUAL_INT(-1, matchIndex(rec, kMagicA, ssids, 1));
}

void test_match_ignores_null_ssid_entries() {
  LastGoodAp rec{};
  store(rec, kMagicA, "NetB", kBssid, 6);
  const char* ssids[] = {nullptr, "NetB", nullptr};
  TEST_ASSERT_EQUAL_INT(1, matchIndex(rec, kMagicA, ssids, 3));
}

// --- fastTimeoutMs : moitié, plancher 2000 (borné au timeout complet) ---

void test_fast_timeout_policy() {
  TEST_ASSERT_EQUAL_UINT32(7500, fastTimeoutMs(15000));  // config PGL
  TEST_ASSERT_EQUAL_UINT32(2500, fastTimeoutMs(5000));   // config n3_wifi typique
  TEST_ASSERT_EQUAL_UINT32(2000, fastTimeoutMs(4000));   // moitié = 2000 (plancher)
  TEST_ASSERT_EQUAL_UINT32(2000, fastTimeoutMs(3000));   // moitié 1500 -> plancher 2000
  // Timeout complet < 2000 : plancher borné au timeout complet.
  TEST_ASSERT_EQUAL_UINT32(1500, fastTimeoutMs(1500));
  TEST_ASSERT_EQUAL_UINT32(1000, fastTimeoutMs(1000));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_store_ok);
  RUN_TEST(test_store_truncates_long_ssid);
  RUN_TEST(test_store_rejects_invalid_channel);
  RUN_TEST(test_store_rejects_null_bssid_and_empty_ssid);
  RUN_TEST(test_valid);
  RUN_TEST(test_valid_cold_boot_zeroed);
  RUN_TEST(test_match_found);
  RUN_TEST(test_match_not_configured);
  RUN_TEST(test_match_invalid_record);
  RUN_TEST(test_match_ignores_null_ssid_entries);
  RUN_TEST(test_fast_timeout_policy);
  return UNITY_END();
}
