// Tests Unity natifs pour n3_power / n3_ina226_math (logique pure INA226 :
// encodage CONFIG, calibration shunt, conversions, saturation, correction de gain).
//
//   pio test -c platformio-native.ini -e native -f test_ina226_math
//
// Valeurs de reference : TI INA226 datasheet SBOS547A (CAL = 0.00512 /
// (Current_LSB * R), LSB shunt 2,5 uV, LSB bus 1,25 mV, Power_LSB = 25 *
// Current_LSB).

#include <unity.h>
#include "n3_ina226_math.cpp"  // implementation incluse (unite de traduction isolee)

using namespace N3Ina226;

void setUp() {}
void tearDown() {}

// --- Moyennage / temps de conversion ---

void test_avg_codes() {
  TEST_ASSERT_EQUAL_UINT16(1, avgSamplesForCode(0));
  TEST_ASSERT_EQUAL_UINT16(128, avgSamplesForCode(4));
  TEST_ASSERT_EQUAL_UINT16(1024, avgSamplesForCode(7));
  TEST_ASSERT_EQUAL_UINT8(4, avgCodeForSamples(128));
  TEST_ASSERT_EQUAL_UINT8(3, avgCodeForSamples(100));  // 64 <= 100 < 128
  TEST_ASSERT_EQUAL_UINT8(0, avgCodeForSamples(0));
  TEST_ASSERT_EQUAL_UINT8(7, avgCodeForSamples(5000));
}

void test_conversion_codes() {
  TEST_ASSERT_EQUAL_UINT32(1100, conversionTimeUs(4));
  TEST_ASSERT_EQUAL_UINT8(4, conversionCodeForUs(1100));
  TEST_ASSERT_EQUAL_UINT8(3, conversionCodeForUs(1000));
  TEST_ASSERT_EQUAL_UINT8(0, conversionCodeForUs(0));
  TEST_ASSERT_EQUAL_UINT8(7, conversionCodeForUs(100000));
}

// --- Registre CONFIG ---

void test_encode_config_defaut_datasheet() {
  // AVG=1, VBUSCT=VSHCT=1,1 ms, mode continu = valeur de reset 0x4127.
  TEST_ASSERT_EQUAL_HEX16(kConfigDefault, encodeConfig(0, 4, 4, kModeShuntBusContinuous));
}

void test_encode_config_banc() {
  // AVG=128 (code 4), 1,1 ms, continu : 0x4000 | 0x800 | 0x100 | 0x20 | 7
  const uint16_t cfg = encodeConfig(4, 4, 4, kModeShuntBusContinuous);
  TEST_ASSERT_EQUAL_HEX16(0x4927, cfg);
  const ConfigFields f = decodeConfig(cfg);
  TEST_ASSERT_EQUAL_UINT8(4, f.avgCode);
  TEST_ASSERT_EQUAL_UINT8(4, f.busCtCode);
  TEST_ASSERT_EQUAL_UINT8(4, f.shuntCtCode);
  TEST_ASSERT_EQUAL_UINT8(7, f.mode);
  TEST_ASSERT_EQUAL_UINT32(281600, cycleTimeUs(cfg));  // 128 * (1100 + 1100) us
}

void test_encode_config_borne_les_champs() {
  // Champs hors 3 bits : masques, jamais de debordement sur RST (bit 15).
  const uint16_t cfg = encodeConfig(0xFF, 0xFF, 0xFF, 0xFF);
  TEST_ASSERT_EQUAL_HEX16(0x4FFF, cfg);
  TEST_ASSERT_EQUAL_HEX16(0, cfg & kConfigResetBit);
}

// --- Calibration ---

void test_calibration_module_r100() {
  const Calibration c = computeCalibration(0.1f, 0.8f);
  TEST_ASSERT_TRUE(c.ok);
  TEST_ASSERT_EQUAL_UINT16(2097, c.cal);  // trunc(2097.152)
  // LSB effectif recalcule depuis CAL tronque (pas 0.8/32768).
  TEST_ASSERT_FLOAT_WITHIN(1e-9f, 0.00512f / (2097.0f * 0.1f), c.currentLsbA);
  TEST_ASSERT_FLOAT_WITHIN(1e-8f, 25.0f * c.currentLsbA, c.powerLsbW);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8192f, c.shuntLimitA);
  TEST_ASSERT_FALSE(c.imaxBeyondShunt);
  TEST_ASSERT_FALSE(c.calClamped);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 0.8000f, c.measurableMaxA);
}

void test_calibration_imax_au_dela_du_shunt() {
  // R100 + 2 A demandes : le shunt sature a 0,82 A -> signale.
  const Calibration c = computeCalibration(0.1f, 2.0f);
  TEST_ASSERT_TRUE(c.ok);
  TEST_ASSERT_TRUE(c.imaxBeyondShunt);
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.8192f, c.measurableMaxA);
}

void test_calibration_shunt_10_mohm() {
  const Calibration c = computeCalibration(0.01f, 8.0f);
  TEST_ASSERT_TRUE(c.ok);
  TEST_ASSERT_EQUAL_UINT16(2097, c.cal);
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 8.192f, c.shuntLimitA);
  TEST_ASSERT_FALSE(c.imaxBeyondShunt);
}

void test_calibration_cal_borne_a_0x7fff() {
  // Shunt 1 mohm et Imax 0,5 A : CAL exact = 335544 -> borne, LSB elargi.
  const Calibration c = computeCalibration(0.001f, 0.5f);
  TEST_ASSERT_TRUE(c.ok);
  TEST_ASSERT_TRUE(c.calClamped);
  TEST_ASSERT_EQUAL_UINT16(kCalibrationMax, c.cal);
  TEST_ASSERT_FLOAT_WITHIN(1e-8f, 0.00512f / (32767.0f * 0.001f), c.currentLsbA);
}

