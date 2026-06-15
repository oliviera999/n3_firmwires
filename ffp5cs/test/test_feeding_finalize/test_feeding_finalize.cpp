#include <unity.h>
#include <cstring>

// Tests de FeedingFinalizeOrchestrator (automatism/feeding_finalize_orchestrator.h) —
// orchestrateur de fin de cycle nourrissage testé nativement via un FakeStatusPublisher
// (chantier C4). Vérifie le câblage complet de Automatism::finalizeFeedingIfNeeded :
//   - hors-ligne -> aucune publication, Outcome::Offline ;
//   - en ligne + AUTO   -> publie l'évènement nourrissage (paires « 108/109=1 ») ;
//   - en ligne + MANUEL -> publie la réinitialisation (paires « 108/109=0 ») ;
//   - sync OK vs échec -> Outcome distinct (qui pilote ensuite GPIOParser + Serial côté appelant).
// Parité des chaînes extraPairs vérifiée à l'identique (historique côté BDD serveur).
#include "../../include/automatism/feeding_finalize_orchestrator.h"

struct SensorReadings {};  // stub (IStatusPublisher fwd-decl)

namespace {
struct FakeStatusPublisher : public IStatusPublisher {
  int publishCount = 0;
  bool failNext = false;            // pour simuler un sync distant refusé
  bool lastExtraWasNull = false;
  char lastExtraPairs[64] = {0};
  bool publish(const SensorReadings&, const char* extraPairs) override {
    ++publishCount;
    if (extraPairs) {
      strncpy(lastExtraPairs, extraPairs, sizeof(lastExtraPairs) - 1);
      lastExtraWasNull = false;
    } else {
      lastExtraPairs[0] = '\0';
      lastExtraWasNull = true;
    }
    return !failNext;
  }
};
}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Hors-ligne (cycle auto) : aucune publication, Outcome::Offline.
void test_offline_auto_no_publish(void) {
  FakeStatusPublisher pub; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/false, /*wasAutomatic=*/true, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::Offline, out);
  TEST_ASSERT_EQUAL_INT(0, pub.publishCount);
}

// Hors-ligne (cycle manuel) : idem, aucune publication.
void test_offline_manual_no_publish(void) {
  FakeStatusPublisher pub; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/false, /*wasAutomatic=*/false, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::Offline, out);
  TEST_ASSERT_EQUAL_INT(0, pub.publishCount);
}

// En ligne + AUTO + sync OK : publie les paires « gros+petits », Outcome::AutoSynced.
void test_auto_synced(void) {
  FakeStatusPublisher pub; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/true, /*wasAutomatic=*/true, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::AutoSynced, out);
  TEST_ASSERT_EQUAL_INT(1, pub.publishCount);
  TEST_ASSERT_EQUAL_STRING("bouffePetits=1&108=1&bouffeGros=1&109=1", pub.lastExtraPairs);
}

// En ligne + AUTO + sync refusé : mêmes paires publiées, Outcome::AutoSyncFailed.
void test_auto_sync_failed(void) {
  FakeStatusPublisher pub; pub.failNext = true; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/true, /*wasAutomatic=*/true, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::AutoSyncFailed, out);
  TEST_ASSERT_EQUAL_INT(1, pub.publishCount);
  TEST_ASSERT_EQUAL_STRING("bouffePetits=1&108=1&bouffeGros=1&109=1", pub.lastExtraPairs);
}

// En ligne + MANUEL + sync OK : publie les paires de réinitialisation, Outcome::ManualSynced.
void test_manual_synced(void) {
  FakeStatusPublisher pub; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/true, /*wasAutomatic=*/false, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::ManualSynced, out);
  TEST_ASSERT_EQUAL_INT(1, pub.publishCount);
  TEST_ASSERT_EQUAL_STRING("bouffePetits=0&108=0&bouffeGros=0&109=0", pub.lastExtraPairs);
}

// En ligne + MANUEL + sync refusé : mêmes paires, Outcome::ManualSyncFailed.
void test_manual_sync_failed(void) {
  FakeStatusPublisher pub; pub.failNext = true; SensorReadings r;
  auto out = FeedingFinalizeOrchestrator::run(/*connected=*/true, /*wasAutomatic=*/false, r, pub);
  TEST_ASSERT_EQUAL(FeedingFinalizeOrchestrator::Outcome::ManualSyncFailed, out);
  TEST_ASSERT_EQUAL_INT(1, pub.publishCount);
  TEST_ASSERT_EQUAL_STRING("bouffePetits=0&108=0&bouffeGros=0&109=0", pub.lastExtraPairs);
}

// Parité des constantes extraPairs (chaînes historiques poussées côté BDD serveur).
void test_extra_pairs_constants(void) {
  TEST_ASSERT_EQUAL_STRING("bouffePetits=1&108=1&bouffeGros=1&109=1",
                           FeedingFinalizeOrchestrator::kAutoExtraPairs);
  TEST_ASSERT_EQUAL_STRING("bouffePetits=0&108=0&bouffeGros=0&109=0",
                           FeedingFinalizeOrchestrator::kManualExtraPairs);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_offline_auto_no_publish);
  RUN_TEST(test_offline_manual_no_publish);
  RUN_TEST(test_auto_synced);
  RUN_TEST(test_auto_sync_failed);
  RUN_TEST(test_manual_synced);
  RUN_TEST(test_manual_sync_failed);
  RUN_TEST(test_extra_pairs_constants);
  return UNITY_END();
}
