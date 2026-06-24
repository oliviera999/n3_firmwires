#include <cstdint>  // DOIT précéder gpio_mapping.h (pins.h utilise uint8_t sans l'inclure)
#include <unity.h>
#include "../../include/gpio_mapping.h"

// Tests des helpers de mapping GPIO ↔ noms serveur (gpio_mapping.h, GPIOMap).
//
// CONTRAT (firmware↔serveur) : ALL_MAPPINGS est la « table de vérité unique »
// synchronisée avec le serveur distant (n3_serveur OutputSyncService, GPIO 100-117).
// Ces helpers traduisent entre numéros GPIO, noms de champs POST serveur et noms
// internes firmware (gestion des renommages). Toute dérive casserait la sync config.
//
// Fonctions pures (cstring uniquement) — testables nativement, jusqu'ici NON couvertes
// par la CI (suite orpheline test_gpio_mapping = mocks ; ici on teste le VRAI header).

void setUp(void) {}
void tearDown(void) {}

// --- Contrat dbvars : noms POST serveur → GPIO (valeurs vérifiées contre ALL_MAPPINGS) ---
void test_config_field_names_map_to_expected_gpios(void) {
  TEST_ASSERT_EQUAL_UINT8(100, GPIOMap::serverPostNameToGPIO("mail"));
  TEST_ASSERT_EQUAL_UINT8(101, GPIOMap::serverPostNameToGPIO("mailNotif"));
  TEST_ASSERT_EQUAL_UINT8(102, GPIOMap::serverPostNameToGPIO("aqThreshold"));
  TEST_ASSERT_EQUAL_UINT8(103, GPIOMap::serverPostNameToGPIO("tankThreshold"));
  TEST_ASSERT_EQUAL_UINT8(104, GPIOMap::serverPostNameToGPIO("chauffageThreshold"));
  TEST_ASSERT_EQUAL_UINT8(105, GPIOMap::serverPostNameToGPIO("bouffeMatin"));
  TEST_ASSERT_EQUAL_UINT8(106, GPIOMap::serverPostNameToGPIO("bouffeMidi"));
  TEST_ASSERT_EQUAL_UINT8(107, GPIOMap::serverPostNameToGPIO("bouffeSoir"));
  TEST_ASSERT_EQUAL_UINT8(111, GPIOMap::serverPostNameToGPIO("tempsGros"));
  TEST_ASSERT_EQUAL_UINT8(112, GPIOMap::serverPostNameToGPIO("tempsPetits"));
  TEST_ASSERT_EQUAL_UINT8(113, GPIOMap::serverPostNameToGPIO("tempsRemplissageSec"));
  TEST_ASSERT_EQUAL_UINT8(114, GPIOMap::serverPostNameToGPIO("limFlood"));
  TEST_ASSERT_EQUAL_UINT8(116, GPIOMap::serverPostNameToGPIO("FreqWakeUp"));
}

// --- Actionneurs : noms POST → GPIO physiques ---
void test_actuator_field_names_map_to_expected_gpios(void) {
  TEST_ASSERT_EQUAL_UINT8(16, GPIOMap::serverPostNameToGPIO("etatPompeAqua"));
  TEST_ASSERT_EQUAL_UINT8(18, GPIOMap::serverPostNameToGPIO("etatPompeTank"));
  TEST_ASSERT_EQUAL_UINT8(2, GPIOMap::serverPostNameToGPIO("etatHeat"));
  TEST_ASSERT_EQUAL_UINT8(15, GPIOMap::serverPostNameToGPIO("etatUV"));
  TEST_ASSERT_EQUAL_UINT8(108, GPIOMap::serverPostNameToGPIO("bouffePetits"));
  TEST_ASSERT_EQUAL_UINT8(109, GPIOMap::serverPostNameToGPIO("bouffeGros"));
}

