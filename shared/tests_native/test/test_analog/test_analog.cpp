// Tests Unity natifs pour n3_analog_sensors (filtrage ADC : médiane, rejet
// d'outliers, moyenne, EMA, validité/fallback, calcul tension batterie).
//
//   pio test -c platformio-native.ini -e native -f test_analog
//
// La logique de filtrage est pure (hors analogRead, mocké) : c'est exactement
// le code partagé par ffp5cs, n3pp et msp, donc une régression ici les touche
// tous les trois.

#include <Arduino.h>             // mock (mocks/Arduino.h)
#include <unity.h>
#include "n3_analog_sensors.cpp" // implémentation incluse (unité de traduction isolée)

using namespace N3Analog;

void setUp() { n3MockAnalogClear(); }
void tearDown() {}

static AnalogConfig baseCfg(FilterMode mode) {
  AnalogConfig c{};
  c.pin = 34;
  c.numSamples = 3;
  c.delayMs = 0;
  c.filterMode = mode;
  c.outlierMax = 0;
  c.minValid = 0;
  c.maxValid = 4095;
  c.fallback = 999;
  c.emaAlpha = 0.0f;
  return c;
}

void test_sortSamples_tri_croissant() {
  uint16_t s[5] = {5, 3, 1, 4, 2};
  sortSamples(s, 5);
  TEST_ASSERT_EQUAL_UINT16(1, s[0]);
  TEST_ASSERT_EQUAL_UINT16(2, s[1]);
  TEST_ASSERT_EQUAL_UINT16(3, s[2]);
  TEST_ASSERT_EQUAL_UINT16(5, s[4]);
}

void test_moyenne() {
  AnalogConfig c = baseCfg(MOYENNE);
  n3MockAnalogPush(100);
  n3MockAnalogPush(200);
  n3MockAnalogPush(300);
  AnalogResult r = readFilteredAnalog(c);
  TEST_ASSERT_TRUE(r.valid);
  TEST_ASSERT_EQUAL_UINT16(200, r.value); // (100+200+300)/3
}

void test_mediane_robuste_au_pic() {
  AnalogConfig c = baseCfg(MEDIANE);
  n3MockAnalogPush(100);
  n3MockAnalogPush(4000); // pic
  n3MockAnalogPush(200);
  AnalogResult r = readFilteredAnalog(c);
  TEST_ASSERT_EQUAL_UINT16(200, r.value); // médiane de {100,200,4000}
}

void test_mediane_puis_moyenne_rejette_outlier() {
  AnalogConfig c = baseCfg(MEDIANE_PUIS_MOYENNE);
  c.numSamples = 4;
  c.outlierMax = 50;
  n3MockAnalogPush(100);
  n3MockAnalogPush(101);
  n3MockAnalogPush(102);
  n3MockAnalogPush(4000); // outlier rejeté
  AnalogResult r = readFilteredAnalog(c);
  // trié {100,101,102,4000} -> médiane samples[2]=102 ; on garde |x-102|<=50 ->
  // {100,101,102} -> moyenne 101
  TEST_ASSERT_EQUAL_UINT16(101, r.value);
}

void test_fallback_si_hors_plage() {
  AnalogConfig c = baseCfg(MOYENNE);
  c.maxValid = 500;
  n3MockAnalogPush(4000);
  n3MockAnalogPush(4000);
  n3MockAnalogPush(4000);
  AnalogResult r = readFilteredAnalog(c);
  TEST_ASSERT_FALSE(r.valid);
  TEST_ASSERT_EQUAL_UINT16(999, r.value); // fallback
}

void test_ema_lisse_les_variations() {
  AnalogConfig c = baseCfg(MOYENNE);
  c.emaAlpha = 0.5f;
  float ema = -1.0f; // sentinelle d'init (prend la 1re valeur telle quelle)
  n3MockAnalogPush(100);
  n3MockAnalogPush(100);
  n3MockAnalogPush(100);
  AnalogResult r1 = readFilteredAnalog(c, &ema);
  TEST_ASSERT_EQUAL_UINT16(100, r1.value);
  n3MockAnalogPush(200);
  n3MockAnalogPush(200);
  n3MockAnalogPush(200);
  AnalogResult r2 = readFilteredAnalog(c, &ema);
  TEST_ASSERT_EQUAL_UINT16(150, r2.value); // 0.5*200 + 0.5*100
}

void test_battery_calcul_tensions() {
  DividerConfig c{};
  c.pin = 35;
  c.R1 = 100000;
  c.R2 = 100000;
  c.Vref = 3.3f;
  c.adcMax = 4095;
  c.numSamples = 2;
  c.delayMs = 0;
  c.rawAt100Percent = 2100;
  c.voltageAtFullScale = 4.2f;
  n3MockAnalogPush(2100);
  n3MockAnalogPush(2100);
  BatteryResult r = readBattery(c);
  TEST_ASSERT_EQUAL_UINT16(2100, r.rawAvg);
  TEST_ASSERT_FLOAT_WITHIN(0.01f, 4.2f, r.batteryVoltage);       // 2100*4.2/2100
  TEST_ASSERT_FLOAT_WITHIN(0.02f, 3.385f, r.measuredVoltage);    // vAdc * (R1+R2)/R2
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_sortSamples_tri_croissant);
  RUN_TEST(test_moyenne);
  RUN_TEST(test_mediane_robuste_au_pic);
  RUN_TEST(test_mediane_puis_moyenne_rejette_outlier);
  RUN_TEST(test_fallback_si_hors_plage);
  RUN_TEST(test_ema_lisse_les_variations);
  RUN_TEST(test_battery_calcul_tensions);
  return UNITY_END();
}
