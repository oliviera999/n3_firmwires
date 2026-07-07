#pragma once
// =============================================================================
// FFP5CS — Orchestrateur d'alerte « aquarium trop plein » (effets de bord, testable)
// =============================================================================
// Extrait du bloc flood de Automatism::handleAlerts. La décision reste dans FloodAlert
// (machine d'état pure, anti-spam). Cet orchestrateur exécute l'envoi du mail (via IMailer)
// et met à jour l'état FloodAlert (evaluate + markEmailSent si envoi réussi). Il renvoie un
// `Outcome` indiquant les effets de bord NON interfacés que l'appelant doit appliquer
// (persistance NVS, flag _highAquaSent, blink OLED, logs série) :
//   - EmailQueued : mail accepté -> _highAquaSent=true + blink + NVS save(lastFloodEmailEpoch) + log OK ;
//   - EmailFailed : mail refusé  -> log échec ;
//   - ExitedFlood : sortie d'état trop-plein -> _highAquaSent=false ;
//   - None        : rien.
//
// L'appelant recopie `st` vers ses membres APRÈS run() (capture evaluate + markEmailSent).
// Testable nativement (FakeMailer + FloodAlert pur). Dépend de imailer.h / flood_alert.h.
// =============================================================================

#include <cstdio>
#include <cstring>
#include "imailer.h"
#include "flood_alert.h"

namespace FloodOrchestrator {

enum class Outcome : uint8_t { None, EmailQueued, EmailFailed, ExitedFlood };

// `pumpLocked` = (tankPumpLocked || config.getPompeAquaLocked()) — déclenche l'ajout
// « / Pompe verrouillée » au message (comportement historique).
// Le seuil affiché dans le message est `p.limFloodMm` (identique à l'ancien
// cmToMm(getLimFlood())). Message reproduit à l'identique (format historique inclus).
//
// Phase 0 (arbitrage mails) — `persistentInFlood` : pointeur vers le flag `inFlood`
// DURABLE de l'appelant (membre d'Automatism, PAS la copie locale `st`). Il sert
// d'accusé de livraison différé : si la livraison SMTP échoue définitivement
// (retries épuisés), le mailer remet *persistentInFlood=false et la machine
// FloodAlert ré-émettra l'alerte (au rythme du cooldown) au lieu de la perdre.
// nullptr accepté (pas d'accusé — mailers synchrones/tests).
inline Outcome run(IMailer& mailer, FloodAlert::State& st, const FloodAlert::Params& p,
                   uint16_t wlAquaMm, uint32_t nowEpoch, bool mailEnabled,
                   const char* email, bool pumpLocked, bool* persistentInFlood = nullptr) {
  const FloodAlert::Decision d = FloodAlert::evaluate(st, wlAquaMm, nowEpoch, p, mailEnabled);

  if (d == FloodAlert::Decision::SendFloodEmail) {
    char msg[128];
    snprintf(msg, sizeof(msg), "Distance: %.1f mm (%s%.1f)",
             (double)wlAquaMm, " mm (< ", (double)p.limFloodMm);
    if (pumpLocked) {
      strncat(msg, " / Pompe verrouillée", sizeof(msg) - strlen(msg) - 1);
    }
    const bool sent = mailer.sendAlertAcked("Alerte - Aquarium TROP PLEIN", msg, email,
                                            false, persistentInFlood, false);
    if (sent) {
      FloodAlert::markEmailSent(st, nowEpoch);
      return Outcome::EmailQueued;
    }
    return Outcome::EmailFailed;
  }
  if (d == FloodAlert::Decision::ExitFlood) {
    return Outcome::ExitedFlood;
  }
  return Outcome::None;
}

}  // namespace FloodOrchestrator
