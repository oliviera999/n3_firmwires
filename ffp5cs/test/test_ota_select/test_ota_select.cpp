#include <unity.h>

// Test natif (hôte) de la sélection d'artefact OTA depuis le manifeste JSON.
//
// CONTRAT (audit S3) : le firmware choisit le binaire à flasher via la cascade
//   channels[env][model] → channels[env][default] → channels[prod][model]
//   → channels[prod][default] → fallback legacy top-level.
// Le schéma du manifeste est figé côté serveur (n3_serveur ota/metadata.json).
// Cette logique pure est extraite de OTAManager::selectArtifactFromMetadata
// (ota_manager_validate.cpp) dans ota_artifact_select.h pour être testable sans
// matériel ni réseau. Jusqu'ici NON testée.
//
// ArduinoJson est header-only et compatible hôte (ajouté aux lib_deps natifs).
#include <ArduinoJson.h>
#include "../../include/ota_artifact_select.h"

using namespace OtaArtifactSelect;

namespace {
struct Out {
  char ver[32];
  char url[256];
  int size;
  char md5[40];
};

// Désérialise `json` et lance la sélection. Renvoie le booléen de selectArtifact.
bool select(const char* json, const char* env, const char* model, Out& o) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, err.code(), "JSON de test invalide");
  o.ver[0] = o.url[0] = o.md5[0] = '\0';
  o.size = 0;
  return selectArtifact(doc, env, model,
                        o.ver, sizeof(o.ver), o.url, sizeof(o.url),
                        o.size, o.md5, sizeof(o.md5));
}

// Idem pour l'image filesystem (champs filesystem_*, pas de version).
bool selectFs(const char* json, const char* env, const char* model, Out& o) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);
  TEST_ASSERT_EQUAL_MESSAGE(DeserializationError::Ok, err.code(), "JSON de test invalide");
  o.ver[0] = o.url[0] = o.md5[0] = '\0';
  o.size = 0;
  return selectFilesystem(doc, env, model,
                          o.url, sizeof(o.url), o.size, o.md5, sizeof(o.md5));
}
}  // namespace

void setUp(void) {}
void tearDown(void) {}

