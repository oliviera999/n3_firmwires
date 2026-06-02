#pragma once

#include "pgl_types.h"

class PglNetwork {
 public:
  void begin();
  bool uploadBatch(const PglStoredEvent* events, size_t count, uint32_t totalCount, uint32_t todayCount);
  bool sendHeartbeat(uint32_t bootCount);
  bool isWifiConnected() const;

 private:
  void ensureWifi();
};
