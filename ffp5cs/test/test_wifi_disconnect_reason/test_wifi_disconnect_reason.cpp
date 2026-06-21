#include <cstdint>  // types entiers AVANT tout en-tête (pitfall)
#include <unity.h>

// Tests de WifiDisconnectReason (wifi_disconnect_reason.h), extrait de
// wifi_manager.cpp (helper wifiDisconnectReasonStr du diagnostic affiché quand
// une connexion STA échoue). Logique pure : mapping d'un code de raison
// ESP-IDF (entier) vers un libellé constant, nullptr si inconnu.
// On vérifie aussi la PARITÉ avec une transcription du switch d'origine
// (brute-force sur une large plage d'entiers).
#include "../../include/wifi_disconnect_reason.h"

#include <cstring>

using namespace WifiDisconnectReason;

void setUp(void) {}
void tearDown(void) {}

// =============================================================================
// Transcription de référence du switch d'origine (wifi_manager.cpp avant
// extraction) — sert d'oracle pour la boucle de parité.
// =============================================================================
static const char* refToString(int reason) {
  switch (reason) {
    case 1:  return "UNSPECIFIED";
    case 2:  return "AUTH_EXPIRE";
    case 3:  return "AUTH_LEAVE";
    case 4:  return "ASSOC_EXPIRE";
    case 5:  return "ASSOC_TOOMANY";
    case 6:  return "NOT_AUTHED";
    case 7:  return "NOT_ASSOCED";
    case 8:  return "ASSOC_LEAVE";
    case 9:  return "ASSOC_NOT_AUTHED";
    case 15: return "4WAY_HANDSHAKE_TIMEOUT";
    case 201: return "NO_AP_FOUND";
    case 202: return "AUTH_FAIL";
    case 204: return "HANDSHAKE_TIMEOUT";
    case 205: return "CONNECTION_FAIL";
    default: return nullptr;
  }
}

// =============================================================================
// Cas nominaux — chaque code connu renvoie son libellé exact
// =============================================================================

void test_unspecified(void) {
  TEST_ASSERT_EQUAL_STRING("UNSPECIFIED", toString(1));
}

void test_auth_expire(void) {
  TEST_ASSERT_EQUAL_STRING("AUTH_EXPIRE", toString(2));
}

void test_auth_leave(void) {
  TEST_ASSERT_EQUAL_STRING("AUTH_LEAVE", toString(3));
}

void test_assoc_expire(void) {
  TEST_ASSERT_EQUAL_STRING("ASSOC_EXPIRE", toString(4));
}

void test_assoc_toomany(void) {
  TEST_ASSERT_EQUAL_STRING("ASSOC_TOOMANY", toString(5));
}

void test_not_authed(void) {
  TEST_ASSERT_EQUAL_STRING("NOT_AUTHED", toString(6));
}

void test_not_assoced(void) {
  TEST_ASSERT_EQUAL_STRING("NOT_ASSOCED", toString(7));
}

void test_assoc_leave(void) {
  TEST_ASSERT_EQUAL_STRING("ASSOC_LEAVE", toString(8));
}

void test_assoc_not_authed(void) {
  TEST_ASSERT_EQUAL_STRING("ASSOC_NOT_AUTHED", toString(9));
}

void test_handshake_timeout_4way(void) {
  TEST_ASSERT_EQUAL_STRING("4WAY_HANDSHAKE_TIMEOUT", toString(15));
}

void test_no_ap_found(void) {
  TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND", toString(201));
}

void test_auth_fail(void) {
  TEST_ASSERT_EQUAL_STRING("AUTH_FAIL", toString(202));
}

void test_handshake_timeout(void) {
  TEST_ASSERT_EQUAL_STRING("HANDSHAKE_TIMEOUT", toString(204));
}

void test_connection_fail(void) {
  TEST_ASSERT_EQUAL_STRING("CONNECTION_FAIL", toString(205));
}

// =============================================================================
// Cas inconnus / garde-fous — renvoient nullptr
// =============================================================================

void test_unknown_returns_null(void) {
  // Codes non couverts par le switch -> nullptr (le caller teste ce nullptr).
  TEST_ASSERT_NULL(toString(0));
  TEST_ASSERT_NULL(toString(10));      // trou entre 9 et 15
  TEST_ASSERT_NULL(toString(14));
  TEST_ASSERT_NULL(toString(16));
  TEST_ASSERT_NULL(toString(200));     // juste avant 201
  TEST_ASSERT_NULL(toString(203));     // trou entre 202 et 204
  TEST_ASSERT_NULL(toString(206));
  TEST_ASSERT_NULL(toString(999));
}

void test_negative_returns_null(void) {
  // -1 est la sentinelle "pas de déconnexion enregistrée" côté wifi_manager.cpp ;
  // le caller ne l'appelle que pour reason >= 0, mais la fonction reste sûre.
  TEST_ASSERT_NULL(toString(-1));
  TEST_ASSERT_NULL(toString(-100));
}

// =============================================================================
// Parité brute-force avec la transcription d'origine
// =============================================================================

void test_parity_bruteforce(void) {
  // Balaye une large plage couvrant tous les cas connus et leurs voisins.
  for (int r = -300; r <= 300; ++r) {
    const char* got = toString(r);
    const char* ref = refToString(r);
    if (ref == nullptr) {
      TEST_ASSERT_NULL(got);
    } else {
      TEST_ASSERT_NOT_NULL(got);
      TEST_ASSERT_EQUAL_STRING(ref, got);
    }
  }
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_unspecified);
  RUN_TEST(test_auth_expire);
  RUN_TEST(test_auth_leave);
  RUN_TEST(test_assoc_expire);
  RUN_TEST(test_assoc_toomany);
  RUN_TEST(test_not_authed);
  RUN_TEST(test_not_assoced);
  RUN_TEST(test_assoc_leave);
  RUN_TEST(test_assoc_not_authed);
  RUN_TEST(test_handshake_timeout_4way);
  RUN_TEST(test_no_ap_found);
  RUN_TEST(test_auth_fail);
  RUN_TEST(test_handshake_timeout);
  RUN_TEST(test_connection_fail);
  RUN_TEST(test_unknown_returns_null);
  RUN_TEST(test_negative_returns_null);
  RUN_TEST(test_parity_bruteforce);
  return UNITY_END();
}
