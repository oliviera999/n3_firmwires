// Tests Unity natifs pour n3_wifi_select (noyau pur de selection WiFi).
//
//   pio test -c platformio-native.ini -e native -f test_wifi_select
//
// Ce noyau decide QUEL reseau essayer et DANS QUEL ORDRE a partir d'un scan.
// Un bug ici = mauvais AP prioritaire / mauvais BSSID+canal => connexions
// lentes ou echouees sur le terrain. Chaque regle (meilleur RSSI, tri
// decroissant, egalites, SSID caches, bornage, entrees invalides) est verifiee.

#include <unity.h>

#include <cstring>

#include "n3_wifi_select.cpp"  // implementation incluse (TU isolee)

using namespace N3WifiSelect;

void setUp() {}
void tearDown() {}

namespace {

// Fabrique une ScanEntry lisible dans les tests.
ScanEntry mk(const char* ssid, int8_t rssi, uint8_t chan, uint8_t b0) {
  ScanEntry e{};
  e.ssid = ssid;
  e.rssi = rssi;
  e.channel = chan;
  const uint8_t bssid[kBssidLen] = {b0, 0x11, 0x22, 0x33, 0x44, 0x55};
  memcpy(e.bssid, bssid, kBssidLen);
  return e;
}

}  // namespace

// --- Cas de base : un seul reseau visible ---

void test_single_visible_network() {
  const char* ssids[] = {"HomeNet"};
  ScanEntry scan[] = {mk("HomeNet", -50, 6, 0xAA)};
  Candidate cand[1];
  size_t order[1];

  size_t n = buildOrder(ssids, 1, scan, 1, cand, order, 0);

  TEST_ASSERT_EQUAL_UINT(1, n);
  TEST_ASSERT_TRUE(cand[0].present);
  TEST_ASSERT_EQUAL_INT(-50, cand[0].rssi);
  TEST_ASSERT_EQUAL_UINT8(6, cand[0].channel);
  TEST_ASSERT_EQUAL_UINT8(0xAA, cand[0].bssid[0]);
  TEST_ASSERT_EQUAL_UINT(0, order[0]);
}

// --- Meilleur RSSI conserve quand un SSID apparait plusieurs fois (multi-AP) ---

void test_keeps_strongest_rssi_for_duplicate_ssid() {
  const char* ssids[] = {"MeshNet"};
  ScanEntry scan[] = {
      mk("MeshNet", -70, 1, 0x01),  // AP faible
      mk("MeshNet", -45, 11, 0x02), // AP fort -> doit gagner
      mk("MeshNet", -60, 6, 0x03),
  };
  Candidate cand[1];
  size_t order[1];

  buildOrder(ssids, 1, scan, 3, cand, order, 0);

  TEST_ASSERT_TRUE(cand[0].present);
  TEST_ASSERT_EQUAL_INT(-45, cand[0].rssi);
  TEST_ASSERT_EQUAL_UINT8(11, cand[0].channel);
  TEST_ASSERT_EQUAL_UINT8(0x02, cand[0].bssid[0]);
}

// --- Tri par RSSI decroissant sur plusieurs credentials ---

void test_orders_by_rssi_descending() {
  const char* ssids[] = {"Weak", "Strong", "Mid"};
  ScanEntry scan[] = {
      mk("Weak", -80, 1, 0x01),
      mk("Strong", -40, 6, 0x02),
      mk("Mid", -60, 11, 0x03),
  };
  Candidate cand[3];
  size_t order[3];

  size_t n = buildOrder(ssids, 3, scan, 3, cand, order, 0);

  TEST_ASSERT_EQUAL_UINT(3, n);
  // Strong (index 1) -> Mid (index 2) -> Weak (index 0)
  TEST_ASSERT_EQUAL_UINT(1, order[0]);
  TEST_ASSERT_EQUAL_UINT(2, order[1]);
  TEST_ASSERT_EQUAL_UINT(0, order[2]);
}

// --- Egalite de RSSI : l'index credential le plus petit passe devant ---

void test_rssi_tie_keeps_lower_index_first() {
  const char* ssids[] = {"NetA", "NetB"};
  ScanEntry scan[] = {
      mk("NetB", -55, 6, 0x0B),
      mk("NetA", -55, 1, 0x0A),
  };
  Candidate cand[2];
  size_t order[2];

  buildOrder(ssids, 2, scan, 2, cand, order, 0);

  // Egalite -> NetA (index 0) avant NetB (index 1)
  TEST_ASSERT_EQUAL_UINT(0, order[0]);
  TEST_ASSERT_EQUAL_UINT(1, order[1]);
}

// --- SSID configures mais absents du scan : ajoutes en fin, ordre d'origine ---

