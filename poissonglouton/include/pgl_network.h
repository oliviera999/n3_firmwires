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

  /**
   * Envoie un heartbeat de supervision au serveur. La telemetrie additionnelle
   * (files d'attente, batterie, mode de detection...) est collectee par
   * l'appelant et passee ici pour rester decouplee du compteur/detection.
   */
  bool sendHeartbeat(uint32_t bootCount, const PglHeartbeatTelemetry& telemetry);
  const PglServerCommStatus& getServerStatus() const;

 private:
  void buildWifiConfig(N3WifiConfig& cfg) const;
  void tryConnectBeforeUpload();
  void recordPostResult(int httpCode);
  void recordHeartbeatResult(int httpCode);
  // Fast-reconnect : tente une connexion ciblee (BSSID+canal memorises en RTC)
  // avant de retomber sur la session de scan multi-reseaux n3_wifi.
  bool tryFastReconnect();
  void rememberLastGoodAp();

  PglServerCommStatus serverStatus_{};
  N3WifiSession wifiSession_{};
  bool wifiSessionActive_ = false;
  bool wifiConnecting_ = false;
  bool wifiBackoff_ = false;
  bool wifiDirectAttempt_ = false;   // tentative ciblee BSSID/canal en cours
  uint32_t wifiDirectDeadlineMs_ = 0;  // borne temps de la tentative ciblee
  uint32_t wifiRetryAfterMs_ = 0;
  wl_status_t lastWifiStatus_ = WL_IDLE_STATUS;
};
