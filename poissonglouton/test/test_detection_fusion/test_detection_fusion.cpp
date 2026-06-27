// -----------------------------------------------------------------------------
// Tests natifs (hote) de la fusion de capteurs pglFuseDetection — fonction PURE.
//
// On inclut directement src/pgl_sensor_fusion.cpp : aucune dependance Arduino,
// donc AUCUN stub requis. On verrouille la rigueur du comptage (ni double
// comptage ni comptage manque) et la robustesse a tout sous-ensemble de
// {IR, US, PIR} : 1, 2 ou 3 capteurs, et fonctionnement non perturbe quand un
// capteur manque.
//
// Rappel des roles : IR/US = capteurs de COMPTAGE, PIR = capteur de PRESENCE.
// -----------------------------------------------------------------------------

#include <unity.h>

#include "../../src/pgl_sensor_fusion.cpp"

namespace {
constexpr uint32_t kCorrWin = 300;   // fenetre corroboration IR<->US
constexpr uint32_t kPresWin = 4000;  // fenetre presence PIR
constexpr uint32_t kNow = 100000;    // "maintenant" arbitraire

// Construit une entree par defaut : rien ne declenche, aucun edge anterieur.
PglFusionInput makeInput(uint8_t presentMask) {
  PglFusionInput in;
  in.presentMask = presentMask;
  in.irTriggered = false;
  in.usTriggered = false;
  in.pirTriggered = false;
  in.lastIrEdgeMs = 0;
  in.lastUsEdgeMs = 0;
  in.lastPirEdgeMs = 0;
  in.nowMs = kNow;
  in.corroborationWindowMs = kCorrWin;
  in.pirPresenceWindowMs = kPresWin;
  in.pirGatesCount = false;
  in.pirCountsWhenAlone = true;
  return in;
}
}  // namespace

// 1. IR seul, IR trigger -> detected, mask=IR, corroborated=false.
void test_ir_seul_trigger(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 2. US seul, US trigger -> detected, mask=US, false.
void test_us_seul_trigger(void) {
  PglFusionInput in = makeInput(PGL_SENS_US);
  in.usTriggered = true;
  in.lastUsEdgeMs = kNow;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_US, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 3. IR+US, les deux ce poll -> mask=3, corroborated=true.
void test_ir_us_les_deux(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_US);
  in.irTriggered = true;
  in.usTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastUsEdgeMs = kNow;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR | PGL_SENS_US, r.sensorsMask);
  TEST_ASSERT_TRUE(r.corroborated);
}

// 4. IR+US, IR trigger, edge US recent (<=fenetre) -> mask=IR, corroborated=true.
void test_ir_us_us_recent(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_US);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastUsEdgeMs = kNow - 200;  // recent (<=300)
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR, r.sensorsMask);
  TEST_ASSERT_TRUE(r.corroborated);
}

// 5. IR+US, IR trigger, edge US ancien (>fenetre) -> mask=IR, corroborated=false.
void test_ir_us_us_ancien(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_US);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastUsEdgeMs = kNow - 500;  // ancien (>300)
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 6. IR+US, aucun trigger -> not detected.
void test_ir_us_aucun_trigger(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_US);
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_FALSE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(0, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 7. PIR seul, pirCountsWhenAlone=true, PIR trigger -> detected, mask=PIR, false.
void test_pir_seul_compte(void) {
  PglFusionInput in = makeInput(PGL_SENS_PIR);
  in.pirTriggered = true;
  in.lastPirEdgeMs = kNow;
  in.pirCountsWhenAlone = true;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_PIR, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 8. PIR seul, pirCountsWhenAlone=false, PIR trigger -> not detected.
void test_pir_seul_ne_compte_pas(void) {
  PglFusionInput in = makeInput(PGL_SENS_PIR);
  in.pirTriggered = true;
  in.lastPirEdgeMs = kNow;
  in.pirCountsWhenAlone = false;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_FALSE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(0, r.sensorsMask);
}

// 9. PIR seul, pas de trigger -> not detected.
void test_pir_seul_sans_trigger(void) {
  PglFusionInput in = makeInput(PGL_SENS_PIR);
  in.pirCountsWhenAlone = true;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_FALSE(r.detected);
}

// 10. IR+PIR, IR trigger, PIR recent -> mask=IR|PIR, corroborated=true.
void test_ir_pir_pir_recent(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_PIR);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastPirEdgeMs = kNow - 1000;  // recent (<=4000)
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR | PGL_SENS_PIR, r.sensorsMask);
  TEST_ASSERT_TRUE(r.corroborated);
}

