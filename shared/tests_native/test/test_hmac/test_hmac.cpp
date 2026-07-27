// Tests Unity natifs pour n3_hmac — CONTRAT DU WRAPPER (pas la crypto).
//
//   pio test -c platformio-native.ini -e native -f test_hmac
//
// mbedtls et HTTPClient sont stubés (indisponibles en natif) : mbedtls renvoie
// un digest déterministe 00..1f. On valide donc le formatage hex (64 car.
// minuscules + NUL), le garde-fou de taille de buffer, et l'attache du header
// X-Signature — c'est exactement ce que la modif snprintf (v…) a touché.
// La justesse HMAC-SHA256 elle-même est validée sur cible (vrai mbedtls).

#include <Arduino.h>      // mock
#include <unity.h>
#include "n3_hmac.cpp"    // impl (mbedtls/md.h + HTTPClient.h résolus via -I stubs)

// Digest stub = octets 0x00..0x1f → hex attendu :
static const char* EXPECTED_HEX =
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

void setUp() {}
void tearDown() {}

void test_hex_format_64_minuscules() {
  char out[65];
  bool ok = n3HmacSha256("key", "message", out, sizeof(out));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING(EXPECTED_HEX, out);
}

void test_null_terminaison() {
  char out[65];
  n3HmacSha256("k", "m", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT('\0', out[64]); // NUL en position 64
}

void test_buffer_trop_petit_rejete() {
  char out[64];                          // < 65
  TEST_ASSERT_FALSE(n3HmacSha256("k", "m", out, sizeof(out)));
}

// Gardes nulles (audit 2026-07, F5) : avant le correctif, strlen(nullptr)
// plantait. La fonction est exportee dans l'en-tete public sans precondition,
// et son module frere computeHmacHex valide deja ses parametres.
void test_pointeurs_nuls_rejetes_sans_crash() {
  char out[65];
  TEST_ASSERT_FALSE(n3HmacSha256(nullptr, "m", out, sizeof(out)));
  TEST_ASSERT_FALSE(n3HmacSha256("k", nullptr, out, sizeof(out)));
  TEST_ASSERT_FALSE(n3HmacSha256("k", "m", nullptr, 65));
}

// Un echec de calcul ne doit poser AUCUN header (sinon on enverrait une
// signature vide ou du contenu de pile).
void test_sign_request_sans_body_ne_pose_pas_de_header() {
  HTTPClient http;
  n3HmacSignRequest(http, "apikey", nullptr);
  TEST_ASSERT_EQUAL_STRING("", http.lastHeaderName.c_str());
}

void test_sign_request_pose_header_x_signature() {
  HTTPClient http;
  n3HmacSignRequest(http, "apikey", "body");
  TEST_ASSERT_EQUAL_STRING("X-Signature", http.lastHeaderName.c_str());
  TEST_ASSERT_EQUAL_STRING(EXPECTED_HEX, http.lastHeaderValue.c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_hex_format_64_minuscules);
  RUN_TEST(test_null_terminaison);
  RUN_TEST(test_buffer_trop_petit_rejete);
  RUN_TEST(test_pointeurs_nuls_rejetes_sans_crash);
  RUN_TEST(test_sign_request_sans_body_ne_pose_pas_de_header);
  RUN_TEST(test_sign_request_pose_header_x_signature);
  return UNITY_END();
}
