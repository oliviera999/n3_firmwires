#include <unity.h>

// Test natif (hôte) du corps POST canonique full-update (Ffp3PostBody).
//
// CONTRAT CRITIQUE (audit S1/S2) : ce corps est signé HMAC par le firmware puis
// reconstruit côté serveur par App\Security\Ffp3HmacPostBody::buildFromParams()
// (n3_serveur) pour vérifier la signature. L'ordre des champs et le format DOIVENT
// rester strictement identiques des deux côtés, sinon → 401 HMAC.
//
// Ordre serveur de référence (Ffp3HmacPostBody.php) :
//   FULL_UPDATE_KEYS (ordre fixe) → extras triés alphabétiquement → SUFFIX_KEYS
//   valeurs vides ('') omises, paires « key=value » jointes par « & ».
//
// Le .cpp testé n'a aucune dépendance Arduino (cstdio/cstring) : inclusion directe.
#include "../../src/ffp3_post_body.cpp"

using namespace Ffp3PostBody;

void setUp(void) {}
void tearDown(void) {}

// Construit un jeu de valeurs minimal mais représentatif.
static void seedBasic(FullUpdateValues& v) {
  snprintf(v.apiKey, sizeof(v.apiKey), "%s", "K");
  snprintf(v.sensor, sizeof(v.sensor), "%s", "ffp3");
  snprintf(v.version, sizeof(v.version), "%s", "14.02");
  snprintf(v.tempAir, sizeof(v.tempAir), "%s", "21.5");
  snprintf(v.bouffeMatin, sizeof(v.bouffeMatin), "%s", "8");
  snprintf(v.mail, sizeof(v.mail), "%s", "a@b.c");
  snprintf(v.resetMode, sizeof(v.resetMode), "%s", "0");
  snprintf(v.postId, sizeof(v.postId), "%s", "42");
}

// Ordre canonique : main (ordre fixe) → extras triés → suffix ; vides omis.
void test_canonical_order_and_skip_empty(void) {
  FullUpdateValues v{};
  seedBasic(v);
  applyExtraPairs(v, "zeb=2&alpha=1");  // volontairement non triés

  char out[512] = {0};
  int n = buildFullUpdateBody(out, sizeof(out), v);

  const char* expected =
      "api_key=K&sensor=ffp3&version=14.02&TempAir=21.5&bouffeMatin=8&mail=a@b.c"
      "&alpha=1&zeb=2&resetMode=0&post_id=42";
  TEST_ASSERT_EQUAL_STRING(expected, out);
  TEST_ASSERT_EQUAL_INT((int)strlen(expected), n);  // longueur retournée = strlen(corps)
}

// Les extras inconnus sont triés alphabétiquement et insérés entre main et suffix.
void test_extras_sorted_alphabetically(void) {
  FullUpdateValues v{};
  snprintf(v.apiKey, sizeof(v.apiKey), "%s", "K");
  snprintf(v.resetMode, sizeof(v.resetMode), "%s", "1");
  applyExtraPairs(v, "gamma=g&alpha=a&beta=b");

  char out[256] = {0};
  buildFullUpdateBody(out, sizeof(out), v);
  TEST_ASSERT_EQUAL_STRING("api_key=K&alpha=a&beta=b&gamma=g&resetMode=1", out);
}

// applyExtraPairs : une clé CONNUE écrase le champ de la struct (pas dans extras),
// une clé INCONNUE est stockée dans extras.
void test_apply_known_vs_unknown(void) {
  FullUpdateValues v{};
  applyExtraPairs(v, "TempEau=19.9&foo=bar");

  TEST_ASSERT_EQUAL_STRING("19.9", v.tempEau);       // champ connu mis à jour
  TEST_ASSERT_EQUAL_UINT8(1, v.extras.count);        // un seul extra
  TEST_ASSERT_EQUAL_STRING("foo", v.extras.pairs[0].key);
  TEST_ASSERT_EQUAL_STRING("bar", v.extras.pairs[0].value);
}

// upsert : ré-affecter le même extra met à jour la valeur sans dupliquer.
void test_extra_upsert_no_duplicate(void) {
  FullUpdateValues v{};
  applyExtraPairs(v, "foo=1");
  applyExtraPairs(v, "foo=2");
  TEST_ASSERT_EQUAL_UINT8(1, v.extras.count);
  TEST_ASSERT_EQUAL_STRING("2", v.extras.pairs[0].value);
}

// Buffer trop petit : retour -1, pas de débordement.
void test_buffer_too_small_returns_minus_one(void) {
  FullUpdateValues v{};
  seedBasic(v);
  char small[10] = {0};
  TEST_ASSERT_EQUAL_INT(-1, buildFullUpdateBody(small, sizeof(small), v));
}

// Cas dégénérés : out null / size 0.
void test_invalid_output_args(void) {
  FullUpdateValues v{};
  seedBasic(v);
  char out[64];
  TEST_ASSERT_EQUAL_INT(-1, buildFullUpdateBody(nullptr, sizeof(out), v));
  TEST_ASSERT_EQUAL_INT(-1, buildFullUpdateBody(out, 0, v));
}

// Aucune valeur → corps vide de longueur 0 (et non -1).
void test_all_empty_yields_empty_body(void) {
  FullUpdateValues v{};
  char out[64] = {"sentinelle"};
  int n = buildFullUpdateBody(out, sizeof(out), v);
  TEST_ASSERT_EQUAL_INT(0, n);
  TEST_ASSERT_EQUAL_STRING("", out);
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_canonical_order_and_skip_empty);
  RUN_TEST(test_extras_sorted_alphabetically);
  RUN_TEST(test_apply_known_vs_unknown);
  RUN_TEST(test_extra_upsert_no_duplicate);
  RUN_TEST(test_buffer_too_small_returns_minus_one);
  RUN_TEST(test_invalid_output_args);
  RUN_TEST(test_all_empty_yields_empty_body);
  return UNITY_END();
}
