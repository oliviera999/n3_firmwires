#ifndef CAMERA_SLEEP_H
#define CAMERA_SLEEP_H

#include <esp_sleep.h>
#include <esp_system.h>

const char* currentTargetName();
const char* wakeupCauseText(esp_sleep_wakeup_cause_t cause);
const char* resetReasonText(esp_reset_reason_t reason);
bool inPhotoWindow();

#endif
