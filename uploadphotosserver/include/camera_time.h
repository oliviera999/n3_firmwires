#ifndef CAMERA_TIME_H
#define CAMERA_TIME_H

#include <Preferences.h>

class ESP32Time;

/** Offline-first : NVS → horloge systeme, puis NTP (TZ Africa/Casablanca) si WiFi OK. */
void n3CamSyncClock(Preferences& prefs, ESP32Time& rtc, bool wifiOk);

#endif
