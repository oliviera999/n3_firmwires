#ifndef N3_WIFI_H
#define N3_WIFI_H

#include <Arduino.h>
#include <WiFi.h>

struct N3WifiNetwork {
  const char* ssid;
  const char* pass;
};

struct N3WifiConfig {
  const N3WifiNetwork* networks;
  size_t networkCount;
  unsigned long timeoutMs;
  unsigned long delayBetweenMs;
  unsigned long preScanDelayMs;
  int scanMax;
  void (*onConnecting)();
  void (*onFailure)();
  void (*onSuccess)(const char* ssid);
};

/** Connexion WiFi : scan, tri par RSSI, essai avec BSSID/canal puis retry sans BSSID. */
bool n3WifiConnect(const N3WifiConfig& config, String* outWifiactif);

#endif
