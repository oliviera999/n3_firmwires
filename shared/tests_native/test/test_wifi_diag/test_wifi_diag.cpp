// Tests Unity natifs pour n3_wifi_diag (libellés de diagnostic WiFi purs).
//
//   pio test -c platformio-native.ini -e native -f test_wifi_diag
//
// Ces mappings s'affichent en diagnostic (Serial/OLED/LVGL) quand une connexion
// WiFi échoue : un mauvais libellé trompe le diagnostic terrain. On vérifie
// chaque case connue, les défauts (nullptr / "UNKNOWN") et la PARITÉ avec une
// transcription de référence (brute-force sur une large plage d'entiers).

#include <cstdint>  // types entiers avant tout en-tête
#include <unity.h>

#include <cstring>

#include "n3_wifi_diag.h"

using namespace N3WifiDiag;

void setUp() {}
void tearDown() {}

// --- Référence indépendante (mêmes cases que ffp5cs::WifiDisconnectReason) ---
static const char* refReasonToken(int r) {
  switch (r) {
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

// --- disconnectReasonToken : cas connus ---

void test_reason_known_codes() {
  TEST_ASSERT_EQUAL_STRING("UNSPECIFIED", disconnectReasonToken(1));
  TEST_ASSERT_EQUAL_STRING("AUTH_EXPIRE", disconnectReasonToken(2));
  TEST_ASSERT_EQUAL_STRING("ASSOC_NOT_AUTHED", disconnectReasonToken(9));
  TEST_ASSERT_EQUAL_STRING("4WAY_HANDSHAKE_TIMEOUT", disconnectReasonToken(15));
  TEST_ASSERT_EQUAL_STRING("NO_AP_FOUND", disconnectReasonToken(201));
  TEST_ASSERT_EQUAL_STRING("AUTH_FAIL", disconnectReasonToken(202));
  TEST_ASSERT_EQUAL_STRING("HANDSHAKE_TIMEOUT", disconnectReasonToken(204));
  TEST_ASSERT_EQUAL_STRING("CONNECTION_FAIL", disconnectReasonToken(205));
}

// --- disconnectReasonToken : trous intentionnels et défauts -> nullptr ---

void test_reason_unknown_returns_null() {
  TEST_ASSERT_NULL(disconnectReasonToken(0));
  TEST_ASSERT_NULL(disconnectReasonToken(10));
  TEST_ASSERT_NULL(disconnectReasonToken(17));   // couvert par PGL, pas par ce token canonique
  TEST_ASSERT_NULL(disconnectReasonToken(203));  // exclu intentionnellement
  TEST_ASSERT_NULL(disconnectReasonToken(200));
  TEST_ASSERT_NULL(disconnectReasonToken(999));
}

void test_reason_negative_returns_null() {
  TEST_ASSERT_NULL(disconnectReasonToken(-1));
  TEST_ASSERT_NULL(disconnectReasonToken(-205));
}

// --- disconnectReasonToken : parité brute-force sur [-300, 300] ---

void test_reason_parity_bruteforce() {
  for (int r = -300; r <= 300; ++r) {
    const char* got = disconnectReasonToken(r);
    const char* ref = refReasonToken(r);
    if (ref == nullptr) {
      TEST_ASSERT_NULL(got);
    } else {
      TEST_ASSERT_NOT_NULL(got);
      TEST_ASSERT_EQUAL_STRING(ref, got);
    }
  }
}

// --- statusName : wl_status (valeurs Arduino) ---

void test_status_known() {
  TEST_ASSERT_EQUAL_STRING("IDLE", statusName(0));
  TEST_ASSERT_EQUAL_STRING("NO_SSID", statusName(1));
  TEST_ASSERT_EQUAL_STRING("SCAN_DONE", statusName(2));
  TEST_ASSERT_EQUAL_STRING("CONNECTED", statusName(3));
  TEST_ASSERT_EQUAL_STRING("CONNECT_FAIL", statusName(4));
  TEST_ASSERT_EQUAL_STRING("LOST", statusName(5));
  TEST_ASSERT_EQUAL_STRING("DISCONNECTED", statusName(6));
}

void test_status_unknown() {
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", statusName(7));
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", statusName(255));  // WL_NO_SHIELD
  TEST_ASSERT_EQUAL_STRING("UNKNOWN", statusName(-1));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_reason_known_codes);
  RUN_TEST(test_reason_unknown_returns_null);
  RUN_TEST(test_reason_negative_returns_null);
  RUN_TEST(test_reason_parity_bruteforce);
  RUN_TEST(test_status_known);
  RUN_TEST(test_status_unknown);
  return UNITY_END();
}
