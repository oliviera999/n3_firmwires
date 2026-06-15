#include <unity.h>

// Tests de l'interface IMailer (imailer.h), introduite par le chantier C4 (inversion de
// dépendance god-class -> messagerie). On vérifie qu'un FAUX mailer peut implémenter
// l'interface et être piloté via une référence `IMailer&` (dispatch virtuel + arguments
// par défaut). Ce `FakeMailer` est l'infrastructure de test réutilisable pour les futures
// extractions d'orchestration (alertes, fin de nourrissage, mails de veille).
#include "../../include/imailer.h"

// IMailer ne fait qu'une déclaration anticipée de SensorReadings ; on en fournit une
// définition stub (le faux mailer ignore son contenu) pour rester 100% natif (pas d'Arduino).
struct SensorReadings {};

namespace {

struct FakeMailer : public IMailer {
  int sendCount = 0, alertCount = 0, sleepCount = 0, wakeCount = 0;
  bool lastDetailed = false;
  const char* lastSubject = nullptr;

  bool send(const char* subject, const char* /*message*/, const char* /*toName*/,
            const char* /*toEmail*/) override {
    ++sendCount; lastSubject = subject; return true;
  }
  bool sendAlert(const char* subject, const char* /*message*/, const char* /*toEmail*/,
                 bool includeDetailedReport = false) override {
    ++alertCount; lastSubject = subject; lastDetailed = includeDetailedReport; return true;
  }
  bool sendSleepMail(const char* /*reason*/, uint32_t /*dur*/, const SensorReadings& /*r*/,
                     const char* /*toEmail*/) override { ++sleepCount; return true; }
  bool sendWakeMail(const char* /*reason*/, uint32_t /*dur*/, const SensorReadings& /*r*/,
                    const char* /*toEmail*/) override { ++wakeCount; return true; }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Le dispatch virtuel via IMailer& atteint l'implémentation dérivée.
void test_dispatch_via_interface(void) {
  FakeMailer fake;
  IMailer& m = fake;
  TEST_ASSERT_TRUE(m.send("Sujet", "msg", "System", "a@b.c"));
  TEST_ASSERT_EQUAL_INT(1, fake.sendCount);
  TEST_ASSERT_EQUAL_STRING("Sujet", fake.lastSubject);
}

// sendAlert : argument par défaut includeDetailedReport=false résolu via l'interface.
void test_alert_default_detailed_false(void) {
  FakeMailer fake;
  IMailer& m = fake;
  m.sendAlert("Alerte", "msg", "a@b.c");  // 3 args -> défaut false
  TEST_ASSERT_EQUAL_INT(1, fake.alertCount);
  TEST_ASSERT_FALSE(fake.lastDetailed);
  m.sendAlert("Boot", "msg", "a@b.c", /*includeDetailedReport=*/true);
  TEST_ASSERT_TRUE(fake.lastDetailed);
}

// sendSleepMail / sendWakeMail dispatchés via l'interface.
void test_sleep_wake_dispatch(void) {
  FakeMailer fake;
  IMailer& m = fake;
  SensorReadings r;
  m.sendSleepMail("inactivité", 600, r, "a@b.c");
  m.sendWakeMail("réveil", 590, r, "a@b.c");
  TEST_ASSERT_EQUAL_INT(1, fake.sleepCount);
  TEST_ASSERT_EQUAL_INT(1, fake.wakeCount);
}

// Destruction polymorphe propre via pointeur d'interface (dtor virtuel).
void test_virtual_destruction(void) {
  IMailer* m = new FakeMailer();
  m->send("s", "m", "n", "e");
  delete m;  // ne doit pas fuir / planter
  TEST_PASS();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_dispatch_via_interface);
  RUN_TEST(test_alert_default_detailed_false);
  RUN_TEST(test_sleep_wake_dispatch);
  RUN_TEST(test_virtual_destruction);
  return UNITY_END();
}
