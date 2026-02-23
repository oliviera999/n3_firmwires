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
  /** RTC brut (non validé). Préférer getCurrentEpochSafe() pour affichage et logique métier. */
  time_t getCurrentEpoch() { return time(nullptr); }
  /** Retourne un epoch validé pour affichage (évite valeurs aberrantes) */
  time_t getCurrentEpochSafe();
  void getCurrentTimeString(char* buffer, size_t bufferSize);
  /** Format ISO pour emails (YYYY-MM-DD HH:MM), cohérent avec mailer footer */
  void getCurrentTimeStringForMail(char* buffer, size_t bufferSize);

  // Validation et correction temps
  bool isValidEpoch(time_t epoch) const;
  time_t loadTimeWithFallback();
  void applyDriftCorrection();
  void smartSaveTime();

  // Configuration NTP
  void setNTPConfig(int gmtOffset, int daylightOffset, const char* ntpServer);

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

  // Suivi synchronisation temps (intervalle : TimingConfig::NTP_SYNC_INTERVAL_MS)
  unsigned long _lastNtpSync;

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
