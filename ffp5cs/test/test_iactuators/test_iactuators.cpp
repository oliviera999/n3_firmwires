#include <unity.h>

// Tests de l'interface IActuators (iactuators.h), introduite par le chantier C4
// (inversion de dépendance god-class -> matériel). On vérifie ici qu'un FAUX acteur
// peut implémenter l'interface et être piloté via une référence `IActuators&`
// (dispatch virtuel + arguments par défaut). Ce `FakeActuators` est l'infrastructure
// de test réutilisable pour les prochaines extractions de contrôleurs.
#include "../../include/iactuators.h"

namespace {

// Faux acteur : enregistre l'état et les derniers arguments, sans aucun matériel.
struct FakeActuators : public IActuators {
  bool tankPump = false, aquaPump = false, heater = false, light = false, seqFeed = false;
  uint16_t lastBigDur = 0, lastSmallDur = 0;
  uint32_t lastStopArg = 0;
  int feedSeqCalls = 0;

  void startTankPump() override { tankPump = true; }
  void stopTankPump(uint32_t startTime = 0) override { tankPump = false; lastStopArg = startTime; }
  void startAquaPump() override { aquaPump = true; }
  void stopAquaPump() override { aquaPump = false; }
  void startHeater() override { heater = true; }
  void stopHeater() override { heater = false; }
  void startLight() override { light = true; }
  void stopLight() override { light = false; }
  void feedBigFish(uint16_t durationSec = 10) override { lastBigDur = durationSec; }
  void feedSmallFish(uint16_t durationSec = 10) override { lastSmallDur = durationSec; }
  bool feedSequential(uint16_t bigDurationSec = 10, uint16_t smallDurationSec = 10,
                      uint16_t /*delayBetweenSec*/ = 2) override {
    ++feedSeqCalls; lastBigDur = bigDurationSec; lastSmallDur = smallDurationSec;
    seqFeed = true; return true;
  }
  bool isSequentialFeedInProgress() const override { return seqFeed; }
  bool isTankPumpRunning() const override { return tankPump; }
  bool isAquaPumpRunning() const override { return aquaPump; }
  bool isHeaterOn() const override { return heater; }
  bool isLightOn() const override { return light; }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Le dispatch virtuel via IActuators& atteint bien l'implémentation dérivée.
void test_polymorphic_dispatch_via_interface(void) {
  FakeActuators fake;
  IActuators& a = fake;  // upcast vers l'interface
  a.startHeater();
  TEST_ASSERT_TRUE(a.isHeaterOn());
  TEST_ASSERT_TRUE(fake.heater);
  a.stopHeater();
  TEST_ASSERT_FALSE(a.isHeaterOn());
}

// Les arguments par défaut sont résolus via l'interface (type statique IActuators).
void test_default_args_via_interface(void) {
  FakeActuators fake;
  IActuators& a = fake;
  a.stopTankPump();  // défaut = 0
  TEST_ASSERT_EQUAL_UINT32(0, fake.lastStopArg);
  a.stopTankPump(12345);
  TEST_ASSERT_EQUAL_UINT32(12345, fake.lastStopArg);
  a.feedBigFish();  // défaut = 10
  TEST_ASSERT_EQUAL_UINT16(10, fake.lastBigDur);
}

// Chaque start/stop bascule l'état lu par le getter correspondant.
void test_each_actuator_toggles(void) {
  FakeActuators fake;
  IActuators& a = fake;
  a.startTankPump(); TEST_ASSERT_TRUE(a.isTankPumpRunning());
  a.stopTankPump();  TEST_ASSERT_FALSE(a.isTankPumpRunning());
  a.startAquaPump(); TEST_ASSERT_TRUE(a.isAquaPumpRunning());
  a.stopAquaPump();  TEST_ASSERT_FALSE(a.isAquaPumpRunning());
  a.startLight();    TEST_ASSERT_TRUE(a.isLightOn());
  a.stopLight();     TEST_ASSERT_FALSE(a.isLightOn());
}

// feedSequential : retour, état "en cours", et capture des durées.
void test_feed_sequential(void) {
  FakeActuators fake;
  IActuators& a = fake;
  TEST_ASSERT_FALSE(a.isSequentialFeedInProgress());
  bool ok = a.feedSequential(7, 3);
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_TRUE(a.isSequentialFeedInProgress());
  TEST_ASSERT_EQUAL_UINT16(7, fake.lastBigDur);
  TEST_ASSERT_EQUAL_UINT16(3, fake.lastSmallDur);
  TEST_ASSERT_EQUAL_INT(1, fake.feedSeqCalls);
}

// Destruction polymorphe propre via pointeur d'interface (dtor virtuel).
void test_virtual_destruction(void) {
  IActuators* a = new FakeActuators();
  a->startLight();
  TEST_ASSERT_TRUE(a->isLightOn());
  delete a;  // ne doit pas fuir / planter (dtor virtuel)
  TEST_PASS();
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_polymorphic_dispatch_via_interface);
  RUN_TEST(test_default_args_via_interface);
  RUN_TEST(test_each_actuator_toggles);
  RUN_TEST(test_feed_sequential);
  RUN_TEST(test_virtual_destruction);
  return UNITY_END();
}
