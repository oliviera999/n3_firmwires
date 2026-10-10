#ifndef CAMERA_TIME_H
#define CAMERA_TIME_H

#include <Preferences.h>

class ESP32Time;

/** Offline-first : horloge systeme (conservee au deep sleep) ou, a defaut, epoch NVS ;
 *  puis NTP confirme (statut SNTP, TZ POSIX UTC+1) si WiFi OK. */
void n3CamSyncClock(Preferences& prefs, ESP32Time& rtc, bool wifiOk);

#endif
