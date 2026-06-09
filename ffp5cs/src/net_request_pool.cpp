// net_request_pool.cpp — Pool statique de requêtes réseau (NetRequest).
// Extrait de app_tasks.cpp (audit optimisation v13.93), déplacement verbatim.
#include "net_request_pool.h"
#include <Arduino.h>
#include <cstring>
#include <cstdlib>
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
#include "rom/ets_sys.h"
#endif

const char* netRequestTypeLabel(NetReqType t) {
  switch (t) {
    case NetReqType::FetchRemoteState:
      return "GET outputs/state (config)";
    case NetReqType::PostRaw:
      return "POST mesures -> file postSender";
    case NetReqType::Heartbeat:
      return "heartbeat (via file)";
    case NetReqType::OtaCheck:
      return "OTA";
    default:
      return "?";
  }
}

// Pool statique NetRequest : évite malloc/free à chaque requête réseau → moins de fragmentation (piste E).
// Pas d'allocation heap par requête ; payload et doc sont dans le pool ou passés par référence.
// v12.20: Pool 16 S3 / 10 WROOM pour limiter saturation ; timeouts RPC réduits pour libérer slots plus tôt.
// v12.26: Dernier slot réservé à l'OTA pour que la vérification OTA (après réveil, trigger distant) puisse toujours s'exécuter même si le pool est saturé.
// v12.29: Slot N-2 réservé à FetchRemoteState (poll/GET) pour garantir exécution même si pool saturé par POSTs.
// v12.40: Slot N-4 réservé au POST. v12.42: 3 slots POST (cat3 replay, cat2 ack, cat1 périodique, priorité 3>2>1).
// WROOM: slot 7 = OTA, 6 = Fetch, 5 = POST cat3, 4 = POST cat2, 3 = POST cat1, 0..2 = partagés (Heartbeat, fallback).
// S3: slot 15 = OTA, 14 = Fetch, 13 = cat3, 12 = cat2, 11 = cat1, 0..10 = partagés.
#if defined(BOARD_WROOM)
static constexpr size_t kNetRequestPoolSize = 8;
#else
static constexpr size_t kNetRequestPoolSize = 16;
#endif
static constexpr size_t kNetRequestOtaReservedSlot = kNetRequestPoolSize - 1;    // Slot dédié OTA
static constexpr size_t kNetRequestFetchReservedSlot = kNetRequestPoolSize - 2;  // Slot dédié FetchRemoteState (poll/GET)
static constexpr size_t kNetRequestPostCat3Slot = kNetRequestPoolSize - 3;   // POST replay (priorité haute)
static constexpr size_t kNetRequestPostCat2Slot = kNetRequestPoolSize - 4;   // POST ack/événements
static constexpr size_t kNetRequestPostCat1Slot = kNetRequestPoolSize - 5;   // POST données périodiques
static constexpr size_t kNetRequestNormalMaxSlot = kNetRequestPoolSize - 6;  // Partagés: 0..NormalMax (WROOM: 0..2, S3: 0..10)
static NetRequest s_netRequestPool[kNetRequestPoolSize];
static bool s_netRequestPoolUsed[kNetRequestPoolSize] = {false};

/** Marque le slot i comme utilisé et retourne le NetRequest, ou nullptr si déjà pris. */
static NetRequest* netRequestAllocTrySlot(size_t i) {
  if (s_netRequestPoolUsed[i]) return nullptr;
  s_netRequestPoolUsed[i] = true;
  memset(&s_netRequestPool[i], 0, sizeof(NetRequest));
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[NETDBG] hypothesis=H2 netRequestAlloc ok slot=%u\n", (unsigned)i);
#else
  Serial.printf("[NETDBG] hypothesis=H2 netRequestAlloc ok slot=%u\n", (unsigned)i);
#endif
  return &s_netRequestPool[i];
}

