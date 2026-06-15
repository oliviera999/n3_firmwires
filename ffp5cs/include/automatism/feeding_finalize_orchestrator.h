#pragma once
// =============================================================================
// FFP5CS — Orchestrateur de fin de cycle nourrissage (effets de bord, testable)
// =============================================================================
// Extrait de Automatism::finalizeFeedingIfNeeded (src/automatism.cpp). À la vraie
// fin d'une phase de nourrissage, l'état local est déjà remis à zéro par l'appelant
// (offline-first) ; cet orchestrateur gère la TENTATIVE de sync distante :
//   - hors-ligne (connected=false)  : aucune publication, renvoie `Offline` ;
//   - en ligne + cycle AUTO          : publie l'évènement « gros+petits » (108/109=1) ;
//   - en ligne + cycle MANUEL        : publie la réinitialisation (108/109=0).
//
// Le SEUL effet de bord interfacé est la publication (IStatusPublisher::publish, qui
// forwarde vers sendFullUpdate). Les effets NON interfacés restent dans l'appelant,
// pilotés par l'`Outcome` renvoyé :
//   - GPIOParser::syncFeedEdgeStateAfterLocalPost(...) (anti faux-front au poll GET) ;
//   - les logs Serial (messages historiques reproduits à l'identique côté appelant) ;
//   - le gate (phase finie ?) et le reset des membres d'état (flags), en amont.
//
// `connected` = (WiFi.status()==WL_CONNECTED && config.isRemoteSendEnabled()) calculé
// par l'appelant (WiFi non abstrait). `readings` n'est lu/utilisé que si `connected`.
// Testable nativement (FakeStatusPublisher). Dépend seulement de istatus_publisher.h.
// =============================================================================

#include <cstdint>
#include "istatus_publisher.h"

namespace FeedingFinalizeOrchestrator {

// Paires « extraPairs » historiques poussées à la fin du cycle (parité exacte) :
//   - AUTO   : enregistre l'évènement nourrissage (gros+petits) pour le graphique serveur ;
//   - MANUEL : réinitialise les variables de nourrissage côté BDD.
inline constexpr const char* kAutoExtraPairs   = "bouffePetits=1&108=1&bouffeGros=1&109=1";
inline constexpr const char* kManualExtraPairs = "bouffePetits=0&108=0&bouffeGros=0&109=0";

// Issue de la tentative de sync : encode (en ligne/hors-ligne) × (auto/manuel) × (sync OK/échec).
// L'appelant en déduit l'appel GPIOParser (true,true pour auto / false,false pour manuel) et le
// message Serial. `Offline` : aucune publication n'a eu lieu.
enum class Outcome : uint8_t {
  Offline,          // non connecté : reset local uniquement, pas de publish
  AutoSynced,       // connecté + auto + publish accepté
  AutoSyncFailed,   // connecté + auto + publish refusé
  ManualSynced,     // connecté + manuel + publish accepté
  ManualSyncFailed  // connecté + manuel + publish refusé
};

inline Outcome run(bool connected, bool wasAutomatic,
                   const SensorReadings& readings, IStatusPublisher& publisher) {
  if (!connected) {
    return Outcome::Offline;
  }
  if (wasAutomatic) {
    // Enregistrer l'événement nourrissage auto (gros+petits) pour le graphique serveur
    const bool syncOk = publisher.publish(readings, kAutoExtraPairs);
    return syncOk ? Outcome::AutoSynced : Outcome::AutoSyncFailed;
  }
  const bool syncOk = publisher.publish(readings, kManualExtraPairs);
  return syncOk ? Outcome::ManualSynced : Outcome::ManualSyncFailed;
}

}  // namespace FeedingFinalizeOrchestrator
