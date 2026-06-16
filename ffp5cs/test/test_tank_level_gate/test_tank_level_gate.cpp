#include <unity.h>
#include <cstdint>

// Tests des prédicats de validité de distance ultrason RÉSERVOIR (cuve étroite),
// extraits de config_sensors.h :
//   - SensorConfig::Ultrasonic::Tank::isOperationalMm(mm) : la distance est-elle
//     dans la plage métier exploitable [15, 1000] mm (sinon mesure ignorée) ;
//   - SensorConfig::Ultrasonic::Tank::isRawReadingMm(mm)  : la lecture brute est-elle
//     plausible [15, 4000) mm (15 = cuve pleine, sous la spec HC-SR04 20 mm) ;
//   - SensorValidation::isWaterLevelKnown(wl) : niveau d'eau connu (0 < wl <= 5000)
//     servant de garde-fou à l'automatisme / au POST serveur.
// Ces gardes décident si une lecture est acceptée : une borne fausse = faux
// « réservoir plein » (pompe jamais relancée) ou faux « vide » (débordement aquarium).
//
// config_sensors.h tire <Arduino.h> (isnan / constantes Arduino) et n'est donc pas
// incluable en natif sous la commande de validation. On REPRODUIT donc la logique
// et SES CONSTANTES à l'identique (même style d'extraction que test_sensor_validation),
// avec un static_assert de cohérence pour figer les valeurs de référence.

namespace TankRef {
  // Valeurs MIROIR de config_sensors.h::SensorConfig::Ultrasonic::Tank / ::Ultrasonic.
  // Si une de ces constantes change côté prod, adapter ici (et les tests doivent
  // alors révéler la modification de comportement attendue).
  constexpr uint16_t MIN_OPERATIONAL_MM = 15;
  constexpr uint16_t MAX_OPERATIONAL_MM = 1000;
  constexpr uint16_t MIN_RAW_MM = 15;
  constexpr uint16_t MAX_DISTANCE_MM = 4000;   // borne haute lecture brute (exclusive)
  constexpr uint16_t MAX_VALID_LEVEL_MM = 5000; // borne « niveau eau » applicative

  // Transcription FIDÈLE des inline constexpr de config_sensors.h.
  constexpr bool isOperationalMm(uint16_t mm) {
    return mm >= MIN_OPERATIONAL_MM && mm <= MAX_OPERATIONAL_MM;
  }
  constexpr bool isRawReadingMm(uint16_t mm) {
    return mm >= MIN_RAW_MM && mm < MAX_DISTANCE_MM;
  }
  // Transcription de SensorValidation::isWaterLevelKnown.
  constexpr bool isWaterLevelKnown(uint16_t wl) {
    return wl > 0 && wl <= MAX_VALID_LEVEL_MM;
  }

  // Garde-fou : fige les bornes de référence (détecte une dérive de copie évidente).
  static_assert(MIN_OPERATIONAL_MM == 15 && MAX_OPERATIONAL_MM == 1000, "bornes operational");
  static_assert(MIN_RAW_MM == 15 && MAX_DISTANCE_MM == 4000, "bornes raw");
  static_assert(MAX_VALID_LEVEL_MM == 5000, "borne niveau eau");
}

using namespace TankRef;

void setUp(void) {}
void tearDown(void) {}

// ---------------------------------------------------------------------------
// isOperationalMm : plage métier [15, 1000] inclusive des deux côtés.
// ---------------------------------------------------------------------------
void test_operational_inside_range(void) {
  TEST_ASSERT_TRUE(isOperationalMm(15));    // borne basse inclusive
  TEST_ASSERT_TRUE(isOperationalMm(16));
  TEST_ASSERT_TRUE(isOperationalMm(500));   // milieu
  TEST_ASSERT_TRUE(isOperationalMm(999));
  TEST_ASSERT_TRUE(isOperationalMm(1000));   // borne haute inclusive
}

