#pragma once
// =============================================================================
// FFP5CS — Résolveur de commandes de nourrissage distant (pur, testable)
// =============================================================================
// Décide, à partir des niveaux 108/109 reçus du serveur (petits/gros) et de
// l'état edge précédent, QUELLE action de nourrissage déclencher à ce poll.
//
// Corrige le bug de simultanéité : avant, 108 et 109 étaient traités GPIO par
// GPIO dans une boucle. Quand les DEUX montaient (0→1) dans le même poll, le
// premier traité posait `_currentFeedingPhase` et le second était rejeté par la
// garde `isFeedingInProgress()` — tout en mémorisant son edge à 1 → la seconde
// commande était PERDUE durablement (plus de front montant). De plus, deux
// distributions « petits » et « gros » simultanées feraient tourner les deux
// servos en même temps (conflit de puissance), ce que `feedSequential` évite.
//
// Règles :
//   - front montant = canal présent && niveau==1 && état précédent==0 ;
//   - DEUX fronts montants simultanés ⇒ `Both` (cycle SÉQUENTIEL gros→petits) ;
//   - un cycle déjà en cours ⇒ on NE consomme PAS le(s) front(s) : on les garde
//     « en attente » (état précédent laissé à 0) pour re-tenter au prochain poll,
//     une fois le cycle courant terminé — au lieu de les perdre silencieusement ;
//   - sinon, on suit le niveau courant (front descendant 1→0 et états stables
//     mettent simplement à jour l'état edge, sans action).
//
// Aucune dépendance Arduino/ArduinoJson → testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace FeedingCommandResolver {

// Action à exécuter par l'appelant (GPIOParser) pour ce poll.
enum class Action : uint8_t {
  None,   // rien à déclencher
  Small,  // petits seuls (front montant 108)
  Big,    // gros seuls (front montant 109)
  Both,   // gros + petits simultanés -> un seul cycle SÉQUENTIEL (gros puis petits)
};

// Entrées : niveaux lus dans le doc serveur + état edge précédent + cycle en cours.
struct Inputs {
  bool smallPresent = false;     // clé 108 / "bouffePetits" présente dans le doc
  bool smallLevel = false;       // niveau lu (pertinent si smallPresent)
  bool bigPresent = false;       // clé 109 / "bouffeGros" présente
  bool bigLevel = false;         // niveau lu (pertinent si bigPresent)
  bool prevSmall = false;        // s_lastFeedSmallState
  bool prevBig = false;          // s_lastFeedBigState
  bool feedingInProgress = false;
};

// Décision : action + nouvelles valeurs à stocker dans s_lastFeed{Small,Big}State.
struct Decision {
  Action action = Action::None;
  bool newPrevSmall = false;
  bool newPrevBig = false;
};

inline Decision resolve(const Inputs& in) {
  const bool risingSmall = in.smallPresent && in.smallLevel && !in.prevSmall;
  const bool risingBig   = in.bigPresent   && in.bigLevel   && !in.prevBig;

  Decision d;
  // Par défaut : suivre le niveau courant des canaux présents ; conserver l'état
  // précédent pour un canal absent du poll (évite d'effacer un edge non rapporté).
  d.newPrevSmall = in.smallPresent ? in.smallLevel : in.prevSmall;
  d.newPrevBig   = in.bigPresent   ? in.bigLevel   : in.prevBig;
  d.action = Action::None;

  // Cycle déjà en cours : ne pas consommer un front montant (sinon commande perdue).
  // On le garde « en attente » (prev laissé à 0) pour re-tenter au prochain poll.
  if (in.feedingInProgress) {
    if (risingSmall) d.newPrevSmall = false;
    if (risingBig)   d.newPrevBig   = false;
    return d;
  }

  if (risingSmall && risingBig) {
    d.action = Action::Both;
  } else if (risingBig) {
    d.action = Action::Big;
  } else if (risingSmall) {
    d.action = Action::Small;
  }
  return d;
}

}  // namespace FeedingCommandResolver
