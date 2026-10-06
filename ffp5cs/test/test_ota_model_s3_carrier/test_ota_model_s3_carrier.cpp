#include <unity.h>
#include <cstring>

// Identité OTA (include/ota_model.h, v15.31) pour UN jeu de macros de build —
// un dossier de test par combinaison (les macros sont figées à la compilation).
// Env wroom-s3-carrier-test (S3 sur carte porteuse bi-module) : clé et dossier propres,
// jamais l'image du S3 historique (OneWire 37 vs 41, strapping 45/3).
#define BOARD_S3
#define PROFILE_TEST
#define USE_TEST3_ENDPOINTS
#define PINMAP_S3_CARRIER

#include "../../include/ota_model.h"

void setUp(void) {}
void tearDown(void) {}

void test_env(void) {
  TEST_ASSERT_EQUAL_STRING("test", OtaModel::ENV);
}

void test_model(void) {
  TEST_ASSERT_EQUAL_STRING("esp32-s3-carrier", OtaModel::MODEL);
}

void test_metadata_subdir(void) {
  TEST_ASSERT_EQUAL_STRING("esp32-s3-carrier/", OtaModel::METADATA_SUBDIR);
  TEST_ASSERT_FALSE(OtaModel::isHistoricPinmap());
  // Câblage non historique : dossier = "<modele>/" (aucune collision avec le manifeste partagé).
  if (!OtaModel::isHistoricPinmap()) {
    const size_t n = strlen(OtaModel::MODEL);
    TEST_ASSERT_EQUAL_INT(0, strncmp(OtaModel::METADATA_SUBDIR, OtaModel::MODEL, n));
    TEST_ASSERT_EQUAL_STRING("/", OtaModel::METADATA_SUBDIR + n);
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_env);
  RUN_TEST(test_model);
  RUN_TEST(test_metadata_subdir);
  return UNITY_END();
}
