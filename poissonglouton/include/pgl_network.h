#pragma once

#include "pgl_types.h"
#include "n3_wifi.h"

#include <WiFi.h>

struct PglUploadResult {
  bool ok = false;
  uint32_t lastAckedEventId = 0;  // event_id du dernier événement acquitté
};

class PglNetwork {
 public:
  void begin();
  void startBackgroundWifi();
  /** Avance la connexion WiFi en arrière-plan. Retourne true si l'état a changé. */
  bool pollWifi(uint32_t budgetMs);
  bool isWifiConnected() const;
  bool isWifiConnecting() const;
  bool isWifiOffline() const;

  /**
   * Envoie un lot d'événements vers le serveur.
   * result.lastAckedEventId est renseigné si le serveur renvoie un ack JSON.
   */
  PglUploadResult uploadBatch(const PglStoredEvent* events, size_t count,
                              uint32_t totalCount, uint32_t todayCount);

  bool sendHeartbeat(uint32_t bootCount);
  const PglServerCommStatus& getServerStatus() const;

 private:
  void buildWifiConfig(N3WifiConfig& cfg) const;
  void tryConnectBeforeUpload();
  void recordPostResult(int httpCode);
  void recordHeartbeatResult(int httpCode);

  PglServerCommStatus serverStatus_{};
  N3WifiSession wifiSession_{};
  bool wifiSessionActive_ = false;
  bool wifiConnecting_ = false;
  bool wifiBackoff_ = false;
  uint32_t wifiRetryAfterMs_ = 0;
  wl_status_t lastWifiStatus_ = WL_IDLE_STATUS;
};
