#ifndef N3_WIFI_DIAG_H
#define N3_WIFI_DIAG_H

// ============================================================================
// n3_wifi_diag — libellés de diagnostic WiFi (PUR, header-only).
//
// Mapping d'entiers de diagnostic WiFi vers des chaînes constantes, sans
// dépendance Arduino/WiFi/esp_wifi. Consolide dans shared/ deux tables
// aujourd'hui définies séparément :
//   - le mapping "raison de déconnexion STA" (codes ESP-IDF wifi_err_reason_t)
//     de ffp5cs (ffp5cs/include/wifi_disconnect_reason.h) ;
//   - le mapping "statut de liaison" (wl_status_t) de poissonglouton
//     (poissonglouton/include/pgl_log.h::pglWifiStatusName).
//
// Pur et header-only (fonctions inline, mapping de littéraux) → testable en
// natif (Unity, suite test_wifi_diag). La capture des évènements et l'affichage
// (Serial/OLED/LVGL) restent côté firmware.
//
// NB : les libellés COURTS de raison de déconnexion de poissonglouton
// (pglWifiDisconnectReasonName : "UNSPEC", "4WAY_HANDSHAKE"…) sont volontairement
// abrégés pour l'écran LVGL et diffèrent des tokens ESP-IDF canoniques ci-dessous ;
// ils restent donc LOCAUX à PGL (non mutualisés) pour ne pas élargir sa ligne
// de diagnostic. Ce module n'unifie que ce qui est réellement identique.
// ============================================================================

namespace N3WifiDiag {

/**
 * Raison de déconnexion STA (code ESP-IDF `wifi_err_reason_t`, entier) → token
 * canonique, ou nullptr si le code n'est pas connu (le caller décide de
 * l'affichage). Parité stricte avec l'ancien `WifiDisconnectReason::toString`
 * de ffp5cs (mêmes cases 1..9, 15, 201, 202, 204, 205 ; default → nullptr).
 */
inline const char* disconnectReasonToken(int reason) {
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

/**
 * Statut de liaison (`wl_status_t`, passé en entier) → libellé court.
 * Les valeurs sont celles, stables, de l'API Arduino WiFi :
 *   0 WL_IDLE_STATUS, 1 WL_NO_SSID_AVAIL, 2 WL_SCAN_COMPLETED, 3 WL_CONNECTED,
 *   4 WL_CONNECT_FAILED, 5 WL_CONNECTION_LOST, 6 WL_DISCONNECTED.
 * Parité stricte avec l'ancien `pglWifiStatusName` (mêmes libellés ; default →
 * "UNKNOWN"). Le firmware passe `static_cast<int>(status)`.
 */
inline const char* statusName(int status) {
  switch (status) {
    case 0: return "IDLE";          // WL_IDLE_STATUS
    case 1: return "NO_SSID";       // WL_NO_SSID_AVAIL
    case 2: return "SCAN_DONE";     // WL_SCAN_COMPLETED
    case 3: return "CONNECTED";     // WL_CONNECTED
    case 4: return "CONNECT_FAIL";  // WL_CONNECT_FAILED
    case 5: return "LOST";          // WL_CONNECTION_LOST
    case 6: return "DISCONNECTED";  // WL_DISCONNECTED
    default: return "UNKNOWN";
  }
}

}  // namespace N3WifiDiag

#endif  // N3_WIFI_DIAG_H
