#include <unity.h>

// Tests de ActuatorSnapshot (automatism/actuator_snapshot.h), extrait de
// Automatism::prepareActuatorsForSleep / restoreActuatorsAfterWake (chantier C4).
// Premier bout de logique d'actionneur testé nativement grâce à IActuators :
// un faux acteur remplace le matériel. Un bug ici = mauvais état restauré au réveil
// (chauffage/pompe/lumière qui ne repartent pas, ou repartent à tort).
#include "../../include/automatism/actuator_snapshot.h"

namespace {

// Faux acteur minimal : suit l'état des 3 actionneurs concernés.
struct FakeActuators : public IActuators {
  bool tankPump = false, aquaPump = false, heater = false, light = false, seqFeed = false;
  void startTankPump() override { tankPump = true; }
  void stopTankPump(uint32_t = 0) override { tankPump = false; }
  void startAquaPump() override { aquaPump = true; }
  void stopAquaPump() override { aquaPump = false; }
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
  bool isSequentialFeedInProgress() const override { return seqFeed; }
  bool isTankPumpRunning() const override { return tankPump; }
  bool isAquaPumpRunning() const override { return aquaPump; }
  bool isHeaterOn() const override { return heater; }
  bool isLightOn() const override { return light; }
};

}  // namespace

void setUp(void) {}
void tearDown(void) {}

// capture() reflète l'état courant des 3 actionneurs.
void test_capture_reads_current_state(void) {
  FakeActuators fake;
  fake.aquaPump = true;
  fake.light = true;  // heater reste off
  ActuatorSnapshot::State s = ActuatorSnapshot::capture(fake);
  TEST_ASSERT_TRUE(s.aqua);
  TEST_ASSERT_FALSE(s.heater);
  TEST_ASSERT_TRUE(s.light);
}

// capture() n'a pas d'effet de bord (lecture seule).
void test_capture_is_read_only(void) {
  FakeActuators fake;
  fake.aquaPump = true; fake.heater = true; fake.light = true;
  ActuatorSnapshot::capture(fake);
  TEST_ASSERT_TRUE(fake.aquaPump);
  TEST_ASSERT_TRUE(fake.heater);
  TEST_ASSERT_TRUE(fake.light);
}

// stopAll() coupe les 3 actionneurs concernés (pas la pompe réservoir).
void test_stop_all_cuts_three(void) {
  FakeActuators fake;
  fake.aquaPump = true; fake.heater = true; fake.light = true; fake.tankPump = true;
  ActuatorSnapshot::stopAll(fake);
  TEST_ASSERT_FALSE(fake.aquaPump);
  TEST_ASSERT_FALSE(fake.heater);
  TEST_ASSERT_FALSE(fake.light);
  TEST_ASSERT_TRUE(fake.tankPump);  // non touchée par le snapshot veille
}

// apply() ne rallume que les actionneurs marqués actifs.
void test_apply_restores_only_active(void) {
  FakeActuators fake;  // tout éteint
  ActuatorSnapshot::State snap;
  snap.aqua = true; snap.heater = false; snap.light = true;
  ActuatorSnapshot::apply(fake, snap);
  TEST_ASSERT_TRUE(fake.aquaPump);
  TEST_ASSERT_FALSE(fake.heater);
  TEST_ASSERT_TRUE(fake.light);
}

// apply() d'un snapshot vide ne rallume rien.
void test_apply_empty_snapshot_noop(void) {
  FakeActuators fake;
  ActuatorSnapshot::State snap;  // tout false
  ActuatorSnapshot::apply(fake, snap);
  TEST_ASSERT_FALSE(fake.aquaPump);
  TEST_ASSERT_FALSE(fake.heater);
  TEST_ASSERT_FALSE(fake.light);
}

// Cycle veille -> réveil : capture, coupe, puis restaure l'état initial.
void test_sleep_wake_roundtrip(void) {
  FakeActuators fake;
  fake.aquaPump = true; fake.heater = false; fake.light = true;
  // Veille : on mémorise puis on coupe tout.
  ActuatorSnapshot::State snap = ActuatorSnapshot::capture(fake);
  ActuatorSnapshot::stopAll(fake);
  TEST_ASSERT_FALSE(fake.aquaPump);
  TEST_ASSERT_FALSE(fake.light);
  // Réveil : on restaure l'état mémorisé.
  ActuatorSnapshot::apply(fake, snap);
  TEST_ASSERT_TRUE(fake.aquaPump);
  TEST_ASSERT_FALSE(fake.heater);
  TEST_ASSERT_TRUE(fake.light);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_capture_reads_current_state);
  RUN_TEST(test_capture_is_read_only);
  RUN_TEST(test_stop_all_cuts_three);
  RUN_TEST(test_apply_restores_only_active);
  RUN_TEST(test_apply_empty_snapshot_noop);
  RUN_TEST(test_sleep_wake_roundtrip);
  return UNITY_END();
}
