#pragma once

#include <Arduino.h>
#include <esp_sleep.h>
#include <esp_timer.h>
#include <esp_task_wdt.h>
#include <time.h>
#include <sys/time.h>
#include "config.h"
#include "log.h"

class PowerManager {
 public:
  PowerManager();

  // Chien de garde
  void initWatchdog();
  void resetWatchdog();

  // Veille légère
  uint32_t goToLightSleep(uint32_t sleepTimeSeconds);

  // Gestion du temps avec persistance en mémoire flash
  void initTime();
  void syncTimeFromNTP();
  void saveTimeToFlash();
  void loadTimeFromFlash();
  void updateTime();

  // Accesseurs
  time_t getCurrentEpoch() { return time(nullptr); }
  void getCurrentTimeString(char* buffer, size_t bufferSize);

  // Validation et correction temps
  bool isValidEpoch(time_t epoch) const;
  time_t loadTimeWithFallback();
  void applyDriftCorrection();
  void smartSaveTime();

  // Configuration NTP
  void setNTPConfig(int gmtOffset, int daylightOffset, const char* ntpServer);

  // Log cause de réveil
  void logWakeupCause(esp_sleep_wakeup_cause_t cause);

  // Gestion WiFi pour light sleep
  void saveCurrentWifiCredentials();
  bool reconnectWithSavedCredentials();
  void waitForNetworkReady();

  // Sauvegarde forcée
  void forceSaveTimeToFlash();

 private:
  // Paramètres NTP
  int _gmtOffsetSec;
  int _daylightOffsetSec;
  const char* _ntpServer;

  // Suivi synchronisation temps
  unsigned long _lastNtpSync;
  static const unsigned long NTP_SYNC_INTERVAL = 3600000UL; // 1 heure

  // Identifiants WiFi sauvegardés pour light sleep
  char _lastSSID[33];  // SSID max 32 chars + null terminator
  char _lastPassword[65];  // WiFi password max 64 chars + null terminator
  bool _hasSavedCredentials;

  // Optimisation NVS - limitation de fréquence des sauvegardes
  unsigned long _lastTimeSave;
  time_t _lastSavedEpoch;

  // Variables pour correction de dérive
  unsigned long _lastDriftCorrection;
  float _currentDriftPPM;
  float _lastDriftSeconds;
  float _driftAccumulator;  // Accumulateur unique pour la correction de dérive

  // Accumulation de microsecondes résiduelles pour une meilleure précision du RTC
  uint32_t _sleepRemainderUs;
  
  // Méthode privée
  uint32_t getSleptTime(uint64_t startUs);
};
