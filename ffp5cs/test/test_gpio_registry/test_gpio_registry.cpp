#include <unity.h>
#include <cstdint>
#include <cstring>

// COMPLÉMENT de test_gpio_lookup (master) sur gpio_mapping.h.
// test_gpio_lookup couvre déjà : anchors GPIO<->nom serveur, round-trip,
// internalToServerPost, findByGPIO, not-found/null. On ajoute ici ce qu'il ne
// teste pas :
//   - INVARIANTS d'unicité de la table (garde anti-collision si on l'étend) ;
//   - getType (type connu + défaut) ;
//   - SensorMap (mapping des champs capteurs firmware<->serveur).
//
// Header pur (pins.h + cstring) : inclusion directe en natif.
#include "../../include/gpio_mapping.h"

void setUp(void) {}
void tearDown(void) {}

// ---- Invariants d'unicité (non couverts par test_gpio_lookup) ----

void test_no_duplicate_gpio(void) {
  for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; ++i)
    for (size_t j = i + 1; j < GPIOMap::MAPPING_COUNT; ++j)
      TEST_ASSERT_NOT_EQUAL_UINT8(GPIOMap::ALL_MAPPINGS[i].gpio,
                                  GPIOMap::ALL_MAPPINGS[j].gpio);
}

void test_no_duplicate_server_post_name(void) {
  for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; ++i)
    for (size_t j = i + 1; j < GPIOMap::MAPPING_COUNT; ++j)
      TEST_ASSERT_FALSE(strcmp(GPIOMap::ALL_MAPPINGS[i].serverPostName,
                               GPIOMap::ALL_MAPPINGS[j].serverPostName) == 0);
}

// ---- getType ----

void test_getType_known_and_default(void) {
  TEST_ASSERT_TRUE(GPIOMap::getType(104) == GPIOType::CONFIG_FLOAT);  // seuil chauffage
  TEST_ASSERT_TRUE(GPIOMap::getType(102) == GPIOType::CONFIG_INT);    // seuil aquarium
  TEST_ASSERT_TRUE(GPIOMap::getType(250) == GPIOType::CONFIG_INT);    // inconnu -> défaut
}

// ---- SensorMap (mapping capteurs firmware <-> serveur distant) ----

void test_sensormap_internal_to_server(void) {
  TEST_ASSERT_EQUAL_STRING("TempEau",    SensorMap::internalToServerPost("tempWater"));
  TEST_ASSERT_EQUAL_STRING("EauPotager", SensorMap::internalToServerPost("wlPota"));
  TEST_ASSERT_EQUAL_STRING("Luminosite", SensorMap::internalToServerPost("luminosite"));
}

void test_sensormap_fallback_and_null(void) {
  TEST_ASSERT_EQUAL_STRING("xyz", SensorMap::internalToServerPost("xyz"));  // fallback
  TEST_ASSERT_NULL(SensorMap::findByInternalName(nullptr));
  TEST_ASSERT_NULL(SensorMap::findByInternalName("inconnu"));
}

void test_sensormap_no_duplicate_internal_name(void) {
  for (size_t i = 0; i < SensorMap::SENSOR_COUNT; ++i)
    for (size_t j = i + 1; j < SensorMap::SENSOR_COUNT; ++j)
      TEST_ASSERT_FALSE(strcmp(SensorMap::ALL_SENSORS[i].internalName,
                               SensorMap::ALL_SENSORS[j].internalName) == 0);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_duplicate_gpio);
  RUN_TEST(test_no_duplicate_server_post_name);
  RUN_TEST(test_getType_known_and_default);
  RUN_TEST(test_sensormap_internal_to_server);
  RUN_TEST(test_sensormap_fallback_and_null);
  RUN_TEST(test_sensormap_no_duplicate_internal_name);
  return UNITY_END();
}
