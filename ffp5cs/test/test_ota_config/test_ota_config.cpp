// Profil de test (résout ServerConfig/secrets sans static_assert PROD).
#define PROFILE_BETA
#define USE_TEST_ENDPOINTS

#include <unity.h>
#include <cstring>

// Premier test natif qui inclut la vraie config.h, débloquée par le stub
// FreeRTOS de test/unit/freertos/ (+ isnan dans le mock Arduino.h). On valide
// la logique de NORMALISATION d'URL OTA de ota_config.h sans figer l'URL
// serveur (dépendante du profil) : on teste les INVARIANTS de construction.
#include "ota_config.h"

void setUp(void) {}
void tearDown(void) {}

// Aucune occurrence de "//" après le schéma "://".
static bool noDoubleSlashAfterScheme(const char* url) {
  const char* scheme = strstr(url, "://");
  const char* rest = scheme ? scheme + 3 : url;
  return strstr(rest, "//") == nullptr;
}

static bool endsWith(const char* s, const char* suffix) {
  size_t ls = strlen(s), lf = strlen(suffix);
  return ls >= lf && strcmp(s + (ls - lf), suffix) == 0;
}

void test_folder_is_wroom_on_native_build(void) {
  // Build natif = pas de BOARD_S3 -> dossier WROOM, terminé par '/'.
  const char* folder = OTAConfig::getOTAFolder();
  TEST_ASSERT_EQUAL_STRING("esp32-wroom/", folder);
  TEST_ASSERT_EQUAL_CHAR('/', folder[strlen(folder) - 1]);
}

void test_base_url_well_formed(void) {
  char base[200] = {0};
  OTAConfig::getOTABaseUrl(base, sizeof(base));
  TEST_ASSERT_TRUE(strlen(base) > 0);
  TEST_ASSERT_EQUAL_INT(0, strncmp(base, "http", 4));      // schéma présent
  TEST_ASSERT_TRUE(endsWith(base, OTAConfig::getOTAFolder()));
  TEST_ASSERT_TRUE(noDoubleSlashAfterScheme(base));        // normalisation OK
}

void test_metadata_url_well_formed(void) {
  char meta[200] = {0};
  OTAConfig::getMetadataUrl(meta, sizeof(meta));
  TEST_ASSERT_TRUE(strlen(meta) > 0);
  TEST_ASSERT_EQUAL_INT(0, strncmp(meta, "http", 4));
  TEST_ASSERT_TRUE(endsWith(meta, "metadata.json"));
  TEST_ASSERT_TRUE(noDoubleSlashAfterScheme(meta));
}

void test_base_and_metadata_share_normalized_prefix(void) {
  // base = PREFIX + folder ; meta = PREFIX + "metadata.json".
  // Les deux fonctions doivent produire le MÊME préfixe normalisé.
  char base[200] = {0}, meta[200] = {0};
  OTAConfig::getOTABaseUrl(base, sizeof(base));
  OTAConfig::getMetadataUrl(meta, sizeof(meta));

  size_t prefBase = strlen(base) - strlen(OTAConfig::getOTAFolder());
  size_t prefMeta = strlen(meta) - strlen("metadata.json");
  TEST_ASSERT_EQUAL_size_t(prefMeta, prefBase);
  TEST_ASSERT_EQUAL_INT(0, strncmp(base, meta, prefBase));
}

void test_truncation_no_overflow(void) {
  char tiny[8];
  memset(tiny, '#', sizeof(tiny));
  OTAConfig::getMetadataUrl(tiny, sizeof(tiny));
  TEST_ASSERT_TRUE(strlen(tiny) < sizeof(tiny));   // terminé, pas de débordement
}

void test_guards_null_and_zero_size(void) {
  // Ne doit pas crasher ; size 0 ne touche pas le buffer.
  OTAConfig::getMetadataUrl(nullptr, 16);
  char buf[16] = "untouched";
  OTAConfig::getOTABaseUrl(buf, 0);
  TEST_ASSERT_EQUAL_STRING("untouched", buf);
  TEST_ASSERT_TRUE(true);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_folder_is_wroom_on_native_build);
  RUN_TEST(test_base_url_well_formed);
  RUN_TEST(test_metadata_url_well_formed);
  RUN_TEST(test_base_and_metadata_share_normalized_prefix);
  RUN_TEST(test_truncation_no_overflow);
  RUN_TEST(test_guards_null_and_zero_size);
  return UNITY_END();
}
