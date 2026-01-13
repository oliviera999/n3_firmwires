#pragma once

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <esp_system.h>
#include <ArduinoJson.h>
#include <rom/rtc.h>

// Structure pour stocker les informations détaillées de panic
struct PanicInfo {
  bool hasPanicInfo;
  String exceptionCause;      // Cause de l'exception
  uint32_t exceptionAddress;  // Adresse où s'est produit le panic
  uint32_t excvaddr;          // Adresse virtuelle de l'exception (pour les fautes mémoire)
  String taskName;            // Nom de la tâche qui a paniqué
  int core;                   // Core CPU qui a paniqué
  String additionalInfo;      // Informations additionnelles
};

struct DiagnosticStats {
  unsigned long uptimeSec;
  uint32_t freeHeap;
  uint32_t minFreeHeap;
  uint32_t maxAllocHeap;
  uint32_t freeStack;
  uint32_t rebootCount;
  String lastRebootReason;
  // Statistiques CPU (optionnelles)
  String taskStats;     // sortie brute de vTaskGetRunTimeStats()
  uint8_t idlePercent;  // somme des tâches IDLE (0..100), 255 si inconnu
  // Réseau/OTA
  uint32_t httpSuccessCount;
  uint32_t httpFailCount;
  int lastHttpCode;
  uint32_t otaSuccessCount;
  uint32_t otaFailCount;
  String lastOtaError;
  // Informations de panic
  PanicInfo panicInfo;
};

class Diagnostics {
 public:
  Diagnostics();
  
  void begin();
  void update();
  void toJson(ArduinoJson::JsonDocument& doc) const;
  // Enregistreurs d'événements
  void recordHttpResult(bool success, int httpCode);
  void recordOtaResult(bool success, const char* errorMsg);
  
  const DiagnosticStats& getStats() const { return _stats; }
  
  // Génère un rapport détaillé de redémarrage pour les emails
  String generateRestartReport() const;
  
  // Vérifier si des infos PANIC sont disponibles
  bool hasPanicInfo() const { return _stats.panicInfo.hasPanicInfo; }
  
  // Nettoyer les infos PANIC (appelé après envoi du mail de boot)
  void clearPanicInfoAfterReport();
  
 private:
  DiagnosticStats _stats;
  unsigned long _lastUpdate;
  static const unsigned long UPDATE_INTERVAL_MS = 30000; // 30 secondes
  
  // Optimisation NVS - limitation de fréquence des sauvegardes
  unsigned long _lastHeapSave;
  uint32_t _lastSavedMinHeap;
  static const unsigned long MIN_HEAP_SAVE_INTERVAL = 600000UL; // 10 minutes minimum entre sauvegardes heap - optimisé
  static const uint32_t MIN_HEAP_DIFF_FOR_SAVE = 1024; // 1KB de différence minimum pour sauvegarder
  
  String getRebootReason() const;
  void capturePanicInfo();
  void savePanicInfo();
  void loadPanicInfo();
  void clearPanicInfo();
}; 