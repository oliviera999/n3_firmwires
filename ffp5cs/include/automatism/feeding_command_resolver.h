#pragma once
// =============================================================================
// FFP5CS — Résolveur de commandes de nourrissage distant (pur, testable)
// =============================================================================
// PROTOCOLE COMPTEUR MONOTONE (v15.0 — remplace l'ancienne détection de front).
//
// Le serveur ne renvoie plus un niveau 0/1 pour 108/"bouffePetits" (petits) et
// 109/"bouffeGros" (gros) : il renvoie un ENTIER monotone croissant = nombre
// TOTAL de commandes de nourrissage jamais émises pour ce canal. Le serveur ne
// le remet JAMAIS à zéro. Le firmware mémorise, par canal, un compteur EXÉCUTÉ
// persisté en NVS plus un flag « seeded » (amorçage) one-shot.
//
// Règles (par poll), pour chaque canal présent :
//   pending = serverCounter - executedCounter
//
//   1. PREMIÈRE VUE après un flash neuf (seeded == false) : on adopte
//      executed = serverCounter, seeded = true, et on NE distribue RIEN. Évite
//      tout nourrissage parasite si l'appareil est flashé alors que le compteur
//      serveur est déjà élevé.
//   2. RECUL du compteur (serverCounter < executed, ex. reset BDD) : resync
//      executed = serverCounter, aucune distribution.
//   3. CAP DE RATTRAPAGE (sécurité poissons, CRITIQUE) : MAX_FEED_CATCHUP = 5.
//      Si pending > MAX_FEED_CATCHUP, on saute executed = serverCounter - 5
//      (on jette l'excédent) afin qu'une longue coupure ne puisse JAMAIS
//      sur-nourrir. pending est alors plafonné à 5.
//   4. CYCLE EN COURS (feedingInProgress == true) : on ne fait RIEN ce poll et
//      on N'AVANCE PAS executed — on re-tentera au poll suivant.
//   5. Sinon, si pending > 0 : on distribue EXACTEMENT UN repas et executed += 1.
//   6. Si les DEUX canaux ont pending > 0 dans le même poll : un seul cycle
//      SÉQUENTIEL (gros puis petits) via Action::Both, et les DEUX compteurs
//      executed sont incrémentés de 1.
//
// `persist` indique à l'appelant qu'au moins un compteur executed ou le flag
// seeded a changé et doit être réécrit en NVS.
//
// Aucune dépendance Arduino/ArduinoJson → testable nativement (g++).
// =============================================================================

#include <cstdint>

namespace FeedingCommandResolver {

// Plafond de rattrapage : on ne distribue jamais plus de 5 repas d'un coup
// (anti-surdosage après une longue période hors-ligne). Invariant de sécurité.
inline constexpr uint32_t MAX_FEED_CATCHUP = 5;

// Action à exécuter par l'appelant (GPIOParser) pour ce poll.
enum class Action : uint8_t {
  None,   // rien à déclencher
  Small,  // petits seuls
  Big,    // gros seuls
  Both,   // gros + petits -> un seul cycle SÉQUENTIEL (gros puis petits)
};

// Entrées : compteurs serveur (108/109) + compteurs exécutés persistés +
// flag d'amorçage + état cycle en cours.
struct Inputs {
  uint32_t smallCounter = 0;     // valeur entière 108 / "bouffePetits" du serveur
  bool smallPresent = false;     // clé 108 / "bouffePetits" présente dans le doc
  uint32_t bigCounter = 0;       // valeur entière 109 / "bouffeGros" du serveur
  bool bigPresent = false;       // clé 109 / "bouffeGros" présente dans le doc
  uint32_t execSmall = 0;        // compteur exécuté persisté (NVS) — petits
  uint32_t execBig = 0;          // compteur exécuté persisté (NVS) — gros
  bool seeded = false;           // false tant que l'amorçage one-shot n'a pas eu lieu
  bool feedingInProgress = false;
};

// Décision : action + nouvelles valeurs à persister.
struct Decision {
  Action action = Action::None;
  uint32_t newExecSmall = 0;
  uint32_t newExecBig = 0;
  bool newSeeded = false;
  bool persist = false;   // true si un compteur exec ou seeded a changé
};

inline Decision resolve(const Inputs& in) {
  Decision d;
  d.newExecSmall = in.execSmall;
  d.newExecBig = in.execBig;
  d.newSeeded = in.seeded;
  d.action = Action::None;

  // Aucun canal présent dans ce poll : rien à faire, rien à persister.
  if (!in.smallPresent && !in.bigPresent) {
    return d;
  }

  // (1) Amorçage one-shot après flash neuf : adopter les compteurs serveur, ne
  // rien distribuer. Seuls les canaux présents sont adoptés ; seeded passe à true.
  if (!in.seeded) {
    if (in.smallPresent) d.newExecSmall = in.smallCounter;
    if (in.bigPresent)   d.newExecBig = in.bigCounter;
    d.newSeeded = true;
    d.persist = true;
    return d;
  }

  // (2) Recul du compteur (reset BDD) : resync sans distribuer. Traité par canal.
  if (in.smallPresent && in.smallCounter < d.newExecSmall) {
    d.newExecSmall = in.smallCounter;
    d.persist = true;
  }
  if (in.bigPresent && in.bigCounter < d.newExecBig) {
    d.newExecBig = in.bigCounter;
    d.persist = true;
  }

  // (3) Cap de rattrapage : si pending > MAX_FEED_CATCHUP, jeter l'excédent.
  if (in.smallPresent && in.smallCounter > d.newExecSmall &&
      (in.smallCounter - d.newExecSmall) > MAX_FEED_CATCHUP) {
    d.newExecSmall = in.smallCounter - MAX_FEED_CATCHUP;
    d.persist = true;
  }
  if (in.bigPresent && in.bigCounter > d.newExecBig &&
      (in.bigCounter - d.newExecBig) > MAX_FEED_CATCHUP) {
    d.newExecBig = in.bigCounter - MAX_FEED_CATCHUP;
    d.persist = true;
  }

  // pending après resync/cap.
  const bool smallPending = in.smallPresent && in.smallCounter > d.newExecSmall;
  const bool bigPending   = in.bigPresent   && in.bigCounter > d.newExecBig;

  // (4) Cycle en cours : ne rien distribuer, ne pas avancer executed. On
  // re-tentera au prochain poll. (persist peut déjà être true via resync/cap.)
  if (in.feedingInProgress) {
    return d;
  }

  // (5)/(6) Distribuer un repas par canal en attente (cycle séquentiel si les deux).
  if (smallPending && bigPending) {
    d.action = Action::Both;
    d.newExecSmall += 1;
    d.newExecBig += 1;
    d.persist = true;
  } else if (bigPending) {
    d.action = Action::Big;
    d.newExecBig += 1;
    d.persist = true;
  } else if (smallPending) {
    d.action = Action::Small;
    d.newExecSmall += 1;
    d.persist = true;
  }
  return d;
}

}  // namespace FeedingCommandResolver