/** Alloue un slot pour Heartbeat et autres (pas POST données). Utilise 0..NormalMax en excluant le slot POST réservé. */
NetRequest* netRequestAlloc(bool forOta) {
  if (forOta) {
    NetRequest* r = netRequestAllocTrySlot(kNetRequestOtaReservedSlot);
    if (r) return r;
    for (size_t i = 0; i <= kNetRequestFetchReservedSlot; ++i) {
      if (i == kNetRequestOtaReservedSlot) continue;
      NetRequest* r2 = netRequestAllocTrySlot(i);
      if (r2) return r2;
    }
  } else {
    for (size_t i = 0; i <= kNetRequestNormalMaxSlot; ++i) {
      NetRequest* r = netRequestAllocTrySlot(i);
      if (r) return r;
    }
  }
  size_t used = 0;
  for (size_t i = 0; i < kNetRequestPoolSize; ++i) if (s_netRequestPoolUsed[i]) used++;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[netRPC] Pool plein, netRequestAlloc échoue\n");
  ets_printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#else
  Serial.println(F("[netRPC] Pool plein, netRequestAlloc échoue"));
  Serial.printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#endif
  return nullptr;
}

/** Alloue un slot pour POST par catégorie (v12.42). Tente slot dédié puis partagés. */
NetRequest* netRequestAllocForPostCategory(AppTasks::PostCategory cat) {
  size_t slot = 0;
  switch (cat) {
    case AppTasks::PostCategory::Replay:   slot = kNetRequestPostCat3Slot; break;
    case AppTasks::PostCategory::EventAck: slot = kNetRequestPostCat2Slot; break;
    case AppTasks::PostCategory::Periodic: slot = kNetRequestPostCat1Slot; break;
  }
  NetRequest* r = netRequestAllocTrySlot(slot);
  if (r) return r;
  for (size_t i = 0; i <= kNetRequestNormalMaxSlot; ++i) {
    r = netRequestAllocTrySlot(i);
    if (r) return r;
  }
  size_t used = 0;
  for (size_t i = 0; i < kNetRequestPoolSize; ++i) if (s_netRequestPoolUsed[i]) used++;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[netRPC] Pool plein, netRequestAllocForPostCategory échoue\n");
  ets_printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#else
  Serial.println(F("[netRPC] Pool plein, netRequestAllocForPostCategory échoue"));
  Serial.printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#endif
  return nullptr;
}

/** Alloue un slot pour FetchRemoteState (poll/GET). Tente le slot réservé en priorité, puis partagés 0..NormalMax. */
NetRequest* netRequestAllocForFetch() {
  NetRequest* r = netRequestAllocTrySlot(kNetRequestFetchReservedSlot);
  if (r) return r;
  for (size_t i = 0; i <= kNetRequestNormalMaxSlot; ++i) {
    r = netRequestAllocTrySlot(i);
    if (r) return r;
  }
  size_t used = 0;
  for (size_t i = 0; i < kNetRequestPoolSize; ++i) if (s_netRequestPoolUsed[i]) used++;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
  ets_printf("[netRPC] Pool plein, netRequestAlloc échoue\n");
  ets_printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#else
  Serial.println(F("[netRPC] Pool plein, netRequestAlloc échoue"));
  Serial.printf("[NETDBG] hypothesis=H1,H2 used=%u poolSize=%u\n", (unsigned)used, (unsigned)kNetRequestPoolSize);
#endif
  return nullptr;
}

void netRequestFree(NetRequest* req) {
  if (!req) return;
  if (req >= s_netRequestPool && req < s_netRequestPool + kNetRequestPoolSize) {
    size_t idx = (size_t)(req - s_netRequestPool);
    s_netRequestPoolUsed[idx] = false;
#if defined(BOARD_S3) && defined(BOARD_HAS_PSRAM)
    ets_printf("[NETDBG] hypothesis=H4 netRequestFree slot=%u\n", (unsigned)idx);
#else
    Serial.printf("[NETDBG] hypothesis=H4 netRequestFree slot=%u\n", (unsigned)idx);
#endif
  } else {
    free(req);  // Sécurité si jamais un malloc legacy
  }
}

// Accesseurs pool (déclarés dans app_tasks.h, appelés par automatism_sync/diagnostics).
size_t netRequestPoolUsedCount() {
  size_t n = 0;
  for (size_t i = 0; i < kNetRequestPoolSize; ++i) {
    if (s_netRequestPoolUsed[i]) ++n;
  }
  return n;
}

size_t netRequestPoolSize() {
  return kNetRequestPoolSize;
}

size_t netRequestPoolPostSlotsFullThreshold() {
  return kNetRequestNormalMaxSlot + 1;  // Tous les slots POST (réservé + partagés) occupés
}
