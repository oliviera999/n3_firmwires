#pragma once
// =============================================================================
// FFP5CS — Libellé des raisons de déconnexion WiFi (pur)
// =============================================================================
// Extrait de wifi_manager.cpp (wifiDisconnectReasonStr, helper de diagnostic
// affiché quand une connexion STA échoue). Logique 100 % pure : un simple
// switch entier -> chaîne constante, sans matériel ni état global.
//
// Pur : n'utilise aucun en-tête (mapping de constantes littérales). Aucune
// dépendance Arduino.h / esp_wifi.h / config.h. Le code d'origine se contentait
// de traduire le champ entier wifi_sta_disconnected.reason (déjà capté côté
// wifi_manager.cpp via s_lastStaDisconnectReason) en libellé lisible ; la
// capture de l'évènement et le Serial.printf restent côté wifi_manager.cpp.
//
// Parité : transcription ligne-à-ligne du switch d'origine (mêmes cases, mêmes
// libellés, même retour nullptr par défaut — le caller teste explicitement le
// nullptr avant d'afficher la raison).
// =============================================================================

namespace WifiDisconnectReason {

// Renvoie une chaîne explicite pour les raisons de déconnexion STA courantes
// (codes ESP-IDF wifi_err_reason_t), ou nullptr si la raison n'est pas connue.
//
// Parité avec wifi_manager.cpp:wifiDisconnectReasonStr : mêmes valeurs de case
// (1..9, 15, 201, 202, 204, 205) et mêmes littéraux ; default -> nullptr.
inline const char* toString(int reason) {
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

}  // namespace WifiDisconnectReason
