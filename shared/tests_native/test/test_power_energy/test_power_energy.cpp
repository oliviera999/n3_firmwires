// Tests Unity natifs pour n3_power / n3_power_energy (accumulateur par voie :
// statistiques de fenetre, integration trapezoidale Wh/Ah, trous de mesure,
// debordement de millis()).
//
//   pio test -c platformio-native.ini -e native -f test_power_energy

#include <unity.h>
#include "n3_power_energy.cpp"  // implementation incluse (unite de traduction isolee)

using namespace N3Power;

// Unity est compile sans UNITY_INCLUDE_DOUBLE (config partagee) : on compare
// les doubles en float, largement suffisant aux ordres de grandeur testes.
#define ASSERT_DBL_WITHIN(tol, expected, actual) \
  TEST_ASSERT_FLOAT_WITHIN(static_cast<float>(tol), static_cast<float>(expected), static_cast<float>(actual))

void setUp() {}
void tearDown() {}

void test_premiere_mesure_non_integree() {
  ChannelAccumulator acc;
  const Increment inc = acc.add(1000, 12.0f, 1.0f, 12.0f);
  TEST_ASSERT_FALSE(inc.integrated);
  ASSERT_DBL_WITHIN(1e-9, 0.0, acc.totalEnergyWh());
  TEST_ASSERT_EQUAL_UINT32(1, acc.peek().samples);
}

void test_integration_constante() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, 1.0f, 10.0f);
  const Increment inc = acc.add(1000, 12.0f, 1.0f, 10.0f);
  TEST_ASSERT_TRUE(inc.integrated);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, inc.dtSeconds);
  ASSERT_DBL_WITHIN(1e-7, 10.0 / 3600.0, inc.wh);
  ASSERT_DBL_WITHIN(1e-7, 1.0 / 3600.0, inc.ah);
}

void test_integration_trapezoidale() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, 0.0f, 0.0f);
  const Increment inc = acc.add(1000, 12.0f, 2.0f, 20.0f);
  // Moyenne des bornes : 10 W et 1 A pendant 1 s
  ASSERT_DBL_WITHIN(1e-7, 10.0 / 3600.0, inc.wh);
  ASSERT_DBL_WITHIN(1e-7, 1.0 / 3600.0, inc.ah);
}

void test_integration_signee_decharge() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, -2.0f, -24.0f);
  acc.add(1800000, 12.0f, -2.0f, -24.0f);  // 30 min -> hors maxGap par defaut
  ASSERT_DBL_WITHIN(1e-9, 0.0, acc.totalChargeAh());
  ChannelAccumulator big(3600000);  // maxGap 1 h
  big.add(0, 12.0f, -2.0f, -24.0f);
  big.add(1800000, 12.0f, -2.0f, -24.0f);
  ASSERT_DBL_WITHIN(1e-7, -1.0, big.totalChargeAh());
  ASSERT_DBL_WITHIN(1e-7, -12.0, big.totalEnergyWh());
}

void test_trou_de_mesure_non_integre_et_compte() {
  ChannelAccumulator acc(5000);
  acc.add(0, 12.0f, 1.0f, 12.0f);
  const Increment inc = acc.add(10000, 12.0f, 1.0f, 12.0f);
  TEST_ASSERT_FALSE(inc.integrated);
  TEST_ASSERT_EQUAL_UINT32(1, acc.peek().gaps);
  // La continuite repart de la mesure a 10 s
  TEST_ASSERT_TRUE(acc.add(11000, 12.0f, 1.0f, 12.0f).integrated);
}

void test_dt_nul_ignore() {
  ChannelAccumulator acc;
  acc.add(500, 12.0f, 1.0f, 12.0f);
  TEST_ASSERT_FALSE(acc.add(500, 12.0f, 1.0f, 12.0f).integrated);
  TEST_ASSERT_EQUAL_UINT32(0, acc.peek().gaps);
}

void test_debordement_millis() {
  ChannelAccumulator acc;
  acc.add(0xFFFFFC18u, 12.0f, 1.0f, 10.0f);  // 1 s avant le debordement
  const Increment inc = acc.add(0, 12.0f, 1.0f, 10.0f);
  TEST_ASSERT_TRUE(inc.integrated);
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, inc.dtSeconds);
}

void test_invalidate_rompt_la_continuite() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, 1.0f, 12.0f);
  acc.invalidate();
  TEST_ASSERT_FALSE(acc.add(1000, 12.0f, 1.0f, 12.0f).integrated);
  TEST_ASSERT_TRUE(acc.add(2000, 12.0f, 1.0f, 12.0f).integrated);
}

void test_statistiques_fenetre() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, 1.0f, 12.0f);
  acc.add(1000, 13.0f, 2.0f, 26.0f);
  acc.add(2000, 11.0f, -1.0f, -11.0f);
  const WindowSnapshot s = acc.peek();
  TEST_ASSERT_EQUAL_UINT32(3, s.samples);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 12.0f, s.avgV);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 11.0f, s.minV);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 13.0f, s.maxV);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -1.0f, s.minI);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 2.0f, s.maxI);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 9.0f, s.avgP);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, -11.0f, s.minP);
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 26.0f, s.maxP);
}

void test_take_remet_la_fenetre_a_zero_mais_garde_cumuls_et_continuite() {
  ChannelAccumulator acc;
  acc.add(0, 12.0f, 1.0f, 36.0f);
  acc.add(1000, 12.0f, 1.0f, 36.0f);
  const WindowSnapshot w1 = acc.take();
  TEST_ASSERT_EQUAL_UINT32(2, w1.samples);
  ASSERT_DBL_WITHIN(1e-7, 0.01, w1.energyWh);  // 36 W * 1 s
  TEST_ASSERT_EQUAL_UINT32(0, acc.peek().samples);
  // Continuite : la 1re mesure de la nouvelle fenetre s'integre avec la derniere
  TEST_ASSERT_TRUE(acc.add(2000, 12.0f, 1.0f, 36.0f).integrated);
  const WindowSnapshot w2 = acc.take();
  TEST_ASSERT_EQUAL_UINT32(1, w2.samples);
  ASSERT_DBL_WITHIN(1e-7, 0.01, w2.energyWh);
  ASSERT_DBL_WITHIN(1e-7, 0.02, acc.totalEnergyWh());
  acc.resetTotals();
  ASSERT_DBL_WITHIN(1e-9, 0.0, acc.totalEnergyWh());
  ASSERT_DBL_WITHIN(1e-9, 0.0, acc.totalChargeAh());
}

void test_fenetre_vide() {
  ChannelAccumulator acc;
  const WindowSnapshot s = acc.take();
  TEST_ASSERT_EQUAL_UINT32(0, s.samples);
  TEST_ASSERT_EQUAL_FLOAT(0.0f, s.avgV);
  ASSERT_DBL_WITHIN(1e-9, 0.0, s.energyWh);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_premiere_mesure_non_integree);
  RUN_TEST(test_integration_constante);
  RUN_TEST(test_integration_trapezoidale);
  RUN_TEST(test_integration_signee_decharge);
  RUN_TEST(test_trou_de_mesure_non_integre_et_compte);
  RUN_TEST(test_dt_nul_ignore);
  RUN_TEST(test_debordement_millis);
  RUN_TEST(test_invalidate_rompt_la_continuite);
  RUN_TEST(test_statistiques_fenetre);
  RUN_TEST(test_take_remet_la_fenetre_a_zero_mais_garde_cumuls_et_continuite);
  RUN_TEST(test_fenetre_vide);
  return UNITY_END();
}
