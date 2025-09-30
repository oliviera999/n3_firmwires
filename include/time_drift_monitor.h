#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <time.h>
#include "log.h"

class PowerManager;

class TimeDriftMonitor {
public:
  TimeDriftMonitor();
  void attachPowerManager(PowerManager* manager);

  // Initialisation
  void begin();

  // Mise à jour périodique
  void update();

  // Synchronisation NTP et calcul de dérive
  void syncWithNTP();

  // Accesseurs
  float getDriftPPM() const { return _driftPPM; }
  float getDriftSeconds() const { return _driftSeconds; }
  time_t getLastSyncTime() const { return _lastSyncTime; }
  bool isDriftCalculated() const { return _driftCalculated; }
  String getDriftStatusString() const;

  // Configuration
  void setSyncInterval(unsigned long intervalMs) { _syncInterval = intervalMs; }
  void setDriftThreshold(float thresholdPPM) { _driftThresholdPPM = thresholdPPM; }

  // Sauvegarde/Chargement depuis NVS
  void saveToNVS();
  void loadFromNVS();

  // Affichage sur moniteur série
  void printDriftInfo() const;

  // Génération de rapport pour emails
  String generateDriftReport() const;

private:
  // Variables de dérive
  float _driftPPM;           // Dérive en parties par million
  float _driftSeconds;       // Dérive en secondes
  time_t _lastSyncTime;      // Timestamp epoch de la dernière sync NTP
  unsigned long _lastSyncMillis; // millis() de la dernière sync
  bool _driftCalculated;     // Indique si la dérive a été calculée

  // Configuration
  unsigned long _syncInterval;     // Intervalle entre les syncs NTP (ms)
  float _driftThresholdPPM;        // Seuil d'alerte pour la dérive (PPM)

  // Suivi du temps
  time_t _lastNtpEpoch;      // Epoch NTP de la dernière sync
  unsigned long _lastNtpMillis; // millis() de la dernière sync
  time_t _lastLocalEpoch;    // Epoch local de la dernière sync
  unsigned long _lastLocalMillis; // millis() de la dernière sync

  // Variables pour le calcul de dérive (références précédentes)
  time_t _previousNtpEpoch;     // Epoch NTP de la sync précédente
  unsigned long _previousNtpMillis; // millis() de la sync précédente
  time_t _previousLocalEpoch;   // Epoch local de la sync précédente
  unsigned long _previousLocalMillis; // millis() de la sync précédente

  PowerManager* _powerManager;        // Interface vers PowerManager pour corrections

  // Persistance NVS
  Preferences _preferences;
  static const char* NVS_NAMESPACE;

  // Constantes
  static const unsigned long DEFAULT_SYNC_INTERVAL = 3600000UL; // 1 heure
  static constexpr float DEFAULT_DRIFT_THRESHOLD_PPM = 100.0f; // 100 PPM = 0.1%
  static const unsigned long MIN_SYNC_INTERVAL = 300000UL; // 5 minutes minimum
  static const int MAX_NTP_RETRIES = 3; // Nombre maximum de tentatives NTP
  static const unsigned long NTP_RETRY_DELAY_MS = 5000; // Délai entre tentatives

  // Calculs internes
  void calculateDrift(time_t newNtpEpoch, time_t localEpochBeforeSync, unsigned long localMillisBeforeSync);
  bool performNTPSync();
  time_t getNTPTime();
  String getCurrentTimeString(time_t epoch = 0) const;
};
