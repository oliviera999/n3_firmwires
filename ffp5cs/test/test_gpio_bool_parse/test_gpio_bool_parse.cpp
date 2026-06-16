#include <unity.h>

// Tests de GpioBoolParse (include/gpio_bool_parse.h) : cœur SANS ArduinoJson de
// parseBoolFromDoc (gpio_parser.cpp). Parse les commandes serveur distant ; tout
// écart de table de vérité = GPIO actionné à tort (pompe/chauffage/reset) ->
// parité byte-identique exigée (chantier no-bench).
//
// Table de vérité de l'original (case-insensitive, comme strcasecmp) :
//   "1" / "true" / "on"   -> reconnu, true
//   "0" / "false"         -> reconnu, false
//   inconnu / vide / null -> NON reconnu (l'appelant retombe sur false)
//   int == 1              -> true ; sinon (0, 2, -1, ...) -> false
#include "../../include/gpio_bool_parse.h"

using namespace GpioBoolParse;

void setUp(void) {}
void tearDown(void) {}

// --- fromToken : tokens "vrai" reconnus (true), insensibles à la casse ---
void test_token_true_minuscule(void) {
  bool out = false;
  TEST_ASSERT_TRUE(fromToken("1", out));    TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("true", out)); TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("on", out));   TEST_ASSERT_TRUE(out);
}

void test_token_true_casse_variee(void) {
  bool out = false;
  TEST_ASSERT_TRUE(fromToken("TRUE", out)); TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("True", out)); TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("tRuE", out)); TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("ON", out));   TEST_ASSERT_TRUE(out);
  out = false;
  TEST_ASSERT_TRUE(fromToken("On", out));   TEST_ASSERT_TRUE(out);
}

// --- fromToken : tokens "faux" reconnus (false), insensibles à la casse ---
void test_token_false_minuscule(void) {
  bool out = true;
  TEST_ASSERT_TRUE(fromToken("0", out));     TEST_ASSERT_FALSE(out);
  out = true;
  TEST_ASSERT_TRUE(fromToken("false", out)); TEST_ASSERT_FALSE(out);
}

void test_token_false_casse_variee(void) {
  bool out = true;
  TEST_ASSERT_TRUE(fromToken("FALSE", out)); TEST_ASSERT_FALSE(out);
  out = true;
  TEST_ASSERT_TRUE(fromToken("False", out)); TEST_ASSERT_FALSE(out);
  out = true;
  TEST_ASSERT_TRUE(fromToken("fAlSe", out)); TEST_ASSERT_FALSE(out);
}

// --- fromToken : tokens NON reconnus -> retourne false, out inchangé ---
void test_token_inconnu_non_reconnu(void) {
  // "yes"/"off"/"checked" ne sont PAS dans la table de parseBoolFromDoc.
  bool out = false;
  TEST_ASSERT_FALSE(fromToken("yes", out));
  TEST_ASSERT_FALSE(fromToken("off", out));
  TEST_ASSERT_FALSE(fromToken("checked", out));
  TEST_ASSERT_FALSE(fromToken("2", out));
  TEST_ASSERT_FALSE(fromToken("truex", out));
  TEST_ASSERT_FALSE(fromToken("foo", out));
}

void test_token_vide_non_reconnu(void) {
  bool out = false;
  TEST_ASSERT_FALSE(fromToken("", out));
}

void test_token_null_non_reconnu(void) {
  bool out = false;
  TEST_ASSERT_FALSE(fromToken(nullptr, out));
}

// out NON modifié quand le token n'est pas reconnu (les deux polarités).
void test_token_non_reconnu_preserve_out(void) {
  bool out_true = true;
  TEST_ASSERT_FALSE(fromToken("bogus", out_true));
  TEST_ASSERT_TRUE(out_true);   // inchangé
  bool out_false = false;
  TEST_ASSERT_FALSE(fromToken("bogus", out_false));
  TEST_ASSERT_FALSE(out_false); // inchangé
}

// --- fromInt : strictement == 1 ---
void test_int_un_vrai(void) {
  TEST_ASSERT_TRUE(fromInt(1));
}

void test_int_zero_faux(void) {
  TEST_ASSERT_FALSE(fromInt(0));
}

void test_int_autres_non_un_faux(void) {
  // Règle de l'original = "== 1", PAS "nonzero" : 2 et -1 -> false.
  TEST_ASSERT_FALSE(fromInt(2));
  TEST_ASSERT_FALSE(fromInt(-1));
  TEST_ASSERT_FALSE(fromInt(42));
  TEST_ASSERT_FALSE(fromInt(100));
}

// --- Parité exhaustive avec l'ancienne logique inline de parseBoolFromDoc ---
// Reproduit le comportement combiné dispatch+conversion pour le cas string :
// résultat effectif renvoyé par parseBoolFromDoc pour un token donné.
static bool oldStringBranch(const char* s) {
  // Réplique exacte des deux `if (s && ...)` puis du `return false` terminal.
  if (s && (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "on") == 0)) return true;
  if (s && (strcasecmp(s, "0") == 0 || strcasecmp(s, "false") == 0)) return false;
  return false;
}

// Nouveau chemin : ce que renvoie parseBoolFromDoc dans la branche string.
static bool newStringBranch(const char* s) {
  bool out = false;
  if (fromToken(s, out)) return out;
  return false;  // défaut terminal de parseBoolFromDoc
}

void test_parite_branche_string(void) {
  const char* cases[] = {
    "1", "true", "on", "TRUE", "On",
    "0", "false", "FALSE",
    "yes", "off", "checked", "2", "", "truex", "foo",
  };
  for (size_t i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
    TEST_ASSERT_EQUAL(oldStringBranch(cases[i]), newStringBranch(cases[i]));
  }
  // nullptr aussi (les deux -> false).
  TEST_ASSERT_EQUAL(oldStringBranch(nullptr), newStringBranch(nullptr));
}

void test_parite_branche_int(void) {
  for (int v = -5; v <= 5; ++v) {
    bool old = (v == 1);  // règle inline de parseBoolFromDoc
    TEST_ASSERT_EQUAL(old, fromInt(v));
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_token_true_minuscule);
  RUN_TEST(test_token_true_casse_variee);
  RUN_TEST(test_token_false_minuscule);
  RUN_TEST(test_token_false_casse_variee);
  RUN_TEST(test_token_inconnu_non_reconnu);
  RUN_TEST(test_token_vide_non_reconnu);
  RUN_TEST(test_token_null_non_reconnu);
  RUN_TEST(test_token_non_reconnu_preserve_out);
  RUN_TEST(test_int_un_vrai);
  RUN_TEST(test_int_zero_faux);
  RUN_TEST(test_int_autres_non_un_faux);
  RUN_TEST(test_parite_branche_string);
  RUN_TEST(test_parite_branche_int);
  return UNITY_END();
}
