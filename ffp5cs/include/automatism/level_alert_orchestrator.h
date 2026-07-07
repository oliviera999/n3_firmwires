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
//
// Phase 0 (arbitrage mails) — latching sur livraison confirmée :
//   - le flag n'est plus latché que si l'enqueue a RÉUSSI (mail refusé/queue pleine
//     -> flag inchangé -> l'alerte est retentée au prochain cycle) ;
//   - `alreadyAlerted` référence un stockage durable (membre d'Automatism) : son
//     adresse sert d'accusé de livraison différé (sendAlertAcked). Si la livraison
//     SMTP échoue définitivement (retries épuisés), le mailer restaure le flag à
//     sa valeur d'avant latch et l'alerte est ré-émise au lieu d'être perdue.
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
    // Accusé différé : échec définitif de livraison -> flag restauré à false (ré-armé).
    if (!mailer.sendAlertAcked(raiseSubject, msg, email, false, &alreadyAlerted, false)) {
      return false;  // mail refusé (queue pleine…) : pas de latch, retenté au prochain cycle
    }
    alreadyAlerted = true;
    return true;
  }
  if (clearSubject != nullptr && d == LevelAlert::Decision::Clear && mailEnabled) {
    char msg[128];
    formatDistance(msg, sizeof(msg), valueMm, " mm (<= ", clearThrMm);
    // Accusé différé : échec définitif -> flag restauré à true (le mail de fin sera retenté).
    if (!mailer.sendAlertAcked(clearSubject, msg, email, false, &alreadyAlerted, true)) {
      return false;
    }
    alreadyAlerted = false;
    return true;
  }
  return false;
}

}  // namespace LevelAlertOrchestrator
