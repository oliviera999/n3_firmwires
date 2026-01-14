#include "time_drift_monitor.h"
#include <WiFi.h>
#include <time.h>
#include <sys/time.h>
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include "config.h"
#include "power.h"

TimeDriftMonitor::TimeDriftMonitor()
  : _lastSyncTime(0),
    _lastSyncMillis(0),
    _syncInterval(DEFAULT_SYNC_INTERVAL),
    _powerManager(nullptr) {
}

void TimeDriftMonitor::attachPowerManager(PowerManager* manager) {
  _powerManager = manager;
}

void TimeDriftMonitor::begin() {
  LOG_INFO("Initialisation du moniteur de temps (Simplifié)");

  // Configuration NTP avec offset GMT
  configTime(SystemConfig::NTP_GMT_OFFSET_SEC, SystemConfig::NTP_DAYLIGHT_OFFSET_SEC, SystemConfig::NTP_SERVER);

  LOG_INFO("Intervalle de sync: %lu ms (%.1f heures)", 
            _syncInterval, _syncInterval / 3600000.0f);
  
  // Tentative immédiate si WiFi connecté
  if (WiFi.status() == WL_CONNECTED) {
    syncWithNTP();
  }
}

void TimeDriftMonitor::update() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  const unsigned long nowMs = millis();
  if (_lastSyncMillis == 0 || nowMs - _lastSyncMillis > _syncInterval) {
    syncWithNTP();
  }
}

void TimeDriftMonitor::syncWithNTP() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  struct tm timeinfo;
  bool syncSuccess = false;
  
  // Tentative simple de lecture NTP
  if (getLocalTime(&timeinfo)) {
    syncSuccess = true;
  } else {
    // Retry rapide - utiliser vTaskDelay() car peut être appelé depuis loop()
    vTaskDelay(pdMS_TO_TICKS(500));
    if (getLocalTime(&timeinfo)) {
      syncSuccess = true;
    }
  }

  if (syncSuccess) {
    time_t now = time(nullptr);
    _lastSyncTime = now;
    _lastSyncMillis = millis();
    
    // Notification au PowerManager si attaché
    if (_powerManager) {
      _powerManager->onExternalNtpSync(now, _lastSyncMillis);
    }
    
    LOG_INFO("Sync NTP OK: %s", asctime(&timeinfo));
  } else {
    LOG_WARN("Echec sync NTP");
  }
}

String TimeDriftMonitor::generateDriftReport() const {
  if (_lastSyncTime == 0) return "Aucune synchronisation NTP effectuée.";
  
  char buffer[64];
  struct tm timeinfo;
  localtime_r(&_lastSyncTime, &timeinfo);
  strftime(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &timeinfo);
  
  return String("Dernière sync NTP: ") + buffer;
}
