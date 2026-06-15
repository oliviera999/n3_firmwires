#include <unity.h>
#include <cstring>

// Tests de LevelAlertOrchestrator (automatism/level_alert_orchestrator.h) — orchestrateur
// d'alerte niveau (aquarium bas / réserve basse), testé nativement via FakeMailer (chantier C4).
// Vérifie le câblage : bonne décision -> bon sujet de mail + bon flag + blink (retour), et la
// distinction aquarium (pas de mail de fin) vs réserve (mail « OK » de fin).
#include "../../include/automatism/level_alert_orchestrator.h"

struct SensorReadings {};  // stub (IMailer ne fait qu'une fwd-decl)

namespace {
struct FakeMailer : public IMailer {
  int alertCount = 0;
  char lastSubject[40] = {0};
  char lastMsg[128] = {0};
  bool send(const char*, const char*, const char*, const char*) override { return true; }
  bool sendAlert(const char* subject, const char* message, const char*, bool = false) override {
    ++alertCount;
    strncpy(lastSubject, subject, sizeof(lastSubject) - 1);
    strncpy(lastMsg, message, sizeof(lastMsg) - 1);
    return true;
  }
  bool sendSleepMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
  bool sendWakeMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
};

// Seuils : alerte si distance > 300 mm ; fin si <= 250 mm.
const uint16_t ALERT = 300, CLEAR = 250;
}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Réserve : distance haute + mail -> Raise -> mail "BASSE", flag=true, blink.
void test_reserve_raise(void) {
  FakeMailer m; bool sent = false;
  bool blink = LevelAlertOrchestrator::run(m, sent, 350, ALERT, CLEAR, true, "e",
                                           "Alerte - Réserve BASSE", "Info - Réserve OK");
  TEST_ASSERT_TRUE(blink);
  TEST_ASSERT_TRUE(sent);
  TEST_ASSERT_EQUAL_STRING("Alerte - Réserve BASSE", m.lastSubject);
  TEST_ASSERT_EQUAL_INT(1, m.alertCount);
}

// Réserve : revenue OK + déjà alerté -> Clear -> mail "OK", flag=false.
void test_reserve_clear(void) {
  FakeMailer m; bool sent = true;
  bool blink = LevelAlertOrchestrator::run(m, sent, 200, ALERT, CLEAR, true, "e",
                                           "Alerte - Réserve BASSE", "Info - Réserve OK");
  TEST_ASSERT_TRUE(blink);
  TEST_ASSERT_FALSE(sent);
  TEST_ASSERT_EQUAL_STRING("Info - Réserve OK", m.lastSubject);
}

// Mail désactivé -> aucun envoi, flag inchangé, pas de blink.
void test_no_mail_when_disabled(void) {
  FakeMailer m; bool sent = false;
  bool blink = LevelAlertOrchestrator::run(m, sent, 350, ALERT, CLEAR, false, "e",
                                           "Alerte - Réserve BASSE", "Info - Réserve OK");
  TEST_ASSERT_FALSE(blink);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
  TEST_ASSERT_FALSE(sent);  // flag pas modifié hors mailEnabled
}

// Aquarium : clearSubject=nullptr -> pas de mail de fin même si décision Clear.
void test_aquarium_no_clear_mail(void) {
  FakeMailer m; bool sent = true;  // déjà alerté
  bool blink = LevelAlertOrchestrator::run(m, sent, 200, ALERT, CLEAR, true, "e",
                                           "Alerte - Niveau aquarium BAS", nullptr);
  TEST_ASSERT_FALSE(blink);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
  TEST_ASSERT_TRUE(sent);  // l'orchestrateur ne reset pas (le reset aquarium est inline ailleurs)
}

// Déjà alerté + toujours bas -> pas de re-Raise.
void test_no_retrigger_when_already_alerted(void) {
  FakeMailer m; bool sent = true;
  bool blink = LevelAlertOrchestrator::run(m, sent, 350, ALERT, CLEAR, true, "e",
                                           "Alerte - Réserve BASSE", "Info - Réserve OK");
  TEST_ASSERT_FALSE(blink);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
}

// Le message reproduit le format historique (avec le « mm ( » redondant).
void test_message_format_parity(void) {
  FakeMailer m; bool sent = false;
  LevelAlertOrchestrator::run(m, sent, 350, ALERT, CLEAR, true, "e", "S", "C");
  TEST_ASSERT_EQUAL_STRING("Distance: 350.0 mm ( mm (> 300.0)", m.lastMsg);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_reserve_raise);
  RUN_TEST(test_reserve_clear);
  RUN_TEST(test_no_mail_when_disabled);
  RUN_TEST(test_aquarium_no_clear_mail);
  RUN_TEST(test_no_retrigger_when_already_alerted);
  RUN_TEST(test_message_format_parity);
  return UNITY_END();
}
