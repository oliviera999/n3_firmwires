// Tests Unity natifs pour n3_hmac_canonical — CONTRAT (pas la crypto).
//
//   pio test -c platformio-native.ini -e native -f test_hmac_canonical
//
// mbedtls et esp_random sont stubés (indisponibles en natif) : mbedtls renvoie un
// digest déterministe 00..1f, esp_fill_random remplit de zéros. On valide donc le
// formatage hex (64 car. minuscules + NUL), les garde-fous de paramètres, et le
// format du nonce (16 hex + NUL). La justesse HMAC-SHA256 et l'entropie du nonce
// sont validées sur cible (vrai mbedtls + HW RNG).

#include <unity.h>
#include <cstring>
#include "n3_hmac_canonical.cpp"  // impl (mbedtls/md.h + esp_random.h via -I stubs)

using namespace n3hmac;

// Digest stub = octets 0x00..0x1f -> hex attendu :
static const char* EXPECTED_HEX =
    "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f";

void setUp() {}
void tearDown() {}

// ---- computeHmacHex : format & garde-fous ----------------------------------

void test_hex_format_64_minuscules() {
  char out[65];
  bool ok = computeHmacHex("secret", "1700000000", "abc123", "field=1", out, sizeof(out));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING(EXPECTED_HEX, out);
}

void test_null_terminaison() {
  char out[65];
  computeHmacHex("s", "1", "n", "b", out, sizeof(out));
  TEST_ASSERT_EQUAL_INT('\0', out[64]);  // NUL en position 64
}

void test_buffer_trop_petit_rejete() {
  char out[64];  // < 65
  TEST_ASSERT_FALSE(computeHmacHex("s", "1", "n", "b", out, sizeof(out)));
}

void test_secret_nul_ou_vide_rejete() {
  char out[65];
  TEST_ASSERT_FALSE(computeHmacHex(nullptr, "1", "n", "b", out, sizeof(out)));
  TEST_ASSERT_FALSE(computeHmacHex("", "1", "n", "b", out, sizeof(out)));
}

void test_timestamp_ou_nonce_nul_rejete() {
  char out[65];
  TEST_ASSERT_FALSE(computeHmacHex("s", nullptr, "n", "b", out, sizeof(out)));
  TEST_ASSERT_FALSE(computeHmacHex("s", "1", nullptr, "b", out, sizeof(out)));
}

void test_out_nul_rejete() {
  TEST_ASSERT_FALSE(computeHmacHex("s", "1", "n", "b", nullptr, 65));
}

void test_body_nul_accepte() {
  // body == nullptr est toléré (traité comme ""). Doit réussir et produire le digest.
  char out[65];
  bool ok = computeHmacHex("s", "1", "n", nullptr, out, sizeof(out));
  TEST_ASSERT_TRUE(ok);
  TEST_ASSERT_EQUAL_STRING(EXPECTED_HEX, out);
}

// ---- generateNonce : format ------------------------------------------------

void test_nonce_format_16_hex() {
  char nonce[17];
  generateNonce(nonce, sizeof(nonce));
  TEST_ASSERT_EQUAL_INT(16, (int)strlen(nonce));
  for (int i = 0; i < 16; ++i) {
    char c = nonce[i];
    bool isHex = (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
    TEST_ASSERT_TRUE(isHex);
  }
  TEST_ASSERT_EQUAL_INT('\0', nonce[16]);
}

void test_nonce_buffer_trop_petit_vide() {
  char nonce[8] = {'X', 'Y', 'Z', 0, 0, 0, 0, 0};
  generateNonce(nonce, sizeof(nonce));  // < 17 -> chaîne vide
  TEST_ASSERT_EQUAL_INT('\0', nonce[0]);
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_hex_format_64_minuscules);
  RUN_TEST(test_null_terminaison);
  RUN_TEST(test_buffer_trop_petit_rejete);
  RUN_TEST(test_secret_nul_ou_vide_rejete);
  RUN_TEST(test_timestamp_ou_nonce_nul_rejete);
  RUN_TEST(test_out_nul_rejete);
  RUN_TEST(test_body_nul_accepte);
  RUN_TEST(test_nonce_format_16_hex);
  RUN_TEST(test_nonce_buffer_trop_petit_vide);
  return UNITY_END();
}
