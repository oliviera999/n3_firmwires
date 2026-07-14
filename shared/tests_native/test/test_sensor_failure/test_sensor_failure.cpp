#include <Arduino.h>  // mock (millis injectable via n3MockMillisSet)
#include <unity.h>

// Tests de SensorFailureManager (n3_sensor_failure_manager.h), mutualisé depuis
// ffp5cs (logique identique). Cette machine d'état inter-lectures n'était
// couverte par AUCUN test auparavant : on verrouille ici les invariants —
// désactivation après N échecs consécutifs, cadence des tests de réactivation
// (millis injecté), réactivation après M succès, resets.

#include "n3_sensor_failure_manager.h"

void setUp(void) { n3MockMillisSet(0); }
void tearDown(void) {}

// ---- Désactivation après N échecs consécutifs ----

void test_active_par_defaut(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  TEST_ASSERT_FALSE(m.isDisabled());
  TEST_ASSERT_EQUAL_UINT8(0, m.getConsecutiveFailures());
}

void test_disable_apres_seuil_echecs(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure();
  m.recordFailure();
  TEST_ASSERT_FALSE(m.isDisabled());  // 2 < 3
  m.recordFailure();                  // 3e échec -> désactivé
  TEST_ASSERT_TRUE(m.isDisabled());
  TEST_ASSERT_EQUAL_UINT8(3, m.getConsecutiveFailures());
}

void test_succes_reset_compteur_echecs(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure();
  m.recordFailure();
  m.recordSuccess();  // série cassée
  TEST_ASSERT_EQUAL_UINT8(0, m.getConsecutiveFailures());
  m.recordFailure();
  m.recordFailure();
  TEST_ASSERT_FALSE(m.isDisabled());  // il refaut 3 échecs consécutifs
}

// ---- Cadence des tests de réactivation (millis injecté) ----

void test_pas_de_test_reactivation_si_actif(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  n3MockMillisSet(100000);
  TEST_ASSERT_FALSE(m.shouldTestReactivation());
}

void test_reactivation_cadencee_par_intervalle(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();  // désactivé (t=0)
  TEST_ASSERT_TRUE(m.isDisabled());

  // _lastReactivationTestMs = 0 : à t=0, (0-0) >= 1000 est faux -> pas encore dû...
  // sauf que 0-0=0 < 1000 -> false. À t=999 -> false ; à t=1000 -> true.
  n3MockMillisSet(999);
  TEST_ASSERT_FALSE(m.shouldTestReactivation());
  n3MockMillisSet(1000);
  TEST_ASSERT_TRUE(m.shouldTestReactivation());
}

void test_echec_reactivation_rearme_le_delai(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(1000);
  TEST_ASSERT_TRUE(m.shouldTestReactivation());
  m.recordReactivationTestFailure();  // horodate le test à t=1000
  n3MockMillisSet(1500);
  TEST_ASSERT_FALSE(m.shouldTestReactivation());  // 500 < 1000
  n3MockMillisSet(2000);
  TEST_ASSERT_TRUE(m.shouldTestReactivation());
}

void test_wraparound_millis_autorise_le_test(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(4294967000UL);
  m.recordReactivationTestFailure();  // _lastReactivationTestMs proche du max
  n3MockMillisSet(100);               // wrap : now < last -> test autorisé
  TEST_ASSERT_TRUE(m.shouldTestReactivation());
}

// ---- Réactivation après M succès consécutifs ----

void test_reactivation_apres_seuil_succes(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  TEST_ASSERT_TRUE(m.isDisabled());

  n3MockMillisSet(1000);
  TEST_ASSERT_FALSE(m.recordReactivationTestSuccess());  // 1/2
  TEST_ASSERT_TRUE(m.isDisabled());
  TEST_ASSERT_EQUAL_UINT8(1, m.getReactivationSuccesses());
  TEST_ASSERT_TRUE(m.recordReactivationTestSuccess());   // 2/2 -> réactivé
  TEST_ASSERT_FALSE(m.isDisabled());
  TEST_ASSERT_EQUAL_UINT8(0, m.getConsecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(0, m.getReactivationSuccesses());
}

void test_echec_reactivation_reset_succes(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(1000);
  TEST_ASSERT_FALSE(m.recordReactivationTestSuccess());  // 1/2
  m.recordReactivationTestFailure();                     // série cassée
  TEST_ASSERT_EQUAL_UINT8(0, m.getReactivationSuccesses());
  TEST_ASSERT_FALSE(m.recordReactivationTestSuccess());  // repart à 1/2
  TEST_ASSERT_TRUE(m.isDisabled());
}

void test_redisable_apres_reactivation(void) {
  // Après réactivation, la machine repart proprement : N nouveaux échecs
  // consécutifs re-désactivent (le latch _disableLogged est bien ré-armé).
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(1000);
  m.recordReactivationTestSuccess();
  m.recordReactivationTestSuccess();  // réactivé
  TEST_ASSERT_FALSE(m.isDisabled());
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  TEST_ASSERT_TRUE(m.isDisabled());
}

void test_reset_remet_tout_a_zero(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(1000);
  m.recordReactivationTestSuccess();
  m.reset();
  TEST_ASSERT_FALSE(m.isDisabled());
  TEST_ASSERT_EQUAL_UINT8(0, m.getConsecutiveFailures());
  TEST_ASSERT_EQUAL_UINT8(0, m.getReactivationSuccesses());
  // _lastReactivationTestMs remis à 0 : à t=1000, un capteur re-désactivé
  // est immédiatement testable.
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  TEST_ASSERT_TRUE(m.shouldTestReactivation());
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_active_par_defaut);
  RUN_TEST(test_disable_apres_seuil_echecs);
  RUN_TEST(test_succes_reset_compteur_echecs);
  RUN_TEST(test_pas_de_test_reactivation_si_actif);
  RUN_TEST(test_reactivation_cadencee_par_intervalle);
  RUN_TEST(test_echec_reactivation_rearme_le_delai);
  RUN_TEST(test_wraparound_millis_autorise_le_test);
  RUN_TEST(test_reactivation_apres_seuil_succes);
  RUN_TEST(test_echec_reactivation_reset_succes);
  RUN_TEST(test_redisable_apres_reactivation);
  RUN_TEST(test_reset_remet_tout_a_zero);
  return UNITY_END();
}
