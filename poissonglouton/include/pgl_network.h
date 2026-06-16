#pragma once

#include "pgl_types.h"

class PglNetwork {
 public:
  void begin();
  void connectWifi();
  bool uploadBatch(const PglStoredEvent* events, size_t count, uint32_t totalCount, uint32_t todayCount);
  bool sendHeartbeat(uint32_t bootCount);
  bool isWifiConnected() const;
  const PglServerCommStatus& getServerStatus() const;

 private:
  void ensureWifi();
  void recordPostResult(int httpCode);
  void recordHeartbeatResult(int httpCode);

  PglServerCommStatus serverStatus_{};
};
