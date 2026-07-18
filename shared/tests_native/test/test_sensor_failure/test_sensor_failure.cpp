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

// =============================================================================
// Persistance deep-sleep (T2 adoption n3pp/msp, lib v1.2.0)
// -----------------------------------------------------------------------------
// La cohorte n3pp/msp deep-sleepe entre chaque lecture : setup() recrée le
// manager, la RAM repart à 0, et sans persistance les échecs ne s'accumulent
// jamais. On verrouille ici la sérialisation POD round-trip, la désactivation
// à travers des cycles simulés, et la cadence de réactivation par cycles de
// réveil (millis() inutilisable à travers le sommeil).
// =============================================================================

// Simule un réveil deep-sleep : le manager est recréé (RAM à 0) puis restauré
// depuis le POD RTC. millis() est aussi remis à 0 (reset RAM du timer).
static SensorFailureManager makeWokenManager(const SensorFailureState& persisted) {
  n3MockMillisSet(0);
  SensorFailureManager m("T", 3, 60000, 2);
  m.restoreState(persisted);
  return m;
}

void test_serialize_restore_roundtrip(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure();
  m.recordFailure();  // 2 échecs, encore actif
  SensorFailureState s = m.serializeState();
  TEST_ASSERT_EQUAL_UINT8(2, s.consecutiveFailures);
  TEST_ASSERT_EQUAL_UINT8(0, s.disabled);

  // Nouveau manager (RAM neuve) restauré depuis le POD : état identique.
  SensorFailureManager m2("T", 3, 1000, 2);
  m2.restoreState(s);
  TEST_ASSERT_EQUAL_UINT8(2, m2.getConsecutiveFailures());
  TEST_ASSERT_FALSE(m2.isDisabled());
}

void test_disable_a_travers_cycles_deep_sleep(void) {
  // 3 échecs répartis sur 3 réveils distincts (manager recréé à chaque cycle) :
  // sans persistance, chaque cycle repartirait de 0 et ne désactiverait jamais.
  SensorFailureState st;  // cold boot = tout à 0
  for (int cycle = 0; cycle < 3; ++cycle) {
    SensorFailureManager m = makeWokenManager(st);
    m.noteWakeCycle();      // un réveil de plus (no-op tant qu'actif)
    TEST_ASSERT_FALSE(m.isDisabled());
    m.recordFailure();      // un échec par réveil
    st = m.serializeState();  // persistance avant "sommeil"
  }
  // Après 3 réveils en échec (seuil=3), l'état persisté est désactivé.
  TEST_ASSERT_EQUAL_UINT8(1, st.disabled);
  SensorFailureManager mFinal = makeWokenManager(st);
  TEST_ASSERT_TRUE(mFinal.isDisabled());
}

void test_reactivation_cyclique_pas_de_test_si_actif(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic());
  m.noteWakeCycle();  // no-op : capteur actif
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic());
}

void test_reactivation_cyclique_teste_chaque_reveil(void) {
  // Désactivé, cadence par défaut (everyNWakes=1) : testable dès le réveil
  // suivant (un noteWakeCycle écoulé), sans dépendre de millis().
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();  // désactivé
  TEST_ASSERT_TRUE(m.isDisabled());
  // Fraîchement désactivé, 0 cycle écoulé -> pas encore dû.
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic());
  m.noteWakeCycle();  // 1 réveil écoulé
  TEST_ASSERT_TRUE(m.shouldTestReactivationCyclic());
}

void test_reactivation_cyclique_intervalle_n_reveils(void) {
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  // Cadence tous les 3 réveils.
  m.noteWakeCycle();
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic(3));  // 1 < 3
  m.noteWakeCycle();
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic(3));  // 2 < 3
  m.noteWakeCycle();
  TEST_ASSERT_TRUE(m.shouldTestReactivationCyclic(3));   // 3 >= 3
  // Un test consommé remet le compteur de cycles à 0.
  m.recordReactivationTestFailure();
  TEST_ASSERT_EQUAL_UINT16(0, m.getCyclesSinceReactivationTest());
  TEST_ASSERT_FALSE(m.shouldTestReactivationCyclic(3));
}

void test_reactivation_cyclique_a_travers_cycles(void) {
  // Réactivation complète simulée à travers le deep sleep : capteur désactivé,
  // puis K=2 succès de test sur des réveils successifs -> réactivé, persisté.
  SensorFailureState st;
  {
    SensorFailureManager m = makeWokenManager(st);
    m.recordFailure(); m.recordFailure(); m.recordFailure();  // désactivé
    st = m.serializeState();
  }
  TEST_ASSERT_EQUAL_UINT8(1, st.disabled);

  bool reactivated = false;
  for (int cycle = 0; cycle < 2 && !reactivated; ++cycle) {
    SensorFailureManager m = makeWokenManager(st);
    m.noteWakeCycle();
    TEST_ASSERT_TRUE(m.shouldTestReactivationCyclic());
    reactivated = m.recordReactivationTestSuccess();  // test OK ce réveil
    st = m.serializeState();
  }
  TEST_ASSERT_TRUE(reactivated);
  TEST_ASSERT_EQUAL_UINT8(0, st.disabled);
  SensorFailureManager mFinal = makeWokenManager(st);
  TEST_ASSERT_FALSE(mFinal.isDisabled());
}

void test_restore_remet_le_millis_a_zero(void) {
  // restoreState ne doit pas hériter d'un _lastReactivationTestMs stale.
  SensorFailureManager m("T", 3, 1000, 2);
  m.recordFailure(); m.recordFailure(); m.recordFailure();
  n3MockMillisSet(50000);
  m.recordReactivationTestFailure();  // horodate à 50000
  SensorFailureState s = m.serializeState();

  SensorFailureManager m2 = makeWokenManager(s);  // millis remis à 0
  // Voie cyclique désormais indépendante de millis : reste utilisable.
  m2.noteWakeCycle();
  TEST_ASSERT_TRUE(m2.shouldTestReactivationCyclic());
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
  // Persistance deep-sleep + réactivation par cycles (v1.2.0)
  RUN_TEST(test_serialize_restore_roundtrip);
  RUN_TEST(test_disable_a_travers_cycles_deep_sleep);
  RUN_TEST(test_reactivation_cyclique_pas_de_test_si_actif);
  RUN_TEST(test_reactivation_cyclique_teste_chaque_reveil);
  RUN_TEST(test_reactivation_cyclique_intervalle_n_reveils);
  RUN_TEST(test_reactivation_cyclique_a_travers_cycles);
  RUN_TEST(test_restore_remet_le_millis_a_zero);
  return UNITY_END();
}
