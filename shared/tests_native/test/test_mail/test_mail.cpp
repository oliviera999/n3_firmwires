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

// ---------- n3MailSendMessageWithSession (session possedee par l'appelant) ----------
// On exerce la construction du SMTP_Message + le passage a MailClient.sendMail via
// le stub ESP_Mail_Client (cf. stubs/ESP_Mail_Client.h) : champs requis, replis de
// noms, opt-in des drapeaux charSet/encoding/priorite, et passthrough du verdict.

static N3MailMessageSpec makeValidSpec() {
  N3MailMessageSpec spec{};
  spec.senderEmail = "from@example.com";
  spec.recipientEmail = "to@example.com";
  spec.subject = "Sujet";
  spec.body = "Corps";
  return spec;
}

void test_send_with_session_rejette_champs_manquants() {
  SMTPSession smtp;
  String err;
  N3MailMessageSpec spec = makeValidSpec();

  spec.senderEmail = nullptr;
  TEST_ASSERT_FALSE(n3MailSendMessageWithSession(smtp, spec, &err));
  spec = makeValidSpec(); spec.recipientEmail = nullptr;
  TEST_ASSERT_FALSE(n3MailSendMessageWithSession(smtp, spec, &err));
  spec = makeValidSpec(); spec.subject = nullptr;
  TEST_ASSERT_FALSE(n3MailSendMessageWithSession(smtp, spec, &err));
  spec = makeValidSpec(); spec.body = nullptr;
  TEST_ASSERT_FALSE(n3MailSendMessageWithSession(smtp, spec, &err));
}

void test_send_with_session_succes_passe_le_message() {
  SMTPSession smtp;
  String err;
  N3MailMessageSpec spec = makeValidSpec();  // drapeaux par defaut a false
  MailClient.nextSendResult = true;          // simule un envoi OK
  MailClient.lastMessage = nullptr;
  MailClient.sendMailCallCount = 0;

  bool ok = n3MailSendMessageWithSession(smtp, spec, &err);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_INT(1, MailClient.sendMailCallCount);
  TEST_ASSERT_NOT_NULL(MailClient.lastMessage);
  TEST_ASSERT_EQUAL_STRING("Sujet", MailClient.lastMessage->subject);
  TEST_ASSERT_EQUAL_STRING("Corps", MailClient.lastMessage->text.content);
  TEST_ASSERT_EQUAL_STRING("from@example.com", MailClient.lastMessage->sender.email);
  // Repli des noms : senderName NULL -> "n3 IoT", recipientName NULL -> "Destinataire"
  TEST_ASSERT_EQUAL_STRING("n3 IoT", MailClient.lastMessage->sender.name);
  TEST_ASSERT_EQUAL_STRING("Destinataire", MailClient.lastMessage->lastRecipientName);
  TEST_ASSERT_EQUAL_STRING("to@example.com", MailClient.lastMessage->lastRecipientEmail);
  // Drapeaux non actives -> champs laisses a l'etat initial du stub.
  TEST_ASSERT_NULL(MailClient.lastMessage->text.charSet);
  TEST_ASSERT_EQUAL_INT(-1, MailClient.lastMessage->priority);
}

void test_send_with_session_optin_drapeaux_et_noms() {
  SMTPSession smtp;
  N3MailMessageSpec spec = makeValidSpec();
  spec.senderName = "FFP5CS";
  spec.recipientName = "User";
  spec.setCharsetUtf8 = true;
  spec.set7bitEncoding = true;
  spec.setLowPriority = true;
  MailClient.nextSendResult = true;
  MailClient.lastMessage = nullptr;

  TEST_ASSERT_TRUE(n3MailSendMessageWithSession(smtp, spec, nullptr));
  TEST_ASSERT_NOT_NULL(MailClient.lastMessage);
  TEST_ASSERT_EQUAL_STRING("FFP5CS", MailClient.lastMessage->sender.name);
  TEST_ASSERT_EQUAL_STRING("User", MailClient.lastMessage->lastRecipientName);
  TEST_ASSERT_EQUAL_STRING("utf-8", MailClient.lastMessage->text.charSet);
  TEST_ASSERT_EQUAL_INT((int)Content_Transfer_Encoding::enc_7bit,
                        (int)MailClient.lastMessage->text.transfer_encoding);
  TEST_ASSERT_EQUAL_INT((int)esp_mail_smtp_priority::esp_mail_smtp_priority_low,
                        MailClient.lastMessage->priority);
}

void test_send_with_session_echec_renseigne_erreur() {
  SMTPSession smtp;
  String err;
  N3MailMessageSpec spec = makeValidSpec();
  MailClient.nextSendResult = false;  // simule un echec d'envoi
  MailClient.lastMessage = nullptr;

  TEST_ASSERT_FALSE(n3MailSendMessageWithSession(smtp, spec, &err));
  TEST_ASSERT_TRUE(contains(err.c_str(), "SMTP envoi echec"));
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
  RUN_TEST(test_send_with_session_rejette_champs_manquants);
  RUN_TEST(test_send_with_session_succes_passe_le_message);
  RUN_TEST(test_send_with_session_optin_drapeaux_et_noms);
  RUN_TEST(test_send_with_session_echec_renseigne_erreur);
  return UNITY_END();
}
