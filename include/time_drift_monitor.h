#pragma once

#include <Arduino.h>
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

  // Synchronisation NTP (simple)
  void syncWithNTP();

  // Accesseurs
  time_t getLastSyncTime() const { return _lastSyncTime; }
  
  // Configuration
  void setSyncInterval(unsigned long intervalMs) { _syncInterval = intervalMs; }

  // Génération de rapport pour emails (version simplifiée)
  String generateDriftReport() const;

private:
  time_t _lastSyncTime;      // Timestamp epoch de la dernière sync NTP
  unsigned long _lastSyncMillis; // millis() de la dernière sync
  
  // Configuration
  unsigned long _syncInterval;     // Intervalle entre les syncs NTP (ms)
  
  PowerManager* _powerManager;        // Interface vers PowerManager

  // Constantes
  static const unsigned long DEFAULT_SYNC_INTERVAL = 3600000UL; // 1h
};
