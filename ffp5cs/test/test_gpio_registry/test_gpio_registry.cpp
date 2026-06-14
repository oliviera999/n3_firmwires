#include <unity.h>
#include <cstdint>
#include <cstring>

// Tests de gpio_mapping.h : SOURCE DE VÉRITÉ UNIQUE du mapping
// GPIO <-> nom serveur POST <-> nom interne firmware (+ SensorMap capteurs).
//
// CONTRAT SERVEUR (S2) : ce header est explicitement synchronisé avec
// OutputController.php / PostDataController.php / OutputRepository.php (cf.
// en-tête du fichier). Les « anchors » ci-dessous figent les correspondances :
// si un nom serveur change ici sans accord serveur, le test casse (et inversement).
//
// Header pur (pins.h + cstring) : inclusion directe en natif.
#include "../../include/gpio_mapping.h"

void setUp(void) {}
void tearDown(void) {}

// ---------------- Anchors de contrat (GPIO fixe -> nom serveur) ----------------
// On n'ancre que les GPIO à numéro fixe (100-116, 108/109), pas les actionneurs
// basés sur Pins::* (dépendants de la carte WROOM/S3).

void test_contract_anchors_config_names(void) {
  TEST_ASSERT_EQUAL_STRING("mail",               GPIOMap::gpioToServerPostName(100));
  TEST_ASSERT_EQUAL_STRING("mailNotif",          GPIOMap::gpioToServerPostName(101));
  TEST_ASSERT_EQUAL_STRING("aqThreshold",        GPIOMap::gpioToServerPostName(102));
  TEST_ASSERT_EQUAL_STRING("tankThreshold",      GPIOMap::gpioToServerPostName(103));
  TEST_ASSERT_EQUAL_STRING("chauffageThreshold", GPIOMap::gpioToServerPostName(104));
  TEST_ASSERT_EQUAL_STRING("bouffeMatin",        GPIOMap::gpioToServerPostName(105));
  TEST_ASSERT_EQUAL_STRING("bouffeSoir",         GPIOMap::gpioToServerPostName(107));
  TEST_ASSERT_EQUAL_STRING("bouffePetits",       GPIOMap::gpioToServerPostName(108));
  TEST_ASSERT_EQUAL_STRING("bouffeGros",         GPIOMap::gpioToServerPostName(109));
  TEST_ASSERT_EQUAL_STRING("tempsRemplissageSec",GPIOMap::gpioToServerPostName(113));
}

void test_contract_anchors_reverse(void) {
  TEST_ASSERT_EQUAL_UINT8(104, GPIOMap::serverPostNameToGPIO("chauffageThreshold"));
  TEST_ASSERT_EQUAL_UINT8(109, GPIOMap::serverPostNameToGPIO("bouffeGros"));
  TEST_ASSERT_EQUAL_UINT8(116, GPIOMap::serverPostNameToGPIO("FreqWakeUp"));
}

// ---------------- Round-trip / cohérence sur toute la table ----------------

void test_roundtrip_gpio_servername_consistent(void) {
  for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; ++i) {
    const GPIOMapping& m = GPIOMap::ALL_MAPPINGS[i];
    // findByGPIO retrouve la même entrée
    const GPIOMapping* byG = GPIOMap::findByGPIO(m.gpio);
    TEST_ASSERT_NOT_NULL(byG);
    TEST_ASSERT_EQUAL_STRING(m.serverPostName, byG->serverPostName);
    // gpio -> nom -> gpio
    const char* name = GPIOMap::gpioToServerPostName(m.gpio);
    TEST_ASSERT_NOT_NULL(name);
    TEST_ASSERT_EQUAL_UINT8(m.gpio, GPIOMap::serverPostNameToGPIO(name));
  }
}

// ---------------- Invariants d'unicité (garde anti-collision) ----------------

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

// ---------------- internalToServerPost : traduction + fallback ----------------

void test_internal_to_server_known(void) {
  TEST_ASSERT_EQUAL_STRING("chauffageThreshold",
                           GPIOMap::internalToServerPost("heaterThresholdC"));
}

void test_internal_to_server_fallback_same_name(void) {
  // Nom inconnu -> renvoyé tel quel (fallback documenté).
  TEST_ASSERT_EQUAL_STRING("inconnu123",
                           GPIOMap::internalToServerPost("inconnu123"));
}

// ---------------- Cas dégénérés / not-found ----------------

void test_lookups_not_found_and_null(void) {
  TEST_ASSERT_NULL(GPIOMap::findByGPIO(250));            // GPIO absent
  TEST_ASSERT_NULL(GPIOMap::findByServerPostName("nope"));
  TEST_ASSERT_NULL(GPIOMap::findByServerPostName(nullptr));
  TEST_ASSERT_NULL(GPIOMap::findByInternalName(nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, GPIOMap::serverPostNameToGPIO("nope"));  // 0 = sentinelle
  TEST_ASSERT_NULL(GPIOMap::gpioToServerPostName(250));
  TEST_ASSERT_FALSE(GPIOMap::isValidGPIO(250));
}

void test_getType_known_and_default(void) {
  TEST_ASSERT_TRUE(GPIOMap::getType(104) == GPIOType::CONFIG_FLOAT);  // seuil chauffage
  TEST_ASSERT_TRUE(GPIOMap::getType(102) == GPIOType::CONFIG_INT);    // seuil aquarium
  TEST_ASSERT_TRUE(GPIOMap::getType(250) == GPIOType::CONFIG_INT);    // inconnu -> défaut
}

// ---------------- SensorMap ----------------

void test_sensormap_internal_to_server(void) {
  TEST_ASSERT_EQUAL_STRING("TempEau",    SensorMap::internalToServerPost("tempWater"));
  TEST_ASSERT_EQUAL_STRING("EauPotager", SensorMap::internalToServerPost("wlPota"));
  // Fallback nom inconnu
  TEST_ASSERT_EQUAL_STRING("xyz", SensorMap::internalToServerPost("xyz"));
  // Null safety
  TEST_ASSERT_NULL(SensorMap::findByInternalName(nullptr));
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_contract_anchors_config_names);
  RUN_TEST(test_contract_anchors_reverse);
  RUN_TEST(test_roundtrip_gpio_servername_consistent);
  RUN_TEST(test_no_duplicate_gpio);
  RUN_TEST(test_no_duplicate_server_post_name);
  RUN_TEST(test_internal_to_server_known);
  RUN_TEST(test_internal_to_server_fallback_same_name);
  RUN_TEST(test_lookups_not_found_and_null);
  RUN_TEST(test_getType_known_and_default);
  RUN_TEST(test_sensormap_internal_to_server);
  return UNITY_END();
}
