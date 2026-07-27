// Implémentation de n3_store_forward.h — voir l'en-tête pour les invariants.
// Transposition fidèle de la boucle de drain d'uploadphotosserver
// (camera_sync.cpp : planned hybride A1, pacing syncUploadRateLimitPause,
// budget temps, retries 429, commit-sur-succès, break-sur-échec-réseau),
// avec temps/attente injectés pour la testabilité native.

#include "n3_store_forward.h"

// ---------------------------------------------------------------------------
// Sémantique d'index (audit 2026-07, F6) — deux familles de backends coexistent :
//
//  - index STABLES (curseur) : `SdCursorBackend` d'uploadphotosserver. `commit()`
//    n'écrit que le curseur NVS ; l'élément reste énuméré, l'index suivant est
//    `index + 1`. Il FAUT avancer.
//  - index GLISSANTS (file indexée) : `commit()` retire réellement la clé — usage
//    explicitement envisagé par l'en-tête (« file NVS indexée côté ffp5cs »).
//    L'élément suivant prend l'index courant. Il ne faut PAS avancer : sinon on
//    saute un élément sur deux, et le drain s'arrête à mi-course puisque
//    `planned` a été calculé sur le `count()` initial.
//
// L'orchestrateur ne choisit pas entre les deux : il OBSERVE. Après un commit, il
// relit l'index courant ; si le handle n'a pas bougé, les index sont stables →
// avancer. `handle` est justement documenté comme l'identité de commit, donc
// unique et comparable.
//
// Coût : un `peek()` supplémentaire par élément committé — négligeable face à
// l'envoi réseau qui le précède, et `peek()` est sans effet de bord par contrat.
// ---------------------------------------------------------------------------
static void n3SfAdvanceCursor(N3SfBackend& backend, uint32_t committedHandle,
                              uint32_t& index) {
  N3SfItem probe = {};
  if (backend.peek(index, probe) && probe.handle == committedHandle) {
    ++index;  // index stables : l'élément committé est toujours là
  }
  // Sinon : élément retiré (ou file vidée) — l'index courant désigne déjà le
  // suivant, ou deviendra invalide au prochain peek() et arrêtera la boucle.
}

N3SfDrainResult n3SfDrain(N3SfBackend& backend, const N3SfDrainConfig& cfg,
                          N3SfSendFn send, void* ctx) {
  N3SfDrainResult r = {};
  if (send == nullptr) {
    return r;  // pas d'envoi possible : no-op (complete=false, rien tenté)
  }

  const uint32_t pending = backend.count();
  r.pending = pending;
  if (pending == 0) {
    r.complete = true;  // rien à faire = rien n'a échoué
    return r;
  }

  // Stratégie hybride (parité camera_sync) : vidage complet au-delà du seuil,
  // sinon incrémental ; plafond absolu dans tous les cas.
  uint32_t planned;
  if (pending > cfg.fullDrainThreshold) {
    planned = pending;
  } else {
    planned = (pending < cfg.maxItemsPerWake) ? pending : cfg.maxItemsPerWake;
  }
  if (cfg.hardCapPerWake != 0 && planned > cfg.hardCapPerWake) {
    planned = cfg.hardCapPerWake;
  }
  r.planned = planned;

  const bool timed = (cfg.nowMs != nullptr);
  const uint32_t drainStartMs = timed ? cfg.nowMs() : 0;
  uint32_t lastSendMs = 0;
  bool lastSendValid = false;

  // Curseur de lecture, distinct du compteur d'itérations : il n'avance que si
  // le backend a des index stables (cf. n3SfAdvanceCursor ci-dessus).
  uint32_t index = 0;

  for (uint32_t i = 0; i < planned; ++i) {
    // Budget temps : vérifié en tête d'itération sauf la première (parité).
    // Un arrêt ici est un REPORT (reprise au prochain réveil), pas un échec.
    if (timed && i > 0 && cfg.maxDurationMs != 0 &&
        (cfg.nowMs() - drainStartMs) >= cfg.maxDurationMs) {
      break;
    }

    N3SfItem item = {};
    if (!backend.peek(index, item)) {
      break;  // index invalide : fin de l'énumération
    }

    // Pacing : au moins minIntervalMs depuis le dernier envoi (parité
    // syncUploadRateLimitPause : attente du complément uniquement).
    if (timed && cfg.sleepMs != nullptr && cfg.minIntervalMs != 0 && lastSendValid) {
      const uint32_t elapsed = cfg.nowMs() - lastSendMs;
      if (elapsed < cfg.minIntervalMs) {
        cfg.sleepMs(cfg.minIntervalMs - elapsed);
      }
    }

    N3SfSend verdict = send(item, ctx);

    // Retries bornés sur rate-limit, pause pleine entre chaque (parité 429).
    for (uint8_t retry = 0;
         verdict == N3SfSend::RateLimited && retry < cfg.rateLimitRetries;
         ++retry) {
      if (cfg.sleepMs != nullptr && cfg.minIntervalMs != 0) {
        cfg.sleepMs(cfg.minIntervalMs);
      }
      verdict = send(item, ctx);
    }

    // Horodatage du dernier envoi APRES les retries (parité lastUploadMs).
    if (timed) {
      lastSendMs = cfg.nowMs();
      lastSendValid = true;
    }

    if (verdict == N3SfSend::Ok) {
      r.sent++;
      backend.commit(item);  // consommé seulement après acquittement
      n3SfAdvanceCursor(backend, item.handle, index);
    } else if (verdict == N3SfSend::HardFail) {
      // Rejet définitif de CET élément : on le saute (commit) pour ne pas
      // bloquer la file, mais il compte comme échec.
      r.failed++;
      backend.commit(item);
      n3SfAdvanceCursor(backend, item.handle, index);
    } else {
      // NetworkError, ou RateLimited après épuisement des retries : le réseau
      // est probablement indisponible -> arrêt, reprise au prochain réveil.
      // AUCUN commit : l'élément n'est pas brûlé.
      r.failed++;
      break;
    }
  }

  r.complete = (r.failed == 0);
  return r;
}
