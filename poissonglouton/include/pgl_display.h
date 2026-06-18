#pragma once

#include "pgl_types.h"

#include <Arduino.h>
#include <WiFi.h>

class PglDisplay {
 public:
  void begin();
  void update();
  void onBottleCount(uint32_t totalCount, uint32_t todayCount);
  void setCounter(uint32_t totalCount, uint32_t todayCount);
  void setWifiInfo(const char* ssid, wl_status_t status, int rssi);
  void setWifiSearching();
  void setWifiConnecting();
  void setWifiOffline();
  void setUltrasonDistance(uint16_t distanceCm, bool sensorPresent);
  void setServerStatus(const PglServerCommStatus& status, uint16_t pendingEvents);
  void showAudioPlaying(const char* reason, const char* mp3Path);
  void showAudioIdle();
  void tickSmileyIdle();
  void showIdle();
  void sleepBacklight();
  bool adminUnlocked() const;
  bool isReady() const;
  void setHardwareStatus(bool displayOk, bool irPresent, bool irObstacle);

 private:
  void refreshLabels();

#if !PGL_HEADLESS
  uint32_t totalCount_ = 0;
  uint32_t todayCount_ = 0;
  uint32_t lastCountAnimMs_ = 0;
  char wifiLine_[56] = "WiFi: ...";
  char audioLine_[56] = "En attente";
  char usLine_[48] = "US: -";
  char serverLine_[64] = "Srv: -";
  char hwLine_[56] = "Ecran: ... | IR: ...";
  bool gfxOk_ = false;
  bool lvglOk_ = false;
  bool ready_ = false;
#endif
};
