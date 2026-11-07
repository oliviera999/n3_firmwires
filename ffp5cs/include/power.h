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

  /** v14.01 : true pendant reconnectWithSavedCredentials() (évite course WiFi loop/power). */
  static bool isStaReconnectInProgress();

  // Sauvegarde forcée
  void forceSaveTimeToFlash();

  /** Applique l'heure du RTC externe DS3231 si présent (à appeler après I2CBus::init()). */
  void applyExternalRTCIfPresent();

  /** v13.88 : true si la dernière sync NTP a validé SNTP_COMPLETED et un epoch plausible.
   *  v15.34 : ou si une heure serveur (X-Server-Ts) a été appliquée il y a moins de
   *  ContactDiag::kServerTimeTrustMs — on ne signe JAMAIS avec une horloge non confirmée. */
  bool hasTrustedNtpTime() const;

  /** v15.34 : applique l'heure serveur (epoch, latence aller-retour compensée) quand NTP a
   *  échoué. Retourne true si l'horloge a été modifiée. L'horloge devient « fiable » pour la
   *  signature HMAC (cf. hasTrustedNtpTime). */
  bool applyServerEpoch(uint32_t serverEpoch, uint32_t rttMs);

 private:
  // Paramètres NTP
  int _gmtOffsetSec;
  int _daylightOffsetSec;
  const char* _ntpServer;

  // Suivi synchronisation temps (intervalle : TimingConfig::NTP_SYNC_INTERVAL_MS)
  unsigned long _lastNtpSync;
  bool _ntpTrusted;
  // v15.34 : heure serveur appliquée (repli quand NTP échoue) — millis() de l'application, 0 = jamais.
  unsigned long _serverTimeAppliedMs;
  bool _serverTimeTrusted;

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
  
  // Méthode privée — v15.34 : wallBefore = epoch système juste avant la veille, pour ne corriger
  // que ce que l'horloge n'a PAS déjà compté (n3clock::sleepCorrectionSeconds, double comptage évité).
  uint32_t getSleptTime(uint64_t startUs, time_t wallBefore);
};