// 11. IR+PIR, IR trigger, PIR PAS recent, pirGatesCount=false -> mask=IR, detected, false.
void test_ir_pir_pir_absent_no_gate(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_PIR);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastPirEdgeMs = kNow - 10000;  // ancien (>4000)
  in.pirGatesCount = false;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR, r.sensorsMask);
  TEST_ASSERT_FALSE(r.corroborated);
}

// 12. IR+PIR, IR trigger, PIR PAS recent, pirGatesCount=true -> NOT detected (gate).
void test_ir_pir_pir_absent_gate(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_PIR);
  in.irTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastPirEdgeMs = kNow - 10000;  // ancien (>4000)
  in.pirGatesCount = true;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_FALSE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(0, r.sensorsMask);
}

// 13. Les 3, IR+US trigger, PIR recent -> mask=7, corroborated=true.
void test_trois_capteurs(void) {
  PglFusionInput in = makeInput(PGL_SENS_IR | PGL_SENS_US | PGL_SENS_PIR);
  in.irTriggered = true;
  in.usTriggered = true;
  in.lastIrEdgeMs = kNow;
  in.lastUsEdgeMs = kNow;
  in.lastPirEdgeMs = kNow - 500;  // recent
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_IR | PGL_SENS_US | PGL_SENS_PIR, r.sensorsMask);
  TEST_ASSERT_TRUE(r.corroborated);
}

// 14. Aucun capteur present -> jamais detected.
void test_aucun_capteur(void) {
  PglFusionInput in = makeInput(0);
  in.irTriggered = true;  // triggers ignores car aucun capteur present
  in.usTriggered = true;
  in.pirTriggered = true;
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_FALSE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(0, r.sensorsMask);
}

// 15. US+PIR, US trigger, PIR recent -> mask=US|PIR, corroborated=true.
void test_us_pir_pir_recent(void) {
  PglFusionInput in = makeInput(PGL_SENS_US | PGL_SENS_PIR);
  in.usTriggered = true;
  in.lastUsEdgeMs = kNow;
  in.lastPirEdgeMs = kNow - 1000;  // recent
  PglFusionResult r = pglFuseDetection(in);
  TEST_ASSERT_TRUE(r.detected);
  TEST_ASSERT_EQUAL_UINT8(PGL_SENS_US | PGL_SENS_PIR, r.sensorsMask);
  TEST_ASSERT_TRUE(r.corroborated);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_ir_seul_trigger);
  RUN_TEST(test_us_seul_trigger);
  RUN_TEST(test_ir_us_les_deux);
  RUN_TEST(test_ir_us_us_recent);
  RUN_TEST(test_ir_us_us_ancien);
  RUN_TEST(test_ir_us_aucun_trigger);
  RUN_TEST(test_pir_seul_compte);
  RUN_TEST(test_pir_seul_ne_compte_pas);
  RUN_TEST(test_pir_seul_sans_trigger);
  RUN_TEST(test_ir_pir_pir_recent);
  RUN_TEST(test_ir_pir_pir_absent_no_gate);
  RUN_TEST(test_ir_pir_pir_absent_gate);
  RUN_TEST(test_trois_capteurs);
  RUN_TEST(test_aucun_capteur);
  RUN_TEST(test_us_pir_pir_recent);
  return UNITY_END();
}
