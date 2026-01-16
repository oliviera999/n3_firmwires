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
  String getCurrentTimeString();

  // Validation et correction temps
  bool isValidEpoch(time_t epoch) const;
  time_t loadTimeWithFallback();
  void applyDriftCorrection();
  void smartSaveTime();
  void setMeasuredDrift(float driftPpm, float driftSeconds);
  void onExternalNtpSync(time_t epoch, unsigned long syncMillis);

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

  // Epoch par défaut si aucun temps sauvegardé (1/1/2024 00:00:00)
  static const time_t DEFAULT_EPOCH = 1704067200;

  // Identifiants WiFi sauvegardés pour light sleep
  String _lastSSID;
  String _lastPassword;
  bool _hasSavedCredentials;

  // Optimisation NVS - limitation de fréquence des sauvegardes
  unsigned long _lastTimeSave;
  time_t _lastSavedEpoch;

  // Variables pour correction de dérive
  unsigned long _lastDriftCorrection;
  float _currentDriftPPM;
  time_t _lastSyncEpoch;
  float _lastDriftSeconds;
  float _defaultDriftAccumulator;
  float _measuredDriftAccumulator;

  // Accumulation de microsecondes résiduelles pour une meilleure précision du RTC
  uint32_t _sleepRemainderUs;
  
  // Méthode privée
  uint32_t getSleptTime(uint64_t startUs);
};
