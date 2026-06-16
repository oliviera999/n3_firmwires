#include <unity.h>

// Pendant « branches true » de BoardTraits (board_traits.h) : on DÉFINIT un
// jeu représentatif de macros carte/profil/endpoints AVANT d'inclure le vrai
// header de prod, et on vérifie que chaque prédicat gardé bascule à true — et,
// crucial, que les prédicats sont INDÉPENDANTS (BOARD_S3 défini n'allume pas
// isWroom(), PROFILE_BETA n'allume pas isProd(), etc.). Le cas « tout faux »
// est dans test_board_traits_default/.
//
// Combinaison choisie = carte S3 PSRAM en profil BETA avec endpoints de test,
// endpoints debug activés et serveur async désactivé : touche une branche `true`
// de chaque helper gardé tout en laissant les helpers voisins à false.
#define BOARD_S3
#define BOARD_HAS_PSRAM
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS
#define FFP_ENABLE_DANGEROUS_ENDPOINTS
#define DISABLE_ASYNC_WEBSERVER

#include "../../include/board_traits.h"

using namespace BoardTraits;

void setUp(void) {}
void tearDown(void) {}

// --- Carte : BOARD_S3 + PSRAM définis, BOARD_WROOM non -> S3 true, WROOM false ---
void test_board_s3_psram_selected_wroom_false(void) {
  TEST_ASSERT_TRUE(isS3());
  TEST_ASSERT_TRUE(hasPsram());
  TEST_ASSERT_FALSE(isWroom());  // indépendance : pas de BOARD_WROOM
}

// --- Profil : PROFILE_BETA seul -> isBeta true ; prod/test/dev restent faux ---
void test_profile_beta_only(void) {
  TEST_ASSERT_TRUE(isBeta());
  TEST_ASSERT_FALSE(isProd());
  TEST_ASSERT_FALSE(isTest());
  TEST_ASSERT_FALSE(isDev());
}

// --- isNonProd : en BETA (donc pas prod) -> true, et == !isProd() ---
void test_isNonProd_in_beta(void) {
  TEST_ASSERT_TRUE(isNonProd());
  TEST_ASSERT_EQUAL(!isProd(), isNonProd());
}

// --- Endpoints : USE_TEST_ENDPOINTS défini -> test true ; test3/local non
//     définis -> false (indépendance des trois familles d'endpoints). ---
void test_endpoints_test_only(void) {
  TEST_ASSERT_TRUE(useTestEndpoints());
  TEST_ASSERT_FALSE(useTest3Endpoints());
  TEST_ASSERT_FALSE(useLocalServerEndpoints());
}

// --- Web : les deux macros web sont définies -> les deux helpers true ---
void test_web_flags_true(void) {
  TEST_ASSERT_TRUE(dangerousEndpointsEnabled());
  TEST_ASSERT_TRUE(asyncWebServerDisabled());
}

// --- Évaluation constexpr des branches true (compile-time) ---
void test_true_branches_are_constexpr(void) {
  static_assert(isS3(), "BOARD_S3 -> isS3() constexpr-true");
  static_assert(hasPsram(), "BOARD_HAS_PSRAM -> hasPsram() constexpr-true");
  static_assert(isBeta(), "PROFILE_BETA -> isBeta() constexpr-true");
  static_assert(!isProd(), "pas de PROFILE_PROD -> isProd() constexpr-false");
  static_assert(isNonProd(), "BETA -> isNonProd() constexpr-true");
  static_assert(useTestEndpoints(), "USE_TEST_ENDPOINTS -> constexpr-true");
  static_assert(asyncWebServerDisabled(), "DISABLE_ASYNC_WEBSERVER -> constexpr-true");
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_board_s3_psram_selected_wroom_false);
  RUN_TEST(test_profile_beta_only);
  RUN_TEST(test_isNonProd_in_beta);
  RUN_TEST(test_endpoints_test_only);
  RUN_TEST(test_web_flags_true);
  RUN_TEST(test_true_branches_are_constexpr);
  return UNITY_END();
}
