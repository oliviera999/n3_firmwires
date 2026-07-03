#ifndef N3_TIME_H
#define N3_TIME_H

#include <Arduino.h>
#include <Preferences.h>
#include <stdint.h>

class ESP32Time;

#ifndef N3_TIME_MIN_VALID_EPOCH
#define N3_TIME_MIN_VALID_EPOCH 1577836800UL  /* 2020-01-01 */
#endif

void n3TimeSaveToFlash(ESP32Time& rtc, Preferences& prefs);
void n3TimeLoadFromFlash(Preferences& prefs, ESP32Time& rtc);
void n3PrintWakeupReason(Preferences& prefs, ESP32Time& rtc);

/** Charge l'epoch NVS dans rtc et l'injecte dans l'horloge système si plausible. */
bool n3TimeLoadAndApplyToSystem(Preferences& prefs, ESP32Time& rtc);

/** configTime + attente getLocalTime ; met à jour rtc si succès. Retourne true si NTP OK. */
bool n3TimeSyncNtp(ESP32Time& rtc,
                   long gmtOffsetSec,
                   int daylightOffsetSec,
                   const char* ntpServer,
                   uint32_t timeoutMs);

/** Horloge système plausible (epoch > seuil 2020). */
bool n3TimeHasPlausibleEpoch(void);

#endif
