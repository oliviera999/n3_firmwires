// Stub WiFi minimal pour tests natifs de n3_data (n3DataPost/n3DataGet lisent
// WiFi.status() et WiFi.RSSI()). Les tests ne ciblent que la logique pure
// (encodage du body), donc ces accès ne sont pas exercés : no-op déterministe.
#pragma once
#include <Arduino.h>

#ifndef WL_CONNECTED
#define WL_CONNECTED 3
#endif

class WiFiStub {
 public:
  int status() { return WL_CONNECTED; }
  int RSSI() { return -50; }
  String SSID() { return String(""); }
};

inline WiFiStub WiFi;
