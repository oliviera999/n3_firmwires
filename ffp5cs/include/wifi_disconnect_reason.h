#pragma once
// =============================================================================
// FFP5CS — Libellé des raisons de déconnexion WiFi (pur)
// =============================================================================
// Extrait de wifi_manager.cpp (wifiDisconnectReasonStr, helper de diagnostic
// affiché quand une connexion STA échoue). Logique 100 % pure : un simple
// switch entier -> chaîne constante, sans matériel ni état global.
//
// Pur : mapping de constantes littérales, sans dépendance Arduino.h /
// esp_wifi.h / config.h (le seul en-tête inclus est le module pur partagé
// n3_wifi_diag). Le code d'origine se contentait de traduire le champ entier
// wifi_sta_disconnected.reason (déjà capté côté wifi_manager.cpp via
// s_lastStaDisconnectReason) en libellé lisible ; la capture de l'évènement et
// le Serial.printf restent côté wifi_manager.cpp.
//
// Parité : transcription ligne-à-ligne du switch d'origine (mêmes cases, mêmes
// libellés, même retour nullptr par défaut — le caller teste explicitement le
// nullptr avant d'afficher la raison).
// =============================================================================

#include "n3_wifi_diag.h"  // table canonique mutualisée (shared/n3_wifi_diag)

namespace WifiDisconnectReason {

// Renvoie une chaîne explicite pour les raisons de déconnexion STA courantes
// (codes ESP-IDF wifi_err_reason_t), ou nullptr si la raison n'est pas connue.
//
// Délègue désormais à N3WifiDiag::disconnectReasonToken (shared/n3_wifi_diag) —
// même table (cases 1..9, 15, 201, 202, 204, 205 ; default -> nullptr),
// comportement inchangé. Wrapper conservé pour l'API historique du caller
// (wifi_manager.cpp:wifiDisconnectReasonStr).
inline const char* toString(int reason) {
  return N3WifiDiag::disconnectReasonToken(reason);
}

}  // namespace WifiDisconnectReason
