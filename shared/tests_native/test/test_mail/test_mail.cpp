// Tests Unity natifs pour n3_mail — construction PURE du corps des mails
// (snprintf borné + repli des champs vides). Pas de SMTP (n3MailSendText non
// testé : nécessite un vrai serveur ; ESP_Mail_Client est stubé pour compiler).
//
//   pio test -c platformio-native.ini -e native -f test_mail
//
// On valide : le repli n3SafeStr (champ NULL/vide -> valeur par défaut), la
// présence des données clés dans le corps, le succès/echec selon la taille du
// buffer, et le garde-fou (outBody NULL ou taille 0 -> false). C'est exactement
// ce que partagent les rapports de n3pp/msp/ffp5cs.

#include <Arduino.h>             // mock
#include <unity.h>
#include <string.h>
#include "n3_mail.cpp"           // impl (ESP_Mail_Client.h résolu via -I stubs)

void setUp() {}
void tearDown() {}

static bool contains(const char* haystack, const char* needle) {
  return strstr(haystack, needle) != nullptr;
}

// ---------- n3MailBuildDebugBody ----------

void test_debug_body_contient_champs() {
  N3MailDebugInfo info{};
  info.projectName = "n3pp";
  info.targetName = "capteur-1";
  info.firmwareVersion = "1.2.3";
  info.eventName = "boot";
  info.localTime = "2026-06-16 10:00";
  info.uptimeSeconds = 42;
  info.wifiSsid = "MonWifi";
  info.wifiRssi = -55;
  info.freeHeap = 123456;
  info.extraInfo = "RAS";

  char body[1024];
  bool ok = n3MailBuildDebugBody(info, body, sizeof(body));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(contains(body, "[n3pp] Notification boot"));
  TEST_ASSERT_TRUE(contains(body, "Cible: capteur-1"));
  TEST_ASSERT_TRUE(contains(body, "Version firmware: 1.2.3"));
  TEST_ASSERT_TRUE(contains(body, "Uptime: 42 s"));
  TEST_ASSERT_TRUE(contains(body, "WiFi SSID: MonWifi"));
  TEST_ASSERT_TRUE(contains(body, "RSSI: -55 dBm"));
  TEST_ASSERT_TRUE(contains(body, "RAS"));
}

void test_debug_body_replis_par_defaut_sur_champs_vides() {
  N3MailDebugInfo info{};       // tout NULL / 0
  char body[1024];
  bool ok = n3MailBuildDebugBody(info, body, sizeof(body));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(contains(body, "[n3-device] Notification event")); // projet+event défaut
  TEST_ASSERT_TRUE(contains(body, "Cible: unknown"));
  TEST_ASSERT_TRUE(contains(body, "Version firmware: unknown"));
  TEST_ASSERT_TRUE(contains(body, "WiFi SSID: (deconnecte)"));
  TEST_ASSERT_TRUE(contains(body, "IP: (n/a)"));
  TEST_ASSERT_TRUE(contains(body, "Aucune information complementaire."));
}

void test_debug_body_chaine_vide_traitee_comme_absente() {
  N3MailDebugInfo info{};
  info.projectName = "";        // vide -> repli "n3-device"
  char body[1024];
  TEST_ASSERT_TRUE(n3MailBuildDebugBody(info, body, sizeof(body)));
  TEST_ASSERT_TRUE(contains(body, "[n3-device]"));
}

void test_debug_body_buffer_trop_petit_retourne_false() {
  N3MailDebugInfo info{};
  info.projectName = "n3pp";
  char body[16];                // trop petit pour le gabarit complet
  TEST_ASSERT_FALSE(n3MailBuildDebugBody(info, body, sizeof(body)));
}

void test_debug_body_garde_fou_null_et_taille_zero() {
  N3MailDebugInfo info{};
  char body[64];
  TEST_ASSERT_FALSE(n3MailBuildDebugBody(info, nullptr, sizeof(body)));
  TEST_ASSERT_FALSE(n3MailBuildDebugBody(info, body, 0));
}

// ---------- n3MailBuildNetReportBody ----------

void test_net_report_body_contient_stats() {
  N3MailNetReportInfo info{};
  info.projectName = "msp";
  info.sensorName = "niveau";
  info.firmwareVersion = "2.0.0";
  info.bootCount = 7;
  info.reportPeriodSeconds = 7200;   // ~2.0 h
  info.httpTimeoutMs = 5000;
  info.stats.postCount = 10;
  info.stats.postOkCount = 9;
  info.stats.postFailCount = 1;
  info.stats.postMaxDurationMs = 1234;
  info.stats.postAvgDurationMs = 567;
  info.stats.getCount = 4;

  char body[2048];
  bool ok = n3MailBuildNetReportBody(info, body, sizeof(body));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(contains(body, "[msp] Rapport reseau"));
  TEST_ASSERT_TRUE(contains(body, "Capteur: niveau | firmware: 2.0.0"));
  TEST_ASSERT_TRUE(contains(body, "bootCount: 7"));
  TEST_ASSERT_TRUE(contains(body, "(~2.0 h)"));       // 7200 s / 3600
  TEST_ASSERT_TRUE(contains(body, "Envoi: 10 | OK: 9 | echecs: 1"));
  TEST_ASSERT_TRUE(contains(body, "Max: 1234 ms | moyenne: 567 ms"));
}

void test_net_report_body_replis_par_defaut() {
  N3MailNetReportInfo info{};    // tout NULL / 0
  char body[2048];
  TEST_ASSERT_TRUE(n3MailBuildNetReportBody(info, body, sizeof(body)));
  TEST_ASSERT_TRUE(contains(body, "[n3-device] Rapport reseau"));
  TEST_ASSERT_TRUE(contains(body, "Capteur: unknown | firmware: unknown"));
  TEST_ASSERT_TRUE(contains(body, "SSID: (deconnecte) | IP: (n/a)"));
}

void test_net_report_body_buffer_trop_petit_retourne_false() {
  N3MailNetReportInfo info{};
  info.projectName = "msp";
  char body[32];                 // trop petit pour le gabarit complet
  TEST_ASSERT_FALSE(n3MailBuildNetReportBody(info, body, sizeof(body)));
}

void test_net_report_body_garde_fou_null_et_taille_zero() {
  N3MailNetReportInfo info{};
  char body[64];
  TEST_ASSERT_FALSE(n3MailBuildNetReportBody(info, nullptr, sizeof(body)));
  TEST_ASSERT_FALSE(n3MailBuildNetReportBody(info, body, 0));
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_debug_body_contient_champs);
  RUN_TEST(test_debug_body_replis_par_defaut_sur_champs_vides);
  RUN_TEST(test_debug_body_chaine_vide_traitee_comme_absente);
  RUN_TEST(test_debug_body_buffer_trop_petit_retourne_false);
  RUN_TEST(test_debug_body_garde_fou_null_et_taille_zero);
  RUN_TEST(test_net_report_body_contient_stats);
  RUN_TEST(test_net_report_body_replis_par_defaut);
  RUN_TEST(test_net_report_body_buffer_trop_petit_retourne_false);
  RUN_TEST(test_net_report_body_garde_fou_null_et_taille_zero);
  return UNITY_END();
}
