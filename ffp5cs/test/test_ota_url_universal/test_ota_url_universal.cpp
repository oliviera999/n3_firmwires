// Profil de test (résout ServerConfig/secrets sans static_assert PROD), carte
// WROOM sur n3-universal : même macros que l'env wroom-universal-test.
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS
#define PINMAP_UNIVERSAL

#include <unity.h>
#include <cstring>

// v15.31 — URL des métadonnées OTA d'un câblage NON historique : dossier propre
// <OTA_BASE_PATH>/esp32-wroom-universal/metadata.json, jamais le manifeste partagé
// des WROOM historiques (<OTA_BASE_PATH>/metadata.json, épinglé par test_ota_config).
#include "ota_config.h"

void setUp(void) {}
void tearDown(void) {}

static bool endsWith(const char* s, const char* suffix) {
  size_t ls = strlen(s), lf = strlen(suffix);
  return ls >= lf && strcmp(s + (ls - lf), suffix) == 0;
}

void test_metadata_url_uses_model_folder(void) {
  char meta[200] = {0};
  OTAConfig::getMetadataUrl(meta, sizeof(meta));
  TEST_ASSERT_TRUE(endsWith(meta, "/esp32-wroom-universal/metadata.json"));
  const char* scheme = strstr(meta, "://");
  TEST_ASSERT_NOT_NULL(scheme);
  TEST_ASSERT_NULL(strstr(scheme + 3, "//"));  // normalisation conservée
}

void test_same_prefix_as_historic_base(void) {
  // base = PREFIX + "esp32-wroom/" ; meta = PREFIX + "esp32-wroom-universal/metadata.json"
  char base[200] = {0}, meta[200] = {0};
  OTAConfig::getOTABaseUrl(base, sizeof(base));
  OTAConfig::getMetadataUrl(meta, sizeof(meta));
  size_t prefBase = strlen(base) - strlen(OTAConfig::getOTAFolder());
  size_t prefMeta = strlen(meta) - strlen("esp32-wroom-universal/metadata.json");
  TEST_ASSERT_EQUAL_size_t(prefBase, prefMeta);
  TEST_ASSERT_EQUAL_INT(0, strncmp(base, meta, prefBase));
}

void test_ota_identity(void) {
  TEST_ASSERT_EQUAL_STRING("test", OtaModel::ENV);
  TEST_ASSERT_EQUAL_STRING("esp32-wroom-universal", OtaModel::MODEL);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_metadata_url_uses_model_folder);
  RUN_TEST(test_same_prefix_as_historic_base);
  RUN_TEST(test_ota_identity);
  return UNITY_END();
}