// 1) channels[env][model] : sélection directe avec tous les champs.
void test_channel_env_model_direct(void) {
  Out o;
  bool r = select(R"({"channels":{"test":{"esp32-wroom":{"bin_url":"u1","version":"1.0","size":10,"md5":"m1"}}}})",
                  "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("u1", o.url);
  TEST_ASSERT_EQUAL_STRING("1.0", o.ver);
  TEST_ASSERT_EQUAL_INT(10, o.size);
  TEST_ASSERT_EQUAL_STRING("m1", o.md5);
}

// 2) Repli sur channels[env][default] quand le modèle exact est absent.
void test_fallback_env_default(void) {
  Out o;
  bool r = select(R"({"channels":{"test":{"default":{"bin_url":"u2","version":"2.0"}}}})",
                  "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("u2", o.url);
  TEST_ASSERT_EQUAL_STRING("2.0", o.ver);
}

// 3) Repli sur channels[prod][model] quand l'env demandé est absent.
void test_fallback_prod_model(void) {
  Out o;
  bool r = select(R"({"channels":{"prod":{"esp32-s3":{"bin_url":"u3","version":"3.0"}}}})",
                  "test", "esp32-s3", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("u3", o.url);
}

// 4) Repli sur channels[prod][default] ; en mode channel la version est optionnelle.
void test_fallback_prod_default_version_optional(void) {
  Out o;
  bool r = select(R"({"channels":{"prod":{"default":{"bin_url":"u4"}}}})",
                  "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("u4", o.url);
  TEST_ASSERT_EQUAL_STRING("", o.ver);  // version absente tolérée dans la cascade channel
}

// 5) Fallback legacy top-level : exige URL ET version.
void test_legacy_top_level(void) {
  Out o;
  bool r = select(R"({"bin_url":"u5","version":"5.0","size":99,"md5":"m5"})",
                  "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("u5", o.url);
  TEST_ASSERT_EQUAL_STRING("5.0", o.ver);
  TEST_ASSERT_EQUAL_INT(99, o.size);
}

// 5b) Legacy top-level sans version → refusé (contrairement au mode channel).
void test_legacy_top_level_requires_version(void) {
  Out o;
  bool r = select(R"({"bin_url":"u5b"})", "test", "esp32-wroom", o);
  TEST_ASSERT_FALSE(r);
}

// 6) Nœud channel présent mais sans URL → complète depuis le top-level
//    (URL+version top-level, mais size pris dans le nœud).
void test_channel_node_fills_from_top_level(void) {
  Out o;
  bool r = select(R"({"bin_url":"top","version":"9","channels":{"test":{"esp32-wroom":{"size":5}}}})",
                  "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("top", o.url);
  TEST_ASSERT_EQUAL_STRING("9", o.ver);
  TEST_ASSERT_EQUAL_INT(5, o.size);
}

// 7) Manifeste vide → aucune sélection.
void test_empty_doc_returns_false(void) {
  Out o;
  TEST_ASSERT_FALSE(select(R"({})", "test", "esp32-wroom", o));
}

// 8) channels présent mais aucun nœud exploitable et pas de top-level → false.
void test_channels_present_but_unusable(void) {
  Out o;
  bool r = select(R"({"channels":{"prod":{"default":{"size":3}}}})", "test", "esp32-wroom", o);
  TEST_ASSERT_FALSE(r);
}

// --- Image filesystem (champs filesystem_*, pas de version) ---

// FS : sélection directe channels[env][model].
void test_fs_channel_env_model_direct(void) {
  Out o;
  bool r = selectFs(R"({"channels":{"test":{"esp32-wroom":{"filesystem_url":"f1","filesystem_size":7,"filesystem_md5":"x1"}}}})",
                    "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("f1", o.url);
  TEST_ASSERT_EQUAL_INT(7, o.size);
  TEST_ASSERT_EQUAL_STRING("x1", o.md5);
}

// FS : repli channels[prod][default], taille absente → 0.
void test_fs_fallback_prod_default(void) {
  Out o;
  bool r = selectFs(R"({"channels":{"prod":{"default":{"filesystem_url":"f4"}}}})",
                    "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("f4", o.url);
  TEST_ASSERT_EQUAL_INT(0, o.size);
}

// FS : fallback legacy top-level — exige seulement une URL (pas de version).
void test_fs_legacy_top_level_url_only(void) {
  Out o;
  bool r = selectFs(R"({"filesystem_url":"ftop","filesystem_size":12})", "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("ftop", o.url);
  TEST_ASSERT_EQUAL_INT(12, o.size);
}

// FS : manifeste avec firmware mais sans image filesystem → false (FS optionnel).
void test_fs_absent_returns_false(void) {
  Out o;
  TEST_ASSERT_FALSE(selectFs(R"({"bin_url":"u","version":"1"})", "test", "esp32-wroom", o));
}

// FS : nœud channel sans URL → complète l'URL depuis le top-level, garde la taille du nœud.
void test_fs_node_fills_from_top_level(void) {
  Out o;
  bool r = selectFs(R"({"filesystem_url":"ftop","channels":{"test":{"esp32-wroom":{"filesystem_size":3}}}})",
                    "test", "esp32-wroom", o);
  TEST_ASSERT_TRUE(r);
  TEST_ASSERT_EQUAL_STRING("ftop", o.url);
  TEST_ASSERT_EQUAL_INT(3, o.size);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_channel_env_model_direct);
  RUN_TEST(test_fallback_env_default);
  RUN_TEST(test_fallback_prod_model);
  RUN_TEST(test_fallback_prod_default_version_optional);
  RUN_TEST(test_legacy_top_level);
  RUN_TEST(test_legacy_top_level_requires_version);
  RUN_TEST(test_channel_node_fills_from_top_level);
  RUN_TEST(test_empty_doc_returns_false);
  RUN_TEST(test_channels_present_but_unusable);
  RUN_TEST(test_fs_channel_env_model_direct);
  RUN_TEST(test_fs_fallback_prod_default);
  RUN_TEST(test_fs_legacy_top_level_url_only);
  RUN_TEST(test_fs_absent_returns_false);
  RUN_TEST(test_fs_node_fills_from_top_level);
  return UNITY_END();
}
