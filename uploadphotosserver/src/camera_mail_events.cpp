#include "camera_mail_events.h"

#include <Arduino.h>
#include "time.h"
#include <cstdio>

bool cameraGetLocalTimeString(char* out, size_t outSize) {
  if (!out || outSize == 0) return false;
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) {
    snprintf(out, outSize, "NTP indisponible");
    return false;
  }
  strftime(out, outSize, "%Y-%m-%d %H:%M:%S", &timeinfo);
  return true;
}
