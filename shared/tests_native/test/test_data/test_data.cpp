// Tests Unity natifs pour n3_data — encodage URL pur (brique du corps
// application/x-www-form-urlencoded envoye par n3DataPost).
//
//   pio test -c platformio-native.ini -e native -f test_data
//
// n3UrlEncode est l'helper file-local (static) qui encode chaque name/value du
// body POST. On l'atteint en incluant directement n3_data.cpp (meme idiome que
// test_analog/test_hmac : TU isolee). WiFi/HTTPClient/mbedtls sont stubes
// (indisponibles en natif) ; ils ne sont PAS exerces ici — on ne teste que la
// logique pure d'encodage, partagee par n3pp/msp/ffp5cs.
//
// Regle d'encodage (RFC 3986 unreserved) : a-z A-Z 0-9 - _ . ~ passent tels
// quels ; tout le reste -> %HH en hexa MAJUSCULE.

#include <Arduino.h>          // mock
#include <unity.h>
#include "n3_data.cpp"        // impl (WiFi.h / HTTPClient.h résolus via -I stubs)
#include "n3_hmac.cpp"        // fournit n3HmacSha256/n3HmacSignRequest référencés par n3_data.cpp
#include "n3_net_stats.cpp"   // fournit n3NetStatsRecordPost/Get référencés par n3_data.cpp

void setUp() {}
void tearDown() {}

void test_caracteres_non_reserves_inchanges() {
  // Lettres, chiffres et - _ . ~ doivent passer tels quels.
  TEST_ASSERT_EQUAL_STRING("AZaz09-_.~", n3UrlEncode(String("AZaz09-_.~")).c_str());
}

void test_espace_encode_en_pourcent_20() {
  TEST_ASSERT_EQUAL_STRING("a%20b", n3UrlEncode(String("a b")).c_str());
}

void test_caracteres_speciaux_form_urlencoded() {
  // & = + ? / : sont reserves -> %HH.
  TEST_ASSERT_EQUAL_STRING("a%26b%3Dc", n3UrlEncode(String("a&b=c")).c_str()); // & =
  TEST_ASSERT_EQUAL_STRING("a%2Bb", n3UrlEncode(String("a+b")).c_str());       // +
  TEST_ASSERT_EQUAL_STRING("k%3Dv%26x", n3UrlEncode(String("k=v&x")).c_str()); // = &
}

void test_hexa_majuscule() {
  // '/' = 0x2F -> "%2F" (et non "%2f").
  TEST_ASSERT_EQUAL_STRING("%2F", n3UrlEncode(String("/")).c_str());
  // '#' = 0x23 -> "%23".
  TEST_ASSERT_EQUAL_STRING("%23", n3UrlEncode(String("#")).c_str());
}

void test_chaine_vide() {
  TEST_ASSERT_EQUAL_STRING("", n3UrlEncode(String("")).c_str());
}

void test_octet_haut_encode() {
  // Octet >= 0x80 (ex. 0xE9 = 'é' Latin-1) -> "%E9".
  char raw[2] = { (char)0xE9, '\0' };
  TEST_ASSERT_EQUAL_STRING("%E9", n3UrlEncode(String(raw)).c_str());
}

int main(int argc, char** argv) {
  (void)argc;
  (void)argv;
  UNITY_BEGIN();
  RUN_TEST(test_caracteres_non_reserves_inchanges);
  RUN_TEST(test_espace_encode_en_pourcent_20);
  RUN_TEST(test_caracteres_speciaux_form_urlencoded);
  RUN_TEST(test_hexa_majuscule);
  RUN_TEST(test_chaine_vide);
  RUN_TEST(test_octet_haut_encode);
  return UNITY_END();
}
