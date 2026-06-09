#pragma once
//
// net_request_pool.h — Pool statique de requêtes réseau (NetRequest).
//
// Extrait de app_tasks.cpp (audit optimisation v13.93). Pool à slots évitant
// malloc/free par requête (moins de fragmentation) : slots dédiés OTA / Fetch /
// POST par catégorie + slots partagés. Voir net_request_pool.cpp pour le détail
// de la réservation. Unité cohésive et isolée (réutilisable, testable).
//
#include <stdint.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <ArduinoJson.h>
#include "config.h"     // BufferConfig::POST_PAYLOAD_MAX_SIZE
#include "app_tasks.h"  // AppTasks::PostCategory

class Diagnostics;

enum class NetReqType : uint8_t {
  FetchRemoteState = 1,
  PostRaw = 2,
  Heartbeat = 3,
  OtaCheck = 4,  // Vérification OTA (boot, périodique 2h, ou déclenchement serveur distant)
};

struct NetRequest {
  NetReqType type;
  TaskHandle_t requester;
  uint32_t timeoutMs;
  bool success;
  volatile bool cancelled;  // Flag d'abandon (evite use-after-free si caller timeout)
  bool fromNVSFallback;    // v11.193: true si FetchRemoteState a utilisé le cache NVS (ne pas itérer sur doc)

  ArduinoJson::JsonDocument* doc;   // FetchRemoteState
  const Diagnostics* diag;          // Heartbeat
  char payload[BufferConfig::POST_PAYLOAD_MAX_SIZE];  // PostRaw (copie). WROOM: 896 pour éviter overflow DRAM.
  uint32_t sdSeqNum;
  bool hasSdQueueEntry;  // true: supprimer la file SD seulement après succès HTTP réel
};

// --- API du pool (anciennement statics dans app_tasks.cpp) ---
const char* netRequestTypeLabel(NetReqType t);
NetRequest* netRequestAlloc(bool forOta = false);
NetRequest* netRequestAllocForPostCategory(AppTasks::PostCategory cat);
NetRequest* netRequestAllocForFetch();
void netRequestFree(NetRequest* req);
