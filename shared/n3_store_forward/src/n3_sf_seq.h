#pragma once
// =============================================================================
// n3_sf_seq — Numérotation d'une file store-and-forward « compteur + curseur » (pur)
// =============================================================================
// Modèle (uploadphotosserver, file SD) : chaque élément persisté reçoit un numéro
// de séquence croissant. Deux compteurs durables (NVS) :
//   - `count`  : dernier numéro ENGAGÉ (élément réellement persisté) ;
//   - `cursor` : dernier numéro ACQUITTÉ par le serveur.
// Les éléments en attente sont exactement les numéros dans ]cursor, count].
//
// Défauts corrigés (terrain 2026-10, ESP32-CAM n3pp rechargée avec une carte SD
// déjà utilisée / NVS vierge) :
//  - un scan ne bornant que `n > cursor` envoyait aussi des fichiers AU-DELÀ du
//    compteur (photos d'une autre vie de la carte) et poussait le curseur loin
//    devant `count` ;
//  - le numéro suivant valait `count + 1` : tant que `count <= cursor`, chaque
//    nouvelle photo naissait « déjà acquittée » (attente = 0) et n'était JAMAIS
//    envoyée ;
//  - une réponse serveur définitive sur un élément (4xx de contenu, fichier
//    local illisible) était traitée comme une panne réseau : l'élément restait
//    en tête et bloquait toute la file, à chaque réveil, indéfiniment.
//
// PUR (aucune dépendance Arduino) → testé en natif (test_sf_seq).
// =============================================================================

#include <stdint.h>

/** Prochain numéro à réserver : toujours strictement au-dessus de `count` ET de
 *  `cursor` (un numéro <= cursor ne serait jamais « en attente » donc jamais envoyé). */
inline uint32_t n3SfSeqNext(uint32_t count, uint32_t cursor) {
  return ((count > cursor) ? count : cursor) + 1U;
}

/** Nombre d'éléments en attente (]cursor, count]). */
inline uint32_t n3SfSeqPending(uint32_t count, uint32_t cursor) {
  return (count > cursor) ? (count - cursor) : 0U;
}

/** L'élément de numéro `n` fait-il partie de la file en attente ? */
inline bool n3SfSeqIsPending(uint32_t n, uint32_t count, uint32_t cursor) {
  return n > cursor && n <= count;
}

enum class N3SfSeqReconcileAction : uint8_t {
  None,           // support cohérent avec les compteurs
  AdoptOrphan,    // un élément count+1 persisté sans engagement (coupure entre écriture et
                  // commit NVS) : il est réintégré (count = n) et sera envoyé normalement
  AdoptForeign,   // numéros bien au-delà des compteurs (carte d'une autre vie, NVS effacée) :
                  // compteurs portés au max, ces éléments ne sont PAS envoyés
  RealignCount,   // cursor > count (état incohérent hérité) : count = cursor
};

struct N3SfSeqReconcile {
  uint32_t count;
  uint32_t cursor;
  N3SfSeqReconcileAction action;
};

/**
 * Réconcilie les compteurs durables avec le plus grand numéro présent sur le support
 * (`maxStored`, 0 si aucun élément). À appeler au démarrage à froid, AVANT toute
 * nouvelle réservation de numéro.
 *
 * Un orphelin d'écriture interrompue porte toujours exactement max(count,cursor)+1 :
 * tant que le commit n'a pas eu lieu, la réservation suivante réutilise ce numéro.
 * Au-delà, l'élément ne peut pas provenir de cette série de compteurs.
 */
inline N3SfSeqReconcile n3SfSeqReconcile(uint32_t count, uint32_t cursor, uint32_t maxStored) {
  N3SfSeqReconcile r = {count, cursor, N3SfSeqReconcileAction::None};
  const uint32_t known = (count > cursor) ? count : cursor;
  if (maxStored > known) {
    if (maxStored - known == 1U) {
      r.count = maxStored;
      r.action = N3SfSeqReconcileAction::AdoptOrphan;
    } else {
      r.count = maxStored;
      r.cursor = maxStored;
      r.action = N3SfSeqReconcileAction::AdoptForeign;
    }
  } else if (cursor > count) {
    r.count = cursor;
    r.action = N3SfSeqReconcileAction::RealignCount;
  }
  return r;
}

/** Classe d'une réponse d'upload HTTP pour le drain. */
enum class N3SfUploadClass : uint8_t {
  Ok,            // 2xx : acquitté
  RateLimited,   // 429
  ItemRejected,  // rejet du CONTENU de cet élément (400/413/415/422) : le renvoyer
                 // tel quel échouera encore — compter, puis sauter (cf. tracker)
  Transient,     // réseau (<=0), auth/route (401/403/404), serveur (5xx)… : réessayer
                 // plus tard SANS sauter l'élément
};

inline N3SfUploadClass n3SfClassifyUploadCode(int httpCode) {
  if (httpCode >= 200 && httpCode < 300) return N3SfUploadClass::Ok;
  if (httpCode == 429) return N3SfUploadClass::RateLimited;
  if (httpCode == 400 || httpCode == 413 || httpCode == 415 || httpCode == 422) {
    return N3SfUploadClass::ItemRejected;
  }
  return N3SfUploadClass::Transient;
}

/** Rejets consécutifs de l'élément en tête de file (à conserver en mémoire RTC). */
struct N3SfRejectTracker {
  uint32_t handle;
  uint8_t count;
};

/**
 * Enregistre un rejet de l'élément `handle`. Retourne true quand le seuil
 * `maxRejects` est atteint : l'appelant saute alors l'élément (HardFail) et
 * appelle n3SfRejectReset(). Un autre handle redémarre le compte à 1.
 */
inline bool n3SfRejectShouldSkip(N3SfRejectTracker& t, uint32_t handle, uint8_t maxRejects) {
  if (t.count == 0 || t.handle != handle) {
    t.handle = handle;
    t.count = 0;
  }
  if (t.count < 0xFF) {
    ++t.count;
  }
  return t.count >= maxRejects;
}

inline void n3SfRejectReset(N3SfRejectTracker& t) {
  t.handle = 0;
  t.count = 0;
}