// --- Invariant : aller-retour GPIO ↔ nom POST cohérent pour TOUTES les entrées ---
void test_round_trip_gpio_postname_is_consistent(void) {
  for (size_t i = 0; i < GPIOMap::MAPPING_COUNT; i++) {
    const GPIOMapping& m = GPIOMap::ALL_MAPPINGS[i];
    TEST_ASSERT_NOT_NULL(m.serverPostName);
    // nom -> gpio
    TEST_ASSERT_EQUAL_UINT8(m.gpio, GPIOMap::serverPostNameToGPIO(m.serverPostName));
    // gpio -> nom (même chaîne)
    const char* back = GPIOMap::gpioToServerPostName(m.gpio);
    TEST_ASSERT_NOT_NULL(back);
    TEST_ASSERT_EQUAL_STRING(m.serverPostName, back);
    // tout gpio mappé est valide
    TEST_ASSERT_TRUE(GPIOMap::isValidGPIO(m.gpio));
  }
}

// --- internalToServerPost : traduit les noms internes (renommages) vers les noms serveur ---
void test_internal_to_server_post_translates_renames(void) {
  // heaterThresholdC (interne) -> chauffageThreshold (serveur)
  TEST_ASSERT_EQUAL_STRING("chauffageThreshold", GPIOMap::internalToServerPost("heaterThresholdC"));
  // refillDurationSec (interne) -> tempsRemplissageSec (serveur)
  TEST_ASSERT_EQUAL_STRING("tempsRemplissageSec", GPIOMap::internalToServerPost("refillDurationSec"));
  // nom inconnu -> fallback identité
  TEST_ASSERT_EQUAL_STRING("inconnu", GPIOMap::internalToServerPost("inconnu"));
}

// --- findByGPIO : champs corrects pour un GPIO connu ---
void test_find_by_gpio_returns_correct_mapping(void) {
  const GPIOMapping* m = GPIOMap::findByGPIO(104);
  TEST_ASSERT_NOT_NULL(m);
  TEST_ASSERT_EQUAL_STRING("chauffageThreshold", m->serverPostName);
  TEST_ASSERT_EQUAL_STRING("heaterThresholdC", m->internalName);
}

// --- Entrées inconnues / nulles : pas de crash, valeurs neutres ---
void test_unknown_and_null_inputs(void) {
  TEST_ASSERT_NULL(GPIOMap::findByServerPostName("champInexistant"));
  TEST_ASSERT_NULL(GPIOMap::findByServerPostName(nullptr));
  TEST_ASSERT_EQUAL_UINT8(0, GPIOMap::serverPostNameToGPIO("champInexistant"));
  TEST_ASSERT_EQUAL_UINT8(0, GPIOMap::serverPostNameToGPIO(nullptr));
  TEST_ASSERT_NULL(GPIOMap::gpioToServerPostName(250));
  TEST_ASSERT_NULL(GPIOMap::findByGPIO(250));
  TEST_ASSERT_FALSE(GPIOMap::isValidGPIO(250));
  TEST_ASSERT_FALSE(GPIOMap::isValidGPIO(0));
}

// --- Taille de la table figée (détecte ajout/suppression non intentionnel) ---
// 27 = 21 + 6 angles servo nourrissage (GPIO 118-123), ajoutés intentionnellement
// pour s'aligner sur le serveur (cf. n3_serveur GPIO 118-123 / OutputSyncService).
void test_mapping_count_is_stable(void) {
  TEST_ASSERT_EQUAL_UINT(27, (unsigned)GPIOMap::MAPPING_COUNT);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_config_field_names_map_to_expected_gpios);
  RUN_TEST(test_actuator_field_names_map_to_expected_gpios);
  RUN_TEST(test_round_trip_gpio_postname_is_consistent);
  RUN_TEST(test_internal_to_server_post_translates_renames);
  RUN_TEST(test_find_by_gpio_returns_correct_mapping);
  RUN_TEST(test_unknown_and_null_inputs);
  RUN_TEST(test_mapping_count_is_stable);
  return UNITY_END();
}
