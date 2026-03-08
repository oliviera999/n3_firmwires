#ifndef N3_TIME_H
#define N3_TIME_H

#include <Arduino.h>
#include <Preferences.h>

class ESP32Time;

void n3TimeSaveToFlash(ESP32Time& rtc, Preferences& prefs);
void n3TimeLoadFromFlash(Preferences& prefs, ESP32Time& rtc);
void n3PrintWakeupReason(Preferences& prefs, ESP32Time& rtc);

#endif
