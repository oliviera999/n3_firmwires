// Tests Unity natifs pour n3_data/n3_net_stats — agregation pure des stats
// reseau (compteurs POST/GET, max, moyenne, near-timeout, snapshot, reset).
//
//   pio test -c platformio-native.ini -e native -f test_net_stats
//
// Aucune dependance materiel/reseau : n3_net_stats.cpp ne fait que de
// l'arithmetique sur un etat statique. C'est exactement le code partage qui
// alimente les rapports mail (N3MailNetReportInfo.stats) de n3pp/msp/ffp5cs.

#include <Arduino.h>             // mock (mocks/Arduino.h)
#include <unity.h>
#include "n3_net_stats.cpp"      // impl incluse (n3_data.h + n3_defaults.h résolus via -I)

// N3_HTTP_TIMEOUT_MS = 5000 (n3_defaults.h) → seuil near-timeout = 4500 ms.

void setUp() { n3NetStatsResetPeriod(); }   // état frais à chaque test
void tearDown() {}

void test_post_ok_incremente_ok_et_total() {
  n3NetStatsRecordPost(200, 100, -40);
  n3NetStatsRecordPost(204, 120, -41);
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(2, s.postCount);
  TEST_ASSERT_EQUAL_UINT32(2, s.postOkCount);
  TEST_ASSERT_EQUAL_UINT32(0, s.postFailCount);
}

void test_post_code_hors_2xx_3xx_compte_comme_echec() {
  n3NetStatsRecordPost(200, 10, -40);   // ok
  n3NetStatsRecordPost(404, 10, -40);   // 4xx -> echec
  n3NetStatsRecordPost(500, 10, -40);   // 5xx -> echec
  n3NetStatsRecordPost(-1, 10, -40);    // reseau -> echec
  n3NetStatsRecordPost(399, 10, -40);   // 3xx -> ok (borne haute exclue a 400)
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(5, s.postCount);
  TEST_ASSERT_EQUAL_UINT32(2, s.postOkCount);   // 200, 399
  TEST_ASSERT_EQUAL_UINT32(3, s.postFailCount); // 404, 500, -1
}

void test_post_max_et_moyenne_duree() {
  n3NetStatsRecordPost(200, 100, -40);
  n3NetStatsRecordPost(200, 300, -40);
  n3NetStatsRecordPost(200, 200, -40);
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(300, s.postMaxDurationMs);
  TEST_ASSERT_EQUAL_UINT32(200, s.postAvgDurationMs); // (100+300+200)/3
  TEST_ASSERT_EQUAL_UINT32(200, s.postLastDurationMs);
}

void test_post_near_timeout_compte_au_dela_du_seuil() {
  n3NetStatsRecordPost(200, 4499, -40);  // < 4500 : pas near-timeout
  n3NetStatsRecordPost(200, 4500, -40);  // == seuil : near-timeout
  n3NetStatsRecordPost(200, 6000, -40);  // au-dela : near-timeout
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(2, s.postNearTimeoutCount);
}

void test_post_dernier_code_et_rssi_memorises() {
  n3NetStatsRecordPost(200, 50, -40);
  n3NetStatsRecordPost(503, 70, -77);
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_INT(503, s.postLastCode);
  TEST_ASSERT_EQUAL_INT(-77, s.postLastRssi);
}

void test_get_compteurs_independants_du_post() {
  n3NetStatsRecordPost(200, 100, -40);
  n3NetStatsRecordGet(200, 80, -45);
  n3NetStatsRecordGet(500, 90, -46);
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(1, s.postCount);
  TEST_ASSERT_EQUAL_UINT32(2, s.getCount);
  TEST_ASSERT_EQUAL_UINT32(1, s.getOkCount);
  TEST_ASSERT_EQUAL_UINT32(1, s.getFailCount);
  TEST_ASSERT_EQUAL_UINT32(90, s.getMaxDurationMs);
  TEST_ASSERT_EQUAL_INT(500, s.getLastCode);
  TEST_ASSERT_EQUAL_INT(-46, s.getLastRssi);
}

void test_snapshot_vide_avant_tout_enregistrement() {
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(0, s.postCount);
  TEST_ASSERT_EQUAL_UINT32(0, s.getCount);
  TEST_ASSERT_EQUAL_UINT32(0, s.postAvgDurationMs); // pas de division par 0
}

void test_reset_remet_tout_a_zero() {
  n3NetStatsRecordPost(200, 100, -40);
  n3NetStatsRecordGet(200, 80, -45);
  n3NetStatsResetPeriod();
  N3NetStatsSnapshot s;
  n3NetStatsGetSnapshot(s);
  TEST_ASSERT_EQUAL_UINT32(0, s.postCount);
  TEST_ASSERT_EQUAL_UINT32(0, s.postOkCount);
  TEST_ASSERT_EQUAL_UINT32(0, s.postMaxDurationMs);
  TEST_ASSERT_EQUAL_UINT32(0, s.getCount);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_post_ok_incremente_ok_et_total);
  RUN_TEST(test_post_code_hors_2xx_3xx_compte_comme_echec);
  RUN_TEST(test_post_max_et_moyenne_duree);
  RUN_TEST(test_post_near_timeout_compte_au_dela_du_seuil);
  RUN_TEST(test_post_dernier_code_et_rssi_memorises);
  RUN_TEST(test_get_compteurs_independants_du_post);
  RUN_TEST(test_snapshot_vide_avant_tout_enregistrement);
  RUN_TEST(test_reset_remet_tout_a_zero);
  return UNITY_END();
}
