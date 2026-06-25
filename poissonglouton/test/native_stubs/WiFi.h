#pragma once

// Stub WiFi minimal pour tests natifs : pgl_log.h inclut <WiFi.h> et utilise
// l'enum wl_status_t (pglWifiStatusName). On ne teste pas le reseau ici.

#include <cstdint>

typedef enum {
  WL_NO_SHIELD = 255,
  WL_IDLE_STATUS = 0,
  WL_NO_SSID_AVAIL = 1,
  WL_SCAN_COMPLETED = 2,
  WL_CONNECTED = 3,
  WL_CONNECT_FAILED = 4,
  WL_CONNECTION_LOST = 5,
  WL_DISCONNECTED = 6
} wl_status_t;