void test_absent_credentials_appended_in_order() {
  const char* ssids[] = {"HiddenFirst", "Visible", "HiddenSecond"};
  ScanEntry scan[] = {mk("Visible", -50, 6, 0x02)};
  Candidate cand[3];
  size_t order[3];

  size_t n = buildOrder(ssids, 3, scan, 1, cand, order, 0);

  TEST_ASSERT_EQUAL_UINT(3, n);
  // Visible d'abord (present), puis les caches dans l'ordre d'origine.
  TEST_ASSERT_EQUAL_UINT(1, order[0]);
  TEST_ASSERT_EQUAL_UINT(0, order[1]);  // HiddenFirst
  TEST_ASSERT_EQUAL_UINT(2, order[2]);  // HiddenSecond
  TEST_ASSERT_TRUE(cand[1].present);
  TEST_ASSERT_FALSE(cand[0].present);
  TEST_ASSERT_FALSE(cand[2].present);
  // Une candidate absente reste a la sentinelle.
  TEST_ASSERT_EQUAL_INT(kRssiAbsent, cand[0].rssi);
  TEST_ASSERT_EQUAL_UINT8(0, cand[0].channel);
}

// --- Aucun reseau visible : tous les credentials en ordre d'origine ---

void test_empty_scan_yields_all_absent() {
  const char* ssids[] = {"N1", "N2"};
  Candidate cand[2];
  size_t order[2];

  size_t n = buildOrder(ssids, 2, nullptr, 0, cand, order, 0);

  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_UINT(0, order[0]);
  TEST_ASSERT_EQUAL_UINT(1, order[1]);
  TEST_ASSERT_FALSE(cand[0].present);
  TEST_ASSERT_FALSE(cand[1].present);
}

// --- Reseaux voisins non configures ignores ---

void test_unconfigured_scan_ssids_ignored() {
  const char* ssids[] = {"Mine"};
  ScanEntry scan[] = {
      mk("Neighbor1", -30, 1, 0x01),
      mk("Mine", -65, 6, 0x02),
      mk("Neighbor2", -35, 11, 0x03),
  };
  Candidate cand[1];
  size_t order[1];

  buildOrder(ssids, 1, scan, 3, cand, order, 0);

  TEST_ASSERT_TRUE(cand[0].present);
  TEST_ASSERT_EQUAL_INT(-65, cand[0].rssi);  // pas le voisin plus fort
  TEST_ASSERT_EQUAL_UINT8(0x02, cand[0].bssid[0]);
}

// --- Bornage maxNetworks (clamp type scanMax / MAX_WIFI_SAVED_NETWORKS) ---

void test_max_networks_clamp() {
  const char* ssids[] = {"A", "B", "C", "D"};
  ScanEntry scan[] = {
      mk("A", -50, 1, 0x0A),
      mk("D", -40, 6, 0x0D),  // hors borne -> jamais considere
  };
  Candidate cand[4];
  size_t order[4];

  // Ne considere que les 2 premiers credentials (A, B).
  size_t n = buildOrder(ssids, 4, scan, 2, cand, order, 2);

  TEST_ASSERT_EQUAL_UINT(2, n);
  TEST_ASSERT_EQUAL_UINT(0, order[0]);  // A present
  TEST_ASSERT_EQUAL_UINT(1, order[1]);  // B absent
  TEST_ASSERT_TRUE(cand[0].present);
  TEST_ASSERT_FALSE(cand[1].present);
}

// --- Entrees invalides : nullptr / SSID vides ---

void test_null_and_empty_ssids_are_safe() {
  const char* ssids[] = {"", "Good", nullptr};
  ScanEntry scan[] = {
      mk("Good", -55, 6, 0x02),
      mk("", -30, 1, 0x09),  // SSID de scan vide -> ignore
  };
  Candidate cand[3];
  size_t order[3];

  size_t n = buildOrder(ssids, 3, scan, 2, cand, order, 0);

  TEST_ASSERT_EQUAL_UINT(3, n);
  // Seul "Good" (index 1) est present ; les slots vides/nullptr ne matchent pas.
  TEST_ASSERT_EQUAL_UINT(1, order[0]);
  TEST_ASSERT_TRUE(cand[1].present);
  TEST_ASSERT_FALSE(cand[0].present);
  TEST_ASSERT_FALSE(cand[2].present);
}

// --- Garde-fous : arguments nuls / count 0 ---

void test_defensive_null_args() {
  Candidate cand[1];
  size_t order[1];
  const char* ssids[] = {"X"};
  ScanEntry scan[] = {mk("X", -50, 1, 0x01)};

  TEST_ASSERT_EQUAL_UINT(0, buildOrder(nullptr, 1, scan, 1, cand, order, 0));
  TEST_ASSERT_EQUAL_UINT(0, buildOrder(ssids, 0, scan, 1, cand, order, 0));
  TEST_ASSERT_EQUAL_UINT(0, buildOrder(ssids, 1, scan, 1, nullptr, order, 0));
  TEST_ASSERT_EQUAL_UINT(0, buildOrder(ssids, 1, scan, 1, cand, nullptr, 0));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_single_visible_network);
  RUN_TEST(test_keeps_strongest_rssi_for_duplicate_ssid);
  RUN_TEST(test_orders_by_rssi_descending);
  RUN_TEST(test_rssi_tie_keeps_lower_index_first);
  RUN_TEST(test_absent_credentials_appended_in_order);
  RUN_TEST(test_empty_scan_yields_all_absent);
  RUN_TEST(test_unconfigured_scan_ssids_ignored);
  RUN_TEST(test_max_networks_clamp);
  RUN_TEST(test_null_and_empty_ssids_are_safe);
  RUN_TEST(test_defensive_null_args);
  return UNITY_END();
}
