#pragma once
// =============================================================================
// n3_store_forward — Orchestrateur de drain d'une file offline durable (pur)
// =============================================================================
// Généralisation de la logique de drain d'uploadphotosserver (camera_sync.cpp,
// stratégie hybride A1/A2 de l'audit 2026-07-05) et de la file NVS de ffp5cs
// (web_client_queue). Invariants verrouillés ici et par les tests natifs :
//
//  - **peek → commit, jamais burn** : un élément n'est consommé (commit) qu'après
//    acquittement de l'envoi. Sur échec, il reste en file pour le prochain réveil.
//  - **Stratégie hybride** : au-delà de `fullDrainThreshold` éléments en attente,
//    on tente le vidage complet (rattrapage) ; sinon drain incrémental borné à
//    `maxItemsPerWake`. Dans tous les cas plafonné à `hardCapPerWake` (réalisable
//    dans le budget temps).
//  - **Pacing** : au moins `minIntervalMs` entre deux envois (rate-limit serveur).
//  - **Budget temps** : le drain s'arrête proprement après `maxDurationMs`
//    (reprise au prochain réveil, ce n'est PAS un échec).
//  - **429-aware** : `RateLimited` → pause `minIntervalMs` + retries bornés
//    (`rateLimitRetries`) sur le MÊME élément ; épuisé → arrêt du drain.
//  - **NetworkError** → arrêt immédiat du drain (réseau perdu, reprise plus tard).
//  - **HardFail** (extension, absent d'upload) → l'élément est définitivement
//    rejeté : compté en échec MAIS committé (skip) pour ne pas bloquer la file.
//  - `complete` = « aucun échec ce réveil » (pas « file vidée ») — un report au
//    réveil suivant (budget/plafond) n'est pas un échec (A1).
//
// PUR : le temps (`nowMs`) et l'attente (`sleepMs`) sont INJECTÉS → entièrement
// testable en natif (backend mock + envoi scripté). nowMs == nullptr désactive
// pacing et budget temps. Aucune dépendance Arduino/HTTP : l'envoi réel est un
// callback fourni par le firmware (upload multipart, POST url-encodé…), le
// stockage réel est un backend concret fourni par le firmware (SD + curseur
// NVS côté upload, file NVS indexée côté ffp5cs). L'ENQUEUE (append/drop-oldest)
// relève du backend concret — ce contrat v1 ne couvre que le drain.
// =============================================================================

#include <stddef.h>
#include <stdint.h>

/** Élément vu par l'orchestrateur : opaque, identifié par un handle stable. */
struct N3SfItem {
  uint32_t handle;        // identité de commit (n° photo, index NVS…)
  const char* ref;        // référence externe (chemin SD) ou nullptr si payload inline
  const uint8_t* payload; // payload inline ou nullptr si ref-based
  size_t payloadLen;
};

/**
 * Backend de stockage durable (SD, NVS…). Sans HTTP, sans WiFi.
 * L'énumération (tri, bornes de scan) est faite par le backend.
 *
 * SÉMANTIQUE D'INDEX — les deux familles sont supportées (v1.1.0, audit F6) :
 *  - **index STABLES** : `commit()` n'avance qu'un curseur, l'élément reste
 *    énuméré (cf. `SdCursorBackend` d'uploadphotosserver).
 *  - **index GLISSANTS** : `commit()` retire réellement la clé, l'élément
 *    suivant prend l'index courant (file NVS indexée).
 *
 * L'orchestrateur ne présuppose ni l'une ni l'autre : après chaque `commit()`
 * il relit l'index courant et n'avance que si le `handle` n'a pas changé.
 * Deux exigences en découlent pour le backend :
 *  - `handle` doit être **unique** parmi les éléments en attente (c'est déjà
 *    l'identité de commit) ;
 *  - `peek()` doit rester **sans effet de bord** (il est appelé une fois de plus
 *    par élément committé).
 *
 * ⚠ Les pointeurs de l'item rendu par peek() doivent rester valides jusqu'au
 * prochain peek()/commit() (l'orchestrateur ne copie pas les données pointées).
 */
class N3SfBackend {
 public:
  virtual ~N3SfBackend() {}
  /** Éléments en attente (déjà énumérés/tries). */
  virtual uint32_t count() = 0;
  /** Lit l'élément d'index i SANS le consommer. false = index invalide (stop). */
  virtual bool peek(uint32_t index, N3SfItem& out) = 0;
  /** Consomme l'élément après acquittement (avance le curseur / retire la clé). */
  virtual void commit(const N3SfItem& item) = 0;
};

/** Verdict d'un envoi, renvoyé par le callback firmware. */
enum class N3SfSend : uint8_t {
  Ok,            // acquitté -> commit
  RateLimited,   // HTTP 429 -> pause + retry borné sur le même élément
  NetworkError,  // réseau perdu / erreur récupérable -> arrêt du drain (pas de commit)
  HardFail,      // rejet définitif de CET élément -> commit (skip) + compté en échec
};

/** Callback d'envoi fourni par le firmware. ctx = état utilisateur (bytes envoyés…). */
typedef N3SfSend (*N3SfSendFn)(const N3SfItem& item, void* ctx);

struct N3SfDrainConfig {
  uint32_t maxItemsPerWake;    // drain incrémental : éléments max par réveil
  uint32_t fullDrainThreshold; // au-delà -> tentative de vidage complet
  uint32_t hardCapPerWake;     // plafond absolu (budget temps / intervalle). 0 = pas de plafond
  uint32_t minIntervalMs;      // pacing entre envois (0 = désactivé)
  uint32_t maxDurationMs;      // budget temps du drain (0 = désactivé)
  uint8_t rateLimitRetries;    // retries supplémentaires sur RateLimited
  uint32_t (*nowMs)();         // horloge injectée (millis). nullptr = pacing+budget off
  void (*sleepMs)(uint32_t);   // attente injectée (delay/vTaskDelay). nullptr = pas d'attente
};

struct N3SfDrainResult {
  uint32_t pending;  // éléments en attente au début du drain
  uint32_t planned;  // éléments planifiés ce réveil (hybride + plafonds)
  uint32_t sent;     // envoyés et committés
  uint32_t failed;   // échecs (NetworkError/RateLimited épuisé/HardFail)
  bool complete;     // aucun échec ce réveil (≠ « file vidée »)
};

/** Draine la file : pacing + budget + 429 + peek->commit. Aucune I/O directe. */
N3SfDrainResult n3SfDrain(N3SfBackend& backend, const N3SfDrainConfig& cfg,
                          N3SfSendFn send, void* ctx);
