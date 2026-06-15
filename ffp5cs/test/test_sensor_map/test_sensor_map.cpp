#include <cstdint>  // DOIT précéder gpio_mapping.h (pins.h utilise uint8_t sans l'inclure)
#include <unity.h>
#include "../../include/gpio_mapping.h"

// Tests de SensorMap (gpio_mapping.h) : traduction des noms de capteurs
// firmware/WebSocket (ex "tempWater") ↔ serveur distant POST (ex "TempEau").
//
// CONTRAT (firmware↔serveur) : ALL_SENSORS est la table de vérité des noms capteurs.
// internalToServerPost() est utilisé pour sérialiser les mesures vers le serveur ;
// toute dérive casserait le POST des données capteur. Fonctions pures (cstring).

void setUp(void) {}
void tearDown(void) {}

// --- Traduction interne -> serveur pour les 8 capteurs (valeurs vérifiées) ---
void test_internal_to_server_post_names(void) {
  TEST_ASSERT_EQUAL_STRING("TempEau", SensorMap::internalToServerPost("tempWater"));
  TEST_ASSERT_EQUAL_STRING("TempAir", SensorMap::internalToServerPost("tempAir"));
  TEST_ASSERT_EQUAL_STRING("Humidite", SensorMap::internalToServerPost("humidity"));
  TEST_ASSERT_EQUAL_STRING("EauAquarium", SensorMap::internalToServerPost("wlAqua"));
  TEST_ASSERT_EQUAL_STRING("EauReserve", SensorMap::internalToServerPost("wlTank"));
  TEST_ASSERT_EQUAL_STRING("EauPotager", SensorMap::internalToServerPost("wlPota"));
  TEST_ASSERT_EQUAL_STRING("Luminosite", SensorMap::internalToServerPost("luminosite"));
  TEST_ASSERT_EQUAL_STRING("diffMaree", SensorMap::internalToServerPost("diffMaree"));
}

// --- findByInternalName renvoie le champ correct ---
void test_find_by_internal_name(void) {
  const SensorMap::SensorField* f = SensorMap::findByInternalName("wlAqua");
  TEST_ASSERT_NOT_NULL(f);
  TEST_ASSERT_EQUAL_STRING("wlAqua", f->internalName);
  TEST_ASSERT_EQUAL_STRING("EauAquarium", f->serverPostName);
}

// --- Invariant : internalToServerPost(internal) == serverPostName pour TOUS ---
void test_round_trip_internal_to_server(void) {
  for (size_t i = 0; i < SensorMap::SENSOR_COUNT; i++) {
    const SensorMap::SensorField& s = SensorMap::ALL_SENSORS[i];
    TEST_ASSERT_EQUAL_STRING(s.serverPostName, SensorMap::internalToServerPost(s.internalName));
    const SensorMap::SensorField* found = SensorMap::findByInternalName(s.internalName);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_EQUAL_STRING(s.serverPostName, found->serverPostName);
  }
}

// --- Entrées inconnues / nulles : fallback identité, pas de crash ---
void test_unknown_and_null_inputs(void) {
  TEST_ASSERT_EQUAL_STRING("inconnu", SensorMap::internalToServerPost("inconnu"));
  TEST_ASSERT_NULL(SensorMap::findByInternalName(nullptr));
  TEST_ASSERT_NULL(SensorMap::findByInternalName(""));
}

// --- Taille de table figée + noms non vides ---
void test_table_integrity(void) {
  TEST_ASSERT_EQUAL_UINT(8, (unsigned)SensorMap::SENSOR_COUNT);
  for (size_t i = 0; i < SensorMap::SENSOR_COUNT; i++) {
    const SensorMap::SensorField& s = SensorMap::ALL_SENSORS[i];
    TEST_ASSERT_NOT_NULL(s.internalName);
    TEST_ASSERT_NOT_NULL(s.serverPostName);
    TEST_ASSERT_NOT_EQUAL_INT(0, s.internalName[0]);
    TEST_ASSERT_NOT_EQUAL_INT(0, s.serverPostName[0]);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_internal_to_server_post_names);
  RUN_TEST(test_find_by_internal_name);
  RUN_TEST(test_round_trip_internal_to_server);
  RUN_TEST(test_unknown_and_null_inputs);
  RUN_TEST(test_table_integrity);
  return UNITY_END();
}
