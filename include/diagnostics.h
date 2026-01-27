#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <rom/rtc.h>

// Structure simplifiée pour stocker les informations de panic
struct PanicInfo {
  bool hasPanicInfo;
  char exceptionCause[128];      // Cause de l'exception (simplifié)
};

// Structure simplifiée pour crash (coredump géré par ESP-IDF)
struct CrashStatus {
  bool hasCrashInfo;
  int resetReason;  // Seule info essentielle conservée
};

struct DiagnosticStats {
  unsigned long uptimeSec;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t maxAllocHeap;
  uint32_t freeStack;
  uint32_t rebootCount;
  char lastRebootReason[32];
  // Statistiques CPU (optionnelles)
  char taskStats[1024];     // sortie brute de vTaskGetRunTimeStats()
  uint8_t idlePercent;  // somme des tâches IDLE (0..100), 255 si inconnu
  // Réseau/OTA
  uint32_t httpSuccessCount;
  uint32_t httpFailCount;
  int lastHttpCode;
  uint32_t otaSuccessCount;
  uint32_t otaFailCount;
  char lastOtaError[256];
  // Informations de panic
  PanicInfo panicInfo;
  // Informations de crash / coredump
  CrashStatus crashStatus;
};

class Diagnostics {
 public:
  Diagnostics();
  
  void begin();
  void loadFromNvs();
  void update();
  void toJson(ArduinoJson::JsonDocument& doc) const;
  // Enregistreurs d'événements
  void recordHttpResult(bool success, int httpCode);
  void recordOtaResult(bool success, const char* errorMsg);
  
  const DiagnosticStats& getStats() const { return _stats; }
  
  // Génère un rapport détaillé de redémarrage pour les emails
  void generateRestartReport(char* buffer, size_t bufferSize) const;
  
  // Vérifier si des infos PANIC sont disponibles
  bool hasPanicInfo() const { return _stats.panicInfo.hasPanicInfo; }
  bool hasCrashInfo() const { return _stats.crashStatus.hasCrashInfo; }
  
  // Nettoyer les infos PANIC (appelé après envoi du mail de boot)
  void clearPanicInfoAfterReport();
  
 private:
  DiagnosticStats _stats;
  unsigned long _lastUpdate;
  bool _bootRecorded;
  static const unsigned long UPDATE_INTERVAL_MS = 30000; // 30 secondes
  
  // Optimisation NVS - limitation de fréquence des sauvegardes
  unsigned long _lastHeapSave;
  uint32_t _lastSavedMinHeap;
  static const unsigned long MIN_HEAP_SAVE_INTERVAL = 600000UL; // 10 minutes minimum entre sauvegardes heap - optimisé
  static const uint32_t MIN_HEAP_DIFF_FOR_SAVE = 1024; // 1KB de différence minimum pour sauvegarder
  
  const char* getRebootReason() const;
  bool isCrashResetReason(esp_reset_reason_t reason) const;
  void capturePanicInfo();  // Simplifié: capture seulement exceptionCause
  void savePanicInfo();
  void loadPanicInfo();
  void clearPanicInfo();
  void loadCrashStatus();  // Simplifié: charge seulement resetReason
  void saveCrashStatus();
}; 