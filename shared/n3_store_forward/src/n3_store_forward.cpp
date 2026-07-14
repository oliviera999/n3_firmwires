// Implémentation de n3_store_forward.h — voir l'en-tête pour les invariants.
// Transposition fidèle de la boucle de drain d'uploadphotosserver
// (camera_sync.cpp : planned hybride A1, pacing syncUploadRateLimitPause,
// budget temps, retries 429, commit-sur-succès, break-sur-échec-réseau),
// avec temps/attente injectés pour la testabilité native.

#include "n3_store_forward.h"

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

  for (uint32_t i = 0; i < planned; ++i) {
    // Budget temps : vérifié en tête d'itération sauf la première (parité).
    // Un arrêt ici est un REPORT (reprise au prochain réveil), pas un échec.
    if (timed && i > 0 && cfg.maxDurationMs != 0 &&
        (cfg.nowMs() - drainStartMs) >= cfg.maxDurationMs) {
      break;
    }

    N3SfItem item = {};
    if (!backend.peek(i, item)) {
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
    } else if (verdict == N3SfSend::HardFail) {
      // Rejet définitif de CET élément : on le saute (commit) pour ne pas
      // bloquer la file, mais il compte comme échec.
      r.failed++;
      backend.commit(item);
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