void test_calibration_parametres_invalides() {
  TEST_ASSERT_FALSE(computeCalibration(0.0f, 1.0f).ok);
  TEST_ASSERT_FALSE(computeCalibration(-0.1f, 1.0f).ok);
  TEST_ASSERT_FALSE(computeCalibration(0.1f, 0.0f).ok);
  TEST_ASSERT_EQUAL_UINT16(0, computeCalibration(0.1f, -1.0f).cal);
}

// --- Conversions ---

void test_conversion_bus() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 12.0f, busVoltageV(9600));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.0f, busVoltageV(0x8000));  // bit 15 ignore
  TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.95875f, busVoltageV(0x7FFF));
}

void test_conversion_shunt_signee() {
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, 81.9175f, shuntVoltageMv(32767));
  TEST_ASSERT_FLOAT_WITHIN(1e-4f, -1.0f, shuntVoltageMv(-400));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, shuntVoltageMv(0));
}

void test_conversion_courant_et_puissance() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, currentFromRegisterA(1000, 1e-4f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -0.25f, currentFromRegisterA(-2500, 1e-4f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.25f, powerFromRegisterW(100, 1e-4f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.1f, currentFromShuntA(10.0f, 0.1f));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, currentFromShuntA(10.0f, 0.0f));
}

void test_chaine_complete_registre_vers_courant() {
  // 0,5 A dans R100 : shunt = 50 mV = 20000 LSB ; la puce calcule
  // Current = Shunt * CAL / 2048 (SBOS547 eq. 3).
  const Calibration c = computeCalibration(0.1f, 0.8f);
  const int16_t shuntRaw = 20000;
  const int16_t currentRaw = static_cast<int16_t>((static_cast<int32_t>(shuntRaw) * c.cal) / 2048);
  const float i = currentFromRegisterA(currentRaw, c.currentLsbA);
  TEST_ASSERT_FLOAT_WITHIN(5e-4f, 0.5f, i);
  TEST_ASSERT_FLOAT_WITHIN(5e-4f, 0.5f, currentFromShuntA(shuntVoltageMv(shuntRaw), 0.1f));
}

// --- Saturation / correction ---

void test_saturation_shunt() {
  TEST_ASSERT_TRUE(isShuntSaturated(32000));
  TEST_ASSERT_TRUE(isShuntSaturated(-32000));
  TEST_ASSERT_TRUE(isShuntSaturated(32767));
  TEST_ASSERT_FALSE(isShuntSaturated(31999));
  TEST_ASSERT_FALSE(isShuntSaturated(-31999));
}

void test_correction_gain_et_sens() {
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.04f, applyCurrentCorrection(1.0f, 1.04f, false));
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, -1.04f, applyCurrentCorrection(1.0f, 1.04f, true));
}

void test_calcul_gain_multimetre() {
  bool ok = false;
  // Mesure 0,50 A, multimetre 0,52 A -> gain 1,04
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.04f, computeGainCorrection(1.0f, 0.50f, 0.52f, 0.01f, 0.5f, 2.0f, &ok));
  TEST_ASSERT_TRUE(ok);
  // Cumul : gain courant deja 1,04, mesure corrigee 0,52 vs 0,52 -> inchange
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.04f, computeGainCorrection(1.04f, 0.52f, 0.52f, 0.01f, 0.5f, 2.0f, &ok));
  // Courant trop faible : refus
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, computeGainCorrection(1.0f, 0.001f, 0.002f, 0.01f, 0.5f, 2.0f, &ok));
  TEST_ASSERT_FALSE(ok);
  // Sens incoherent : refus
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, computeGainCorrection(1.0f, 0.5f, -0.5f, 0.01f, 0.5f, 2.0f, &ok));
  TEST_ASSERT_FALSE(ok);
  // Hors bornes (x4) : refus
  TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, computeGainCorrection(1.0f, 0.5f, 2.0f, 0.01f, 0.5f, 2.0f, &ok));
  TEST_ASSERT_FALSE(ok);
  // Courants negatifs (decharge) acceptes si meme sens
  TEST_ASSERT_FLOAT_WITHIN(1e-5f, 1.1f, computeGainCorrection(1.0f, -1.0f, -1.1f, 0.01f, 0.5f, 2.0f, nullptr));
}

void test_adresses_valides() {
  TEST_ASSERT_TRUE(isValidAddress(0x40));
  TEST_ASSERT_TRUE(isValidAddress(0x44));
  TEST_ASSERT_TRUE(isValidAddress(0x4F));
  TEST_ASSERT_FALSE(isValidAddress(0x3C));  // OLED
  TEST_ASSERT_FALSE(isValidAddress(0x50));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_avg_codes);
  RUN_TEST(test_conversion_codes);
  RUN_TEST(test_encode_config_defaut_datasheet);
  RUN_TEST(test_encode_config_banc);
  RUN_TEST(test_encode_config_borne_les_champs);
  RUN_TEST(test_calibration_module_r100);
  RUN_TEST(test_calibration_imax_au_dela_du_shunt);
  RUN_TEST(test_calibration_shunt_10_mohm);
  RUN_TEST(test_calibration_cal_borne_a_0x7fff);
  RUN_TEST(test_calibration_parametres_invalides);
  RUN_TEST(test_conversion_bus);
  RUN_TEST(test_conversion_shunt_signee);
  RUN_TEST(test_conversion_courant_et_puissance);
  RUN_TEST(test_chaine_complete_registre_vers_courant);
  RUN_TEST(test_saturation_shunt);
  RUN_TEST(test_correction_gain_et_sens);
  RUN_TEST(test_calcul_gain_multimetre);
  RUN_TEST(test_adresses_valides);
  return UNITY_END();
}