void test_operational_below_min_rejected(void) {
  TEST_ASSERT_FALSE(isOperationalMm(0));
  TEST_ASSERT_FALSE(isOperationalMm(1));
  TEST_ASSERT_FALSE(isOperationalMm(14));    // juste sous la borne basse
}

void test_operational_above_max_rejected(void) {
  TEST_ASSERT_FALSE(isOperationalMm(1001));   // juste au-dessus
  TEST_ASSERT_FALSE(isOperationalMm(4000));
  TEST_ASSERT_FALSE(isOperationalMm(65535));  // max uint16
}

// ---------------------------------------------------------------------------
// isRawReadingMm : [15, 4000) — borne basse INCLUSIVE, borne haute EXCLUSIVE.
// 15 mm correspond à une cuve pleine (sous la spec datasheet 20 mm).
// ---------------------------------------------------------------------------
void test_raw_inside_range(void) {
  TEST_ASSERT_TRUE(isRawReadingMm(15));     // cuve pleine, borne basse inclusive
  TEST_ASSERT_TRUE(isRawReadingMm(20));     // spec datasheet
  TEST_ASSERT_TRUE(isRawReadingMm(2000));
  TEST_ASSERT_TRUE(isRawReadingMm(3999));   // juste sous la borne haute
}

void test_raw_below_min_rejected(void) {
  TEST_ASSERT_FALSE(isRawReadingMm(0));
  TEST_ASSERT_FALSE(isRawReadingMm(14));
}

void test_raw_high_bound_exclusive(void) {
  // 4000 est EXCLU (strict <), contrairement à isOperationalMm dont la borne
  // haute est inclusive : différence de comportement à figer.
  TEST_ASSERT_FALSE(isRawReadingMm(4000));
  TEST_ASSERT_FALSE(isRawReadingMm(4001));
  TEST_ASSERT_FALSE(isRawReadingMm(65535));
}

// La plage RAW est plus large que la plage OPERATIONAL : toute distance
// opérationnelle est une lecture brute valide, mais pas l'inverse.
void test_operational_subset_of_raw(void) {
  for (uint32_t mm = 0; mm <= 4100U; ++mm) {
    uint16_t v = static_cast<uint16_t>(mm);
    if (isOperationalMm(v)) {
      TEST_ASSERT_TRUE(isRawReadingMm(v));
    }
  }
  // Exemple concret hors-sous-ensemble : 1500 mm est brut-valide mais non-opérationnel.
  TEST_ASSERT_TRUE(isRawReadingMm(1500));
  TEST_ASSERT_FALSE(isOperationalMm(1500));
}

// ---------------------------------------------------------------------------
// isWaterLevelKnown : 0 = inconnu (capteur muet), sinon valide jusqu'à 5000 mm.
// ---------------------------------------------------------------------------
void test_water_level_zero_unknown(void) {
  TEST_ASSERT_FALSE(isWaterLevelKnown(0));   // 0 = mesure absente
}

void test_water_level_known_range(void) {
  TEST_ASSERT_TRUE(isWaterLevelKnown(1));    // strictement > 0
  TEST_ASSERT_TRUE(isWaterLevelKnown(152));  // niveau aquarium typique
  TEST_ASSERT_TRUE(isWaterLevelKnown(5000));  // borne haute inclusive
}

void test_water_level_above_max_unknown(void) {
  TEST_ASSERT_FALSE(isWaterLevelKnown(5001));  // au-delà de la borne applicative
  TEST_ASSERT_FALSE(isWaterLevelKnown(65535));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_operational_inside_range);
  RUN_TEST(test_operational_below_min_rejected);
  RUN_TEST(test_operational_above_max_rejected);
  RUN_TEST(test_raw_inside_range);
  RUN_TEST(test_raw_below_min_rejected);
  RUN_TEST(test_raw_high_bound_exclusive);
  RUN_TEST(test_operational_subset_of_raw);
  RUN_TEST(test_water_level_zero_unknown);
  RUN_TEST(test_water_level_known_range);
  RUN_TEST(test_water_level_above_max_unknown);
  return UNITY_END();
}
