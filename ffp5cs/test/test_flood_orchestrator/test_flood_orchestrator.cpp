#include <unity.h>
#include <cstring>

// Tests de FloodOrchestrator (automatism/flood_orchestrator.h) — orchestrateur « aquarium
// trop plein » testé nativement via FakeMailer (chantier C4). Vérifie le câblage complet :
// décision FloodAlert -> envoi mail -> markEmailSent -> Outcome (NVS/blink/flag côté appelant),
// + parité du message (format historique + « / Pompe verrouillée ») + cas échec d'envoi.
#include "../../include/automatism/flood_orchestrator.h"

struct SensorReadings {};  // stub (IMailer fwd-decl)

namespace {
struct FakeMailer : public IMailer {
  int alertCount = 0;
  bool failNext = false;  // pour simuler un envoi refusé
  char lastSubject[40] = {0};
  char lastMsg[128] = {0};
  // Capture de la plomberie d'accusé de livraison différé (Phase 0).
  bool* lastAckFlag = nullptr;
  bool lastAckFailValue = true;
  bool send(const char*, const char*, const char*, const char*) override { return true; }
  bool sendAlert(const char* subject, const char* message, const char*, bool = false) override {
    ++alertCount;
    strncpy(lastSubject, subject, sizeof(lastSubject) - 1);
    strncpy(lastMsg, message, sizeof(lastMsg) - 1);
    return !failNext;
  }
  bool sendAlertAcked(const char* subject, const char* message, const char* toEmail,
                      bool includeDetailedReport, bool* ackFlag, bool ackFailValue) override {
    lastAckFlag = ackFlag;
    lastAckFailValue = ackFailValue;
    return sendAlert(subject, message, toEmail, includeDetailedReport);
  }
  bool sendSleepMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
  bool sendWakeMail(const char*, uint32_t, const SensorReadings&, const char*) override { return true; }
};

// Params représentatifs : limFlood 80 mm, reset 100 mm, debounce 120 s, cooldown 600 s, stable 180 s.
FloodAlert::Params makeParams() {
  FloodAlert::Params p;
  p.limFloodMm = 80; p.resetThresholdMm = 100;
  p.debounceSec = 120; p.cooldownSec = 600; p.resetStableSec = 180;
  return p;
}
}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Trop plein, debounce atteint, cooldown OK -> EmailQueued + markEmailSent + message correct.
void test_email_queued(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = false; st.floodEnterSinceEpoch = 1000; st.lastFloodEmailEpoch = 0;
  auto out = FloodOrchestrator::run(m, st, p, /*wlAqua=*/50, /*now=*/1120, /*mail=*/true, "e", /*pumpLocked=*/false);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::EmailQueued, out);
  TEST_ASSERT_EQUAL_STRING("Alerte - Aquarium TROP PLEIN", m.lastSubject);
  TEST_ASSERT_EQUAL_STRING("Distance: 50.0 mm ( mm (< 80.0)", m.lastMsg);
  TEST_ASSERT_TRUE(st.inFlood);                       // markEmailSent appliqué
  TEST_ASSERT_EQUAL_UINT32(1120, st.lastFloodEmailEpoch);
}

// Pompe verrouillée -> suffixe « / Pompe verrouillée » ajouté au message.
void test_pump_locked_appends_suffix(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.floodEnterSinceEpoch = 1000;
  FloodOrchestrator::run(m, st, p, 50, 1120, true, "e", /*pumpLocked=*/true);
  TEST_ASSERT_EQUAL_STRING("Distance: 50.0 mm ( mm (< 80.0) / Pompe verrouillée", m.lastMsg);
}

// Envoi refusé par le mailer -> EmailFailed, et PAS de markEmailSent (état inchangé).
void test_email_failed(void) {
  FakeMailer m; m.failNext = true; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = false; st.floodEnterSinceEpoch = 1000; st.lastFloodEmailEpoch = 0;
  auto out = FloodOrchestrator::run(m, st, p, 50, 1120, true, "e", false);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::EmailFailed, out);
  TEST_ASSERT_FALSE(st.inFlood);                      // markEmailSent NON appliqué
  TEST_ASSERT_EQUAL_UINT32(0, st.lastFloodEmailEpoch);
}

// Niveau revenu OK et stable -> ExitedFlood, aucun mail.
void test_exited_flood(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = true; st.aboveResetSinceEpoch = 2000;
  auto out = FloodOrchestrator::run(m, st, p, /*wlAqua=*/120, /*now=*/2180, true, "e", false);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::ExitedFlood, out);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
  TEST_ASSERT_FALSE(st.inFlood);
}

// Debounce non atteint -> None, aucun mail.
void test_none_before_debounce(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = false; st.floodEnterSinceEpoch = 1000; st.lastFloodEmailEpoch = 0;
  auto out = FloodOrchestrator::run(m, st, p, 50, /*now=*/1100, true, "e", false);  // 100 s < 120
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::None, out);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
}

// Mail désactivé -> pas d'envoi même si trop plein.
void test_none_when_mail_disabled(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.floodEnterSinceEpoch = 1000;
  auto out = FloodOrchestrator::run(m, st, p, 50, 1120, /*mail=*/false, "e", false);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::None, out);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
}

// Contrat Phase 3 (v15.27) : mailEnabled=false (serveur couvre / grâce boot) ne doit
// PAS figer inFlood. ExitFlood clear toujours l'état — sinon RefillOverfill refuse Unlock.
void test_phase3_mail_gate_still_exits_flood(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = true; st.aboveResetSinceEpoch = 2000;
  auto out = FloodOrchestrator::run(m, st, p, /*wlAqua=*/120, /*now=*/2180,
                                    /*mail=*/false, "e", false);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::ExitedFlood, out);
  TEST_ASSERT_FALSE(st.inFlood);
  TEST_ASSERT_EQUAL_INT(0, m.alertCount);
}

// Phase 0 : le flag durable `inFlood` de l'appelant est transmis comme accusé de
// livraison différé (restauré à false si la livraison SMTP échoue définitivement).
void test_ack_plumbing_persistent_inflood(void) {
  FakeMailer m; FloodAlert::Params p = makeParams();
  FloodAlert::State st; st.inFlood = false; st.floodEnterSinceEpoch = 1000; st.lastFloodEmailEpoch = 0;
  bool persistentInFlood = false;
  auto out = FloodOrchestrator::run(m, st, p, 50, 1120, true, "e", false, &persistentInFlood);
  TEST_ASSERT_EQUAL(FloodOrchestrator::Outcome::EmailQueued, out);
  TEST_ASSERT_EQUAL_PTR(&persistentInFlood, m.lastAckFlag);
  TEST_ASSERT_FALSE(m.lastAckFailValue);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_email_queued);
  RUN_TEST(test_pump_locked_appends_suffix);
  RUN_TEST(test_email_failed);
  RUN_TEST(test_exited_flood);
  RUN_TEST(test_none_before_debounce);
  RUN_TEST(test_none_when_mail_disabled);
  RUN_TEST(test_phase3_mail_gate_still_exits_flood);
  RUN_TEST(test_ack_plumbing_persistent_inflood);
  return UNITY_END();
}
