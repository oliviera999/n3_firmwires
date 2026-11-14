#pragma once
// =============================================================================
// FFP5CS — Gate d'arbitrage des alertes partagées (Phase 3, logique pure)
// =============================================================================
// Décide si le SERVEUR couvre les MAILS d'alertes partagées (aquarium bas,
// trop-plein, réserve basse, chauffage ON/OFF) : depuis les Phases 1+2 côté
// n3_serveur (CRON 1 min + alertes dérivées du POST), le serveur est l'émetteur
// PRIMAIRE de ces mails. L'ESP ne les émet plus que si son échange serveur a
// échoué (décision LOCALE — cf. ARCHITECTURE_MAILS_ARBITRAGE.md §3.1/§3.3).
//
//   serverCovers == true  -> l'ESP se tait sur les MAILS partagés (fin des
//                            doublons ; le serveur les calcule sur nos données) ;
//   serverCovers == false -> FAILOVER : l'ESP émet, borné par l'anti-congestion
//                            (P1/P2 uniquement, WiFi requis, budget — Mailer).
//
// Hors périmètre : les actionneurs locaux (relais chauffage, pompe réserve)
// ET l'état flood (`inFlood`) lu par RefillOverfill pour Unlock. Le serveur
// dérive les mails chauffage depuis `etatHeat` ; il ne pilote pas le GPIO.
// HeaterOrchestrator et FloodOrchestrator restent exécutés même si serverCovers.
//
// Critère : dernier GET config OK (`AutomatismSync::isServerOk()`) ET dernier
// POST réussi assez frais (le serveur ne peut dériver que des données reçues).
// Pure (aucune dépendance Arduino) -> testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace SharedAlertGate {

// Fraîcheur max du dernier POST réussi : 3 cycles d'envoi (SEND_INTERVAL_MS = 30 s).
// Au-delà, le serveur travaille sur des données périmées -> failover.
inline constexpr uint32_t DEFAULT_POST_FRESH_MS = 90000UL;

// `nowMs` / `lastPostOkMs` en millis(). uint32_t EXPLICITE : l'arithmétique non
// signée 32 bits rend le calcul d'âge wrap-safe, y compris en test natif 64 bits
// (millis() est déjà 32 bits sur ESP32).
// `lastPostOkMs == 0` = jamais posté depuis le boot -> non couvert.
inline bool serverCovers(bool serverOk, uint32_t nowMs, uint32_t lastPostOkMs,
                         uint32_t maxAgeMs = DEFAULT_POST_FRESH_MS) {
  if (!serverOk) return false;
  if (lastPostOkMs == 0) return false;
  return static_cast<uint32_t>(nowMs - lastPostOkMs) <= maxAgeMs;
}

}  // namespace SharedAlertGate
