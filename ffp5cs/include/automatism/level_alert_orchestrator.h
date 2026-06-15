#pragma once
// =============================================================================
// FFP5CS — Orchestrateur d'alerte de niveau d'eau (effets de bord, testable nativement)
// =============================================================================
// Extrait des blocs « aquarium bas » et « réserve basse » de Automatism::handleAlerts.
// Décision déléguée à LevelAlert (pur) ; exécution (email via IMailer) ici. Testable
// nativement avec FakeMailer. Générique : sert pour les DEUX alertes de niveau.
//
//   - Raise : email `raiseSubject` (+ flag passé à true) ;
//   - Clear : email `clearSubject` (+ flag passé à false) — UNIQUEMENT si clearSubject != nullptr
//             (l'aquarium n'a pas de mail de fin : son flag est remis à zéro à part, en silence).
//
// Renvoie true si un mail a été envoyé -> l'appelant déclenche le blink OLED.
// Parité : le flag n'est mis à jour que dans la branche `&& mailEnabled` (comme l'inline),
// et le message reproduit EXACTEMENT l'ancien `formatDistanceAlert` (format identique,
// y compris le « mm ( » redondant historique).
// Dépend de imailer.h / level_alert.h + <cstdio> -> testable g++.
// =============================================================================

#include <cstdio>
#include "imailer.h"
#include "level_alert.h"

namespace LevelAlertOrchestrator {

// Reproduit l'ancien formatDistanceAlert : "%s%.1f mm (%s%.1f)" avec prefix="Distance: ".
inline void formatDistance(char* buf, size_t n, float distanceMm,
                           const char* suffix, float thresholdMm) {
  snprintf(buf, n, "Distance: %.1f mm (%s%.1f)", distanceMm, suffix, thresholdMm);
}

inline bool run(IMailer& mailer, bool& alreadyAlerted, uint16_t valueMm,
                uint16_t alertThrMm, uint16_t clearThrMm, bool mailEnabled,
                const char* email, const char* raiseSubject, const char* clearSubject) {
  const LevelAlert::Decision d =
      LevelAlert::evaluate(valueMm, alertThrMm, clearThrMm, alreadyAlerted);

  if (d == LevelAlert::Decision::Raise && mailEnabled) {
    char msg[128];
    formatDistance(msg, sizeof(msg), valueMm, " mm (> ", alertThrMm);
    mailer.sendAlert(raiseSubject, msg, email);
    alreadyAlerted = true;
    return true;
  }
  if (clearSubject != nullptr && d == LevelAlert::Decision::Clear && mailEnabled) {
    char msg[128];
    formatDistance(msg, sizeof(msg), valueMm, " mm (<= ", clearThrMm);
    mailer.sendAlert(clearSubject, msg, email);
    alreadyAlerted = false;
    return true;
  }
  return false;
}

}  // namespace LevelAlertOrchestrator
