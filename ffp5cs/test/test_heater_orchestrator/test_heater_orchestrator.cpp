#include <unity.h>
#include <cstring>

// Tests de HeaterOrchestrator (automatism/heater_orchestrator.h) — premier ORCHESTRATEUR
// à effets de bord testé nativement, grâce à IActuators + IMailer (chantier C4). On vérifie
// le CÂBLAGE complet : la bonne décision déclenche le bon relais ET le bon mail, et le blink
// (valeur de retour) suit l'envoi du mail. Impossible avant l'introduction des interfaces.
#include "../../include/automatism/heater_orchestrator.h"

// IMailer ne fait qu'une déclaration anticipée de SensorReadings -> définition stub ici.
struct SensorReadings {};

namespace {

struct FakeActuators : public IActuators {
  bool heater = false, tank = false, aqua = false, light = false, seq = false;
  void startTankPump() override { tank = true; }
  void stopTankPump(uint32_t = 0) override { tank = false; }
  void startAquaPump() override { aqua = true; }
  void stopAquaPump() override { aqua = false; }
  void startHeater() override { heater = true; }
  void stopHeater() override { heater = false; }
  void startLight() override { light = true; }
  void stopLight() override { light = false; }
  void feedBigFish(uint16_t = 10) override {}
  void feedSmallFish(uint16_t = 10) override {}
  bool feedSequential(uint16_t = 10, uint16_t = 10, uint16_t = 2) override { return true; }
  void setServoGrosRest(int) override {}
  void setServoGrosFeed(int) override {}
  void setServoGrosInter(int) override {}
  void setServoPetitsRest(int) override {}
  void setServoPetitsFeed(int) override {}
  void setServoPetitsInter(int) override {}
  bool isSequentialFeedInProgress() const override { return seq; }
  bool isTankPumpRunning() const override { return tank; }
  bool isAquaPumpRunning() const override { return aqua; }
  bool isHeaterOn() const override { return heater; }
  bool isLightOn() const override { return light; }
};

struct FakeMailer : public IMailer {
  int alertCount = 0;
  char lastSubject[32] = {0};
  bool send(const char*, const char*, const char*, const char*) override { return true; }
  bool sendAlert(const char* subject, const char*, const char*, bool = false) override {
    ++alertCount;
    strncpy(lastSubject, subject, sizeof(lastSubject) - 1);
    return true;
  }
  bool sendSleepMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
  bool sendWakeMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
};

// Seuil 24 °C, hystérésis 2 °C (OFF au-dessus de 26 °C).
const float THR = 24.0f, HYST = 2.0f;

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Froid + chauffage éteint + mail activé -> relais ON, état=true, mail "Chauffage ON", blink (true).
void test_turn_on_sends_mail(void) {
  FakeActuators acts; FakeMailer mail; bool on = false;
  bool blink = HeaterOrchestrator::run(acts, mail, on, 22.0f, THR, HYST, /*mailEnabled=*/true, "a@b.c");
  TEST_ASSERT_TRUE(acts.heater);
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_EQUAL_INT(1, mail.alertCount);
  TEST_ASSERT_EQUAL_STRING("Chauffage ON", mail.lastSubject);
  TEST_ASSERT_TRUE(blink);
}

// Mail désactivé -> relais commute quand même, mais aucun mail ni blink.
void test_turn_on_no_mail_when_disabled(void) {
  FakeActuators acts; FakeMailer mail; bool on = false;
  bool blink = HeaterOrchestrator::run(acts, mail, on, 22.0f, THR, HYST, /*mailEnabled=*/false, "a@b.c");
  TEST_ASSERT_TRUE(acts.heater);   // relais commuté
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_EQUAL_INT(0, mail.alertCount);  // pas de mail
  TEST_ASSERT_FALSE(blink);
}

// Contrat Phase 3 (v15.27) : quand le serveur couvre les mails partagés, l'appelant
// passe mailEnabled=false. Le relais DOIT quand même s'allumer (froid) et s'éteindre
// (trop chaud) — sinon plus aucune régulation en liaison saine.
void test_phase3_server_covers_still_regulates_relay(void) {
  FakeActuators acts; FakeMailer mail; bool on = false;
  (void)HeaterOrchestrator::run(acts, mail, on, 22.0f, THR, HYST, /*mailEnabled=*/false, "a@b.c");
  TEST_ASSERT_TRUE(acts.heater);
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_EQUAL_INT(0, mail.alertCount);

  (void)HeaterOrchestrator::run(acts, mail, on, 27.0f, THR, HYST, /*mailEnabled=*/false, "a@b.c");
  TEST_ASSERT_FALSE(acts.heater);
  TEST_ASSERT_FALSE(on);
  TEST_ASSERT_EQUAL_INT(0, mail.alertCount);
}

// Trop chaud + chauffage allumé -> relais OFF, état=false, mail "Chauffage OFF".
void test_turn_off_sends_mail(void) {
  FakeActuators acts; acts.heater = true; FakeMailer mail; bool on = true;
  bool blink = HeaterOrchestrator::run(acts, mail, on, 27.0f, THR, HYST, true, "a@b.c");
  TEST_ASSERT_FALSE(acts.heater);
  TEST_ASSERT_FALSE(on);
  TEST_ASSERT_EQUAL_STRING("Chauffage OFF", mail.lastSubject);
  TEST_ASSERT_TRUE(blink);
}

// Bande morte -> aucun effet, aucun mail, état inchangé.
void test_deadband_noop(void) {
  FakeActuators acts; acts.heater = true; FakeMailer mail; bool on = true;
  bool blink = HeaterOrchestrator::run(acts, mail, on, 25.0f, THR, HYST, true, "a@b.c");
  TEST_ASSERT_TRUE(acts.heater);   // inchangé
  TEST_ASSERT_TRUE(on);
  TEST_ASSERT_EQUAL_INT(0, mail.alertCount);
  TEST_ASSERT_FALSE(blink);
}

// Déjà dans le bon état (froid mais déjà allumé) -> pas de re-déclenchement.
void test_no_retrigger_when_already_on(void) {
  FakeActuators acts; acts.heater = true; FakeMailer mail; bool on = true;
  bool blink = HeaterOrchestrator::run(acts, mail, on, 22.0f, THR, HYST, true, "a@b.c");
  TEST_ASSERT_EQUAL_INT(0, mail.alertCount);
  TEST_ASSERT_FALSE(blink);
  TEST_ASSERT_TRUE(on);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_turn_on_sends_mail);
  RUN_TEST(test_turn_on_no_mail_when_disabled);
  RUN_TEST(test_phase3_server_covers_still_regulates_relay);
  RUN_TEST(test_turn_off_sends_mail);
  RUN_TEST(test_deadband_noop);
  RUN_TEST(test_no_retrigger_when_already_on);
  return UNITY_END();
}
