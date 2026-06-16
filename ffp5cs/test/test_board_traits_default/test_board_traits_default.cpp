#include <unity.h>

// Tests de BoardTraits (board_traits.h) — helpers constexpr de sélection
// carte/profil/endpoints utilisés DANS TOUT le firmware (BOARD_TYPE dans
// config_system.h, timeouts WiFi dans config_tasks.h, choix d'endpoints serveur,
// activation d'endpoints debug). Chaque prédicat renvoie true ssi sa macro de
// compilation est définie. Un prédicat inversé = mauvais endpoint / mauvais
// timeout / debug activé en prod sur toute une classe de cartes.
//
// CE FICHIER couvre le cas DÉFAUT : AUCUNE macro carte/profil/endpoint définie
// (la branche `#else -> return false` de chaque helper). Le pendant « branches
// true » est dans test_board_traits_s3beta/. On inclut le VRAI header de prod
// (constexpr, sans dépendance) — aucune macro n'est définie avant l'inclusion.
//
// IMPORTANT : ne définir AUCUNE des macros BOARD_*/PROFILE_*/USE_*_ENDPOINTS/
// FFP_*/DISABLE_* ici, sinon le cas « tout faux » n'est plus testé.
#include "../../include/board_traits.h"

using namespace BoardTraits;

void setUp(void) {}
void tearDown(void) {}

// --- Cible matérielle : aucune carte définie -> tous faux ---
void test_no_board_macros_all_false(void) {
  TEST_ASSERT_FALSE(isS3());
  TEST_ASSERT_FALSE(isWroom());
  TEST_ASSERT_FALSE(hasPsram());
}

// --- Profil : aucun PROFILE_* défini -> tous faux ---
void test_no_profile_macros_all_false(void) {
  TEST_ASSERT_FALSE(isProd());
  TEST_ASSERT_FALSE(isBeta());
  TEST_ASSERT_FALSE(isTest());
  TEST_ASSERT_FALSE(isDev());
}

// --- isNonProd dérive de !isProd() : sans PROFILE_PROD -> profil « inconnu »
//     considéré non-prod (logs verbeux / endpoints debug autorisés). ---
void test_isNonProd_true_when_not_prod(void) {
  TEST_ASSERT_FALSE(isProd());
  TEST_ASSERT_TRUE(isNonProd());
  // Invariant structurel : isNonProd() == !isProd() en toute circonstance.
  TEST_ASSERT_EQUAL(!isProd(), isNonProd());
}

// --- Endpoints : aucune macro USE_*_ENDPOINTS -> endpoints de prod (tous faux) ---
void test_no_endpoint_macros_all_false(void) {
  TEST_ASSERT_FALSE(useTestEndpoints());
  TEST_ASSERT_FALSE(useTest3Endpoints());
  TEST_ASSERT_FALSE(useLocalServerEndpoints());
}

// --- Web : pas de macro debug/async -> endpoints dangereux désactivés,
//     serveur async actif (asyncWebServerDisabled() faux). ---
void test_no_web_macros_all_false(void) {
  TEST_ASSERT_FALSE(dangerousEndpointsEnabled());
  TEST_ASSERT_FALSE(asyncWebServerDisabled());
}

// --- constexpr : utilisable en contexte constant (if constexpr / static_assert).
//     On vérifie l'évaluation à la compilation (pas seulement à l'exécution). ---
void test_predicates_are_constexpr(void) {
  static_assert(!isS3(), "isS3 doit etre constexpr-false sans BOARD_S3");
  static_assert(!isProd(), "isProd doit etre constexpr-false sans PROFILE_PROD");
  static_assert(isNonProd(), "isNonProd doit etre constexpr-true sans PROFILE_PROD");
  static_assert(!useTestEndpoints(), "useTestEndpoints constexpr-false par defaut");
  static_assert(!dangerousEndpointsEnabled(), "endpoints dangereux constexpr-false par defaut");
  constexpr bool s3 = isS3();
  TEST_ASSERT_FALSE(s3);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_no_board_macros_all_false);
  RUN_TEST(test_no_profile_macros_all_false);
  RUN_TEST(test_isNonProd_true_when_not_prod);
  RUN_TEST(test_no_endpoint_macros_all_false);
  RUN_TEST(test_no_web_macros_all_false);
  RUN_TEST(test_predicates_are_constexpr);
  return UNITY_END();
}
