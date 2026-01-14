#include "diagnostics.h"
#include "nvs_manager.h" // v11.106
#include "app_tasks.h" // Pour les TaskHandle_t en mode debug
#include <ArduinoJson.h>
#include <esp_core_dump.h>
#include <esp_partition.h>
#include <soc/rtc_cntl_reg.h>
#include <rom/rtc.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <time.h>

extern "C" esp_err_t esp_core_dump_image_get(size_t* out_addr,
                                             size_t* out_size) __attribute__((weak));
extern "C" esp_err_t esp_core_dump_image_erase(void) __attribute__((weak));

namespace {

const char* resetReasonToString(esp_reset_reason_t reason) {
  switch (reason) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW: return "SW";
    case ESP_RST_PANIC: return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT: return "TASK_WDT";
    case ESP_RST_WDT: return "WDT";
    case ESP_RST_DEEPSLEEP: return "DEEPSLEEP";
    case ESP_RST_BROWNOUT: return "BROWNOUT";
    case ESP_RST_SDIO: return "SDIO";
    default: return "OTHER";
  }
}

bool isBufferEmpty(const uint8_t* data, size_t len) {
  for (size_t i = 0; i < len; ++i) {
    if (data[i] != 0xFF) {
      return false;
    }
  }
  return true;
}

} // namespace

Diagnostics::Diagnostics() 
    : _lastUpdate(0),
      _lastHeapSave(0),
      _lastSavedMinHeap(UINT32_MAX),
      _bootRecorded(false) {
  _stats.uptimeSec = 0;
  _stats.freeHeap = 0;
  _stats.minFreeHeap = UINT32_MAX;
  _stats.maxAllocHeap = 0;
  _stats.freeStack = 0;
  _stats.rebootCount = 0;
  _stats.lastRebootReason = "UNKNOWN";
  _stats.taskStats = "";
  _stats.idlePercent = 255; // inconnu
  _stats.httpSuccessCount = 0;
  _stats.httpFailCount = 0;
  _stats.lastHttpCode = 0;
  _stats.otaSuccessCount = 0;
  _stats.otaFailCount = 0;
  _stats.lastOtaError = "";
  _stats.panicInfo.hasPanicInfo = false;
  _stats.crashStatus.hasCrashInfo = false;
  _stats.crashStatus.resetReason = -1;
  _stats.crashStatus.crashUptimeSec = 0;
  _stats.crashStatus.crashEpoch = 0;
  _stats.crashStatus.coredumpPresent = false;
  _stats.crashStatus.coredumpSize = 0;
  _stats.crashStatus.coredumpFormat = "UNKNOWN";
}

void Diagnostics::begin() {
  if (_bootRecorded) {
    return;
  }
  _bootRecorded = true;
  
  // Charger les valeurs précédentes
  int rebootCount;
  unsigned int minFreeHeap, httpSuccessCount, httpFailCount, otaSuccessCount, otaFailCount;

  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", rebootCount, 0);
  _stats.rebootCount = rebootCount + 1;
  
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_minHeap", (int&)_stats.minFreeHeap, UINT32_MAX);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_httpOk", (int&)_stats.httpSuccessCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_httpKo", (int&)_stats.httpFailCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_otaOk", (int&)_stats.otaSuccessCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_otaKo", (int&)_stats.otaFailCount, 0);

  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", _stats.rebootCount);

  // Initialisation des variables de suivi
  _lastSavedMinHeap = _stats.minFreeHeap;
  _lastHeapSave = millis();

  esp_reset_reason_t resetReason = esp_reset_reason();
  _stats.lastRebootReason = getRebootReason();
  
  loadCrashStatus();
  updateCoredumpStatus(false);
  
  // Crash: capturer les infos et les persister
  if (isCrashResetReason(resetReason)) {
    capturePanicInfo();
    if (_stats.panicInfo.hasPanicInfo) {
      savePanicInfo();
    }
    _stats.crashStatus.hasCrashInfo = true;
    _stats.crashStatus.resetReason = static_cast<int>(resetReason);
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS,
                           "diag_lastUptime",
                           _stats.crashStatus.crashUptimeSec,
                           0);
    time_t now = time(nullptr);
    _stats.crashStatus.crashEpoch = (now > 100000) ? static_cast<uint32_t>(now) : 0;
    updateCoredumpStatus(true);
  } else {
    // Reboot normal: charger les infos de panic/crash précédentes si présentes
    loadPanicInfo();
  }
  
  Serial.printf("[Diagnostics] 🚀 Initialisé - reboot #%u, minHeap: %u bytes", 
                _stats.rebootCount, _stats.minFreeHeap);
  if (_stats.panicInfo.hasPanicInfo) {
    Serial.printf(" [PANIC: %s]\n", _stats.panicInfo.exceptionCause.c_str());
  } else {
    Serial.println();
  }
}

void Diagnostics::loadFromNvs() {
  int rebootCount;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_rebootCnt", rebootCount, 0);
  _stats.rebootCount = rebootCount;
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_minHeap", (int&)_stats.minFreeHeap, UINT32_MAX);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_httpOk", (int&)_stats.httpSuccessCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_httpKo", (int&)_stats.httpFailCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_otaOk", (int&)_stats.otaSuccessCount, 0);
  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_otaKo", (int&)_stats.otaFailCount, 0);

  _stats.lastRebootReason = getRebootReason();
  loadCrashStatus();
  loadPanicInfo();
  updateCoredumpStatus(false);
}

String Diagnostics::getRebootReason() const {
  esp_reset_reason_t r = esp_reset_reason();
  return resetReasonToString(r);
}

bool Diagnostics::isCrashResetReason(esp_reset_reason_t reason) const {
  return (reason == ESP_RST_PANIC ||
          reason == ESP_RST_INT_WDT ||
          reason == ESP_RST_TASK_WDT ||
          reason == ESP_RST_WDT);
}

void Diagnostics::update() {
  unsigned long now = millis();
  if (now - _lastUpdate < UPDATE_INTERVAL_MS) return;
  _lastUpdate = now;

  _stats.uptimeSec = now / 1000;
  _stats.freeHeap = esp_get_free_heap_size();
  _stats.maxAllocHeap = heap_caps_get_largest_free_block(MALLOC_CAP_8BIT);
  _stats.freeStack = uxTaskGetStackHighWaterMark(nullptr);
  
  // Statistiques CPU par tâche (si config FreeRTOS le permet)
  char buf[1024];
  buf[0] = '\0';
  #if (configGENERATE_RUN_TIME_STATS == 1)
  vTaskGetRunTimeStats(buf);
  _stats.taskStats = String(buf);
  #else
  _stats.taskStats = "";
  #endif
  
  // Estimation Idle%: parse simple de taskStats (lignes "IDLE" et/ou "IDLE1/IDLE0")
  uint8_t idlePct = 255;
  if (_stats.taskStats.length() > 0) {
    // Cherche la dernière colonne en % sur les lignes contenant "IDLE"
    uint16_t sumIdle = 0;
    uint8_t cnt = 0;
    int idx = 0;
    while ((idx = _stats.taskStats.indexOf("IDLE", idx)) != -1) {
      int lineEnd = _stats.taskStats.indexOf('\n', idx);
      if (lineEnd == -1) lineEnd = _stats.taskStats.length();
      String line = _stats.taskStats.substring(idx, lineEnd);
      int pctPos = line.lastIndexOf('%');
      if (pctPos > 0) {
        // remonte pour trouver le début du nombre
        int start = pctPos - 1;
        while (start >= 0 && isdigit(line[start])) start--;
        start++;
        int len = pctPos - start;
        if (len > 0 && len <= 3) {
          uint16_t val = (uint16_t) line.substring(start, pctPos).toInt();
          sumIdle += val;
          cnt++;
        }
      }
      idx = lineEnd;
    }
    if (cnt > 0) {
      uint16_t avg = sumIdle / cnt;
      idlePct = (avg > 100) ? 100 : (uint8_t)avg;
    }
  }
  _stats.idlePercent = idlePct;
  
  // En mode debug: sauvegarder périodiquement uptime et heap pour diagnostic reboot
  #if defined(PROFILE_TEST) || defined(DEBUG_MODE)
  static unsigned long lastDiagnosticSave = 0;
  if (now - lastDiagnosticSave > 60000) { // Toutes les minutes
    g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_lastUptime", _stats.uptimeSec);
    g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_lastHeap", _stats.freeHeap);
    lastDiagnosticSave = now;
  }
  #endif
  
  // Optimisation NVS : sauvegarder le minHeap seulement si nécessaire
  if (_stats.freeHeap < _stats.minFreeHeap) {
    _stats.minFreeHeap = _stats.freeHeap;
    
    // Vérifier si une sauvegarde est nécessaire
    bool shouldSave = false;
    
    // 1. Première sauvegarde
    if (_lastHeapSave == 0) {
      shouldSave = true;
      Serial.println(F("[Diagnostics] 💾 Première sauvegarde minHeap"));
    }
    // 2. Sauvegarde si intervalle minimum écoulé ET différence significative
    else if ((now - _lastHeapSave > MIN_HEAP_SAVE_INTERVAL) && 
             (_lastSavedMinHeap - _stats.minFreeHeap > MIN_HEAP_DIFF_FOR_SAVE)) {
      shouldSave = true;
      Serial.printf("[Diagnostics] 💾 Sauvegarde minHeap (diff: %u bytes, interval: %lu ms)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap, now - _lastHeapSave);
    }
    // 3. Sauvegarde si perte très importante (plus de 10KB)
    else if (_lastSavedMinHeap - _stats.minFreeHeap > 10240) {
      shouldSave = true;
      Serial.printf("[Diagnostics] ⚠️ Sauvegarde minHeap forcée (perte importante: %u bytes)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap);
    }
    
    if (shouldSave) {
      g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_minHeap", _stats.minFreeHeap);
      
      _lastHeapSave = now;
      _lastSavedMinHeap = _stats.minFreeHeap;
      
      Serial.printf("[Diagnostics] ✅ minHeap sauvegardé: %u bytes\n", _stats.minFreeHeap);
    } else {
      Serial.printf("[Diagnostics] ⏭️ Sauvegarde minHeap ignorée (diff: %u bytes, interval: %lu ms)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap, now - _lastHeapSave);
    }
  }
}

//   doc["freeStack"] = _stats.freeStack;
//   doc["rebootCount"] = _stats.rebootCount;
//   doc["lastRebootReason"] = _stats.lastRebootReason;
//   if (_stats.idlePercent != 255) doc["idlePercent"] = _stats.idlePercent;
//   if (_stats.taskStats.length() > 0) doc["taskStats"] = _stats.taskStats;
//   // Réseau/OTA
//   doc["httpOk"] = _stats.httpSuccessCount;
//   doc["httpKo"] = _stats.httpFailCount;
//   doc["lastHttpCode"] = _stats.lastHttpCode;
//   doc["otaOk"] = _stats.otaSuccessCount;
//   doc["otaKo"] = _stats.otaFailCount;
//   if (_stats.lastOtaError.length() > 0) doc["lastOtaError"] = _stats.lastOtaError;
// }

void Diagnostics::toJson(ArduinoJson::JsonDocument& doc) const {
  doc["uptime"] = _stats.uptimeSec;
  doc["freeHeap"] = _stats.freeHeap;
  doc["minFreeHeap"] = _stats.minFreeHeap;
  doc["maxAllocHeap"] = _stats.maxAllocHeap;
  doc["freeStack"] = _stats.freeStack;
  doc["rebootCount"] = _stats.rebootCount;
  doc["lastRebootReason"] = _stats.lastRebootReason;
  if (_stats.idlePercent != 255) doc["idlePercent"] = _stats.idlePercent;
  if (_stats.taskStats.length() > 0) doc["taskStats"] = _stats.taskStats;
  // Réseau/OTA
  doc["httpOk"] = _stats.httpSuccessCount;
  doc["httpKo"] = _stats.httpFailCount;
  doc["lastHttpCode"] = _stats.lastHttpCode;
  doc["otaOk"] = _stats.otaSuccessCount;
  doc["otaKo"] = _stats.otaFailCount;
  if (_stats.lastOtaError.length() > 0) doc["lastOtaError"] = _stats.lastOtaError;
  if (_stats.crashStatus.hasCrashInfo || _stats.crashStatus.coredumpPresent) {
    ArduinoJson::JsonObject crash = doc["crash"].to<ArduinoJson::JsonObject>();
    crash["hasCrashInfo"] = _stats.crashStatus.hasCrashInfo;
    crash["resetReason"] = _stats.crashStatus.resetReason;
    crash["resetReasonLabel"] = resetReasonToString(
        static_cast<esp_reset_reason_t>(_stats.crashStatus.resetReason));
    crash["crashUptimeSec"] = _stats.crashStatus.crashUptimeSec;
    crash["crashEpoch"] = _stats.crashStatus.crashEpoch;
    crash["coredumpPending"] = _stats.crashStatus.coredumpPresent;
    crash["coredumpSize"] = _stats.crashStatus.coredumpSize;
    crash["coredumpFormat"] = _stats.crashStatus.coredumpFormat;
  }
} 

void Diagnostics::recordHttpResult(bool success, int httpCode) {
  _stats.lastHttpCode = httpCode;
  if (success) {
    _stats.httpSuccessCount++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_httpOk", _stats.httpSuccessCount);
  } else {
    _stats.httpFailCount++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_httpKo", _stats.httpFailCount);
  }
}

void Diagnostics::recordOtaResult(bool success, const char* errorMsg) {
  if (success) {
    _stats.otaSuccessCount++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_otaOk", _stats.otaSuccessCount);
    _stats.lastOtaError = "";
  } else {
    _stats.otaFailCount++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_otaKo", _stats.otaFailCount);
    _stats.lastOtaError = errorMsg ? String(errorMsg) : String("");
  }
}

String Diagnostics::generateRestartReport() const {
  String report;
  report.reserve(2048); // Augmenté pour inclure les infos de panic
  
  // Raison du redémarrage actuel
  esp_reset_reason_t resetReason = esp_reset_reason();
  report += "Raison du redémarrage: ";
  report += resetReasonToString(resetReason);
  report += " (code ";
  report += String((int)resetReason);
  report += ")";
  report += "\n";
  
  // Informations détaillées de PANIC si disponibles
  if (_stats.panicInfo.hasPanicInfo) {
    report += "\n⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️\n";
    report += "Exception: "; report += _stats.panicInfo.exceptionCause; report += "\n";
    report += "Adresse PC: 0x"; report += String(_stats.panicInfo.exceptionAddress, HEX); report += "\n";
    if (_stats.panicInfo.excvaddr != 0) {
      report += "Adresse mémoire fautive: 0x"; report += String(_stats.panicInfo.excvaddr, HEX); report += "\n";
    }
    if (_stats.panicInfo.taskName.length() > 0) {
      report += "Tâche: "; report += _stats.panicInfo.taskName; report += "\n";
    }
    if (_stats.panicInfo.core >= 0) {
      report += "CPU Core: "; report += String(_stats.panicInfo.core); report += "\n";
    }
    if (_stats.panicInfo.additionalInfo.length() > 0) {
      report += "Info supplémentaire: "; report += _stats.panicInfo.additionalInfo; report += "\n";
    }
    report += "\n";
  }

  if (_stats.crashStatus.hasCrashInfo) {
    report += "\n-- DERNIER CRASH ENREGISTRÉ --\n";
    report += "Reset reason: ";
    report += resetReasonToString(static_cast<esp_reset_reason_t>(_stats.crashStatus.resetReason));
    report += " (code ";
    report += String(_stats.crashStatus.resetReason);
    report += ")\n";
    if (_stats.crashStatus.crashUptimeSec > 0) {
      report += "Uptime avant crash: ";
      report += String(_stats.crashStatus.crashUptimeSec);
      report += " s\n";
    }
    if (_stats.crashStatus.crashEpoch > 0) {
      report += "Epoch crash/boot: ";
      report += String(_stats.crashStatus.crashEpoch);
      report += "\n";
    }
  }
  report += "Core Dump: ";
  if (_stats.crashStatus.coredumpPresent) {
    report += "PRÉSENT";
    if (_stats.crashStatus.coredumpSize > 0) {
      report += " (";
      report += String(_stats.crashStatus.coredumpSize);
      report += " bytes)";
    }
    if (_stats.crashStatus.coredumpFormat.length() > 0) {
      report += " - ";
      report += _stats.crashStatus.coredumpFormat;
    }
  } else {
    report += "ABSENT";
  }
  report += "\n";
  
  // Détails supplémentaires en mode DEBUG/PROFILE_TEST pour diagnostic
  #if defined(PROFILE_TEST) || defined(DEBUG_MODE)
  // Informations détaillées pour WATCHDOG
  if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) {
    report += "\n🔍 DÉTAILS WATCHDOG (Mode Debug) 🔍\n";
    
    // Informations sur l'uptime avant reboot (si disponible dans NVS)
    unsigned long lastUptime = 0;
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_lastUptime", lastUptime, 0);
    if (lastUptime > 0) {
      report += "Uptime avant reboot: "; 
      unsigned long hours = lastUptime / 3600;
      unsigned long mins = (lastUptime % 3600) / 60;
      unsigned long secs = lastUptime % 60;
      report += String(hours); report += "h "; 
      report += String(mins); report += "m "; 
      report += String(secs); report += "s\n";
    }
    
    // État mémoire avant reboot
    uint32_t lastHeap = 0;
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_lastHeap", lastHeap, 0);
    if (lastHeap > 0) {
      report += "Heap libre avant reboot: "; report += String(lastHeap); report += " bytes\n";
    }
    
    // Configuration watchdog
    report += "Watchdog timeout configuré: 300 secondes (5 minutes)\n";
    report += "Note: Une tâche n'a pas reset le watchdog pendant >300s\n";
    
    // Core Dump info
    #if CONFIG_ESP_COREDUMP_ENABLE
    report += "Core Dump: ACTIVÉ (Flash)\n";
    report += "Note: Utilisez 'pio run -t monitor' ou esp-coredump pour extraire le dump\n";
    #else
    report += "Core Dump: DÉSACTIVÉ\n";
    #endif

    report += "\n";
  }
  
  // Informations supplémentaires pour PANIC en mode debug
  if (_stats.panicInfo.hasPanicInfo) {
    report += "🔍 DÉTAILS COMPLÉMENTAIRES PANIC (Mode Debug) 🔍\n";
    report += "Reset reason code: "; report += String((int)resetReason); report += "\n";
    RESET_REASON rtcReason = rtc_get_reset_reason(0);
    report += "RTC reason code (Core 0): "; report += String((int)rtcReason); report += "\n";
    RESET_REASON rtcReason1 = rtc_get_reset_reason(1);
    if (rtcReason1 != NO_MEAN) {
      report += "RTC reason code (Core 1): "; report += String((int)rtcReason1); report += "\n";
    }
    report += "\n";
  }
  #endif
  
  // Informations sur le redémarrage
  report += "Compteur de redémarrages: "; report += String(_stats.rebootCount); report += "\n";
  report += "Dernière raison sauvegardée: "; report += _stats.lastRebootReason; report += "\n";
  
  // État de la mémoire
  report += "Heap libre au démarrage: "; report += String(ESP.getFreeHeap()); report += " bytes\n";
  report += "Heap libre minimum historique: "; report += String(_stats.minFreeHeap); report += " bytes\n";
  
  // Informations système
  report += "Modèle chip: "; report += ESP.getChipModel(); report += "\n";
  report += "Révision chip: "; report += String(ESP.getChipRevision()); report += "\n";
  report += "Nombre de cores: "; report += String(ESP.getChipCores()); report += "\n";
  report += "Fréquence CPU: "; report += String(getCpuFrequencyMhz()); report += " MHz\n";
  report += "Version SDK: "; report += ESP.getSdkVersion(); report += "\n";
  
  // Statistiques réseau/OTA
  report += "Requêtes HTTP réussies: "; report += String(_stats.httpSuccessCount); report += "\n";
  report += "Requêtes HTTP échouées: "; report += String(_stats.httpFailCount); report += "\n";
  report += "Mises à jour OTA réussies: "; report += String(_stats.otaSuccessCount); report += "\n";
  report += "Mises à jour OTA échouées: "; report += String(_stats.otaFailCount); report += "\n";
  if (_stats.lastOtaError.length() > 0) {
    report += "Dernière erreur OTA: "; report += _stats.lastOtaError; report += "\n";
  }
  
  return report;
}

// Capturer les informations de panic depuis la mémoire RTC
void Diagnostics::capturePanicInfo() {
  _stats.panicInfo.hasPanicInfo = false;
  
  // Vérifier si le dernier reset était un panic
  esp_reset_reason_t resetReason = esp_reset_reason();
  if (resetReason != ESP_RST_PANIC && 
      resetReason != ESP_RST_INT_WDT && 
      resetReason != ESP_RST_TASK_WDT) {
    return;
  }
  
  // Obtenir la raison RTC plus détaillée
  RESET_REASON rtcReason = rtc_get_reset_reason(0);
  
  _stats.panicInfo.hasPanicInfo = true;
  _stats.panicInfo.exceptionAddress = 0;
  _stats.panicInfo.excvaddr = 0;
  _stats.panicInfo.core = 0;
  _stats.panicInfo.taskName = "";
  _stats.panicInfo.additionalInfo = "";
  
  // Décoder la raison RTC en détails
  // Note: On utilise seulement les constantes disponibles dans toutes les versions du SDK
  switch (rtcReason) {
    case RTCWDT_RTC_RESET:
      _stats.panicInfo.exceptionCause = "RTC Watchdog Reset";
      break;
#ifdef TGWDT_CPU_RESET
    case TGWDT_CPU_RESET:
      _stats.panicInfo.exceptionCause = "Timer Group Watchdog (CPU)";
      break;
#endif
    case TG0WDT_SYS_RESET:
      _stats.panicInfo.exceptionCause = "Timer Group 0 Watchdog (System)";
      break;
    case TG1WDT_SYS_RESET:
      _stats.panicInfo.exceptionCause = "Timer Group 1 Watchdog (System)";
      break;
#ifdef SW_CPU_RESET
    case SW_CPU_RESET:
      _stats.panicInfo.exceptionCause = "Software CPU Reset";
      break;
#endif
    case RTCWDT_CPU_RESET:
      _stats.panicInfo.exceptionCause = "RTC Watchdog CPU Reset";
      break;
    case INTRUSION_RESET:
      _stats.panicInfo.exceptionCause = "Intrusion Test Reset";
      break;
    case DEEPSLEEP_RESET:
      _stats.panicInfo.exceptionCause = "Deep Sleep Reset";
      break;
#ifdef SDIO_RESET
    case SDIO_RESET:
      _stats.panicInfo.exceptionCause = "SDIO Reset";
      break;
#endif
    case POWERON_RESET:
      _stats.panicInfo.exceptionCause = "Power-On Reset";
      break;
#ifdef SW_RESET
    case SW_RESET:
      _stats.panicInfo.exceptionCause = "Software Reset";
      break;
#endif
    default:
      // Pour les codes inconnus, on affiche le code numérique
      _stats.panicInfo.exceptionCause = "Unknown Exception";
      _stats.panicInfo.additionalInfo += " (Reset code: " + String((int)rtcReason) + ")";
      break;
  }
  
  // Vérifier les deux cores pour identifier lequel a paniqué
  RESET_REASON rtcReason1 = rtc_get_reset_reason(1);
  if (rtcReason1 != rtcReason && rtcReason1 != NO_MEAN) {
    _stats.panicInfo.core = 1;
    if (_stats.panicInfo.additionalInfo.length() > 0) {
      _stats.panicInfo.additionalInfo += "; ";
    }
    _stats.panicInfo.additionalInfo += "Core 1 reason differs: " + String((int)rtcReason1);
  }
  
  // Ajouter le code ESP reset reason pour plus de détails
  if (_stats.panicInfo.additionalInfo.length() > 0) {
    _stats.panicInfo.additionalInfo += "; ";
  }
  _stats.panicInfo.additionalInfo += "ESP reset reason: " + String((int)resetReason);
  _stats.panicInfo.additionalInfo += ", RTC reason: " + String((int)rtcReason);
  
  // Note: Les détails supplémentaires (PC, registres, stack trace) nécessitent :
  // 1. Activation du Core Dump dans menuconfig
  // 2. Partition dédiée pour stocker le coredump
  // 3. Analyse post-mortem avec esp-coredump.py
  // 
  // Pour activer, ajouter dans platformio.ini:
  //   board_build.partitions = partitions_with_coredump.csv
  //   build_flags = -DCONFIG_ESP32_ENABLE_COREDUMP_TO_FLASH=1
  //
  // Pour l'instant, on capture ce qui est disponible via RTC memory
  
  Serial.printf("[Diagnostics] 🔍 Panic capturé: %s (Core %d)\n", 
                _stats.panicInfo.exceptionCause.c_str(), 
                _stats.panicInfo.core);
  Serial.printf("[Diagnostics] ℹ️ Pour plus de détails, consulter les logs série complets\n");
  Serial.printf("[Diagnostics] ℹ️ ou activer le Core Dump pour analyse post-mortem\n");
}

// Sauvegarder les informations de panic dans NVS
void Diagnostics::savePanicInfo() {
  if (!_stats.panicInfo.hasPanicInfo) return;
  
  g_nvsManager.saveBool(NVS_NAMESPACES::LOGS, "diag_hasPanic", true);
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicCause", _stats.panicInfo.exceptionCause);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_panicAddr", _stats.panicInfo.exceptionAddress);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_panicExcv", _stats.panicInfo.excvaddr);
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicTask", _stats.panicInfo.taskName);
  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_panicCore", _stats.panicInfo.core);
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicInfo", _stats.panicInfo.additionalInfo);
  
  Serial.println(F("[Diagnostics] 💾 Informations de panic sauvegardées"));
}

// Charger les informations de panic depuis NVS
void Diagnostics::loadPanicInfo() {
  g_nvsManager.loadBool(NVS_NAMESPACES::LOGS, "diag_hasPanic", _stats.panicInfo.hasPanicInfo, false);
  
  if (_stats.panicInfo.hasPanicInfo) {
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicCause", _stats.panicInfo.exceptionCause, "Unknown");
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicAddr", _stats.panicInfo.exceptionAddress, 0);
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicExcv", _stats.panicInfo.excvaddr, 0);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicTask", _stats.panicInfo.taskName, "");
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_panicCore", _stats.panicInfo.core, -1);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicInfo", _stats.panicInfo.additionalInfo, "");
    
    Serial.println(F("[Diagnostics] 📖 Informations de panic chargées depuis NVS"));
  }
  
  // NOTE: Ne pas nettoyer immédiatement - les infos sont nécessaires pour le rapport de boot
  // Le nettoyage sera fait après l'envoi du mail de boot (app.cpp)
}

// Nettoyer les informations de panic dans NVS
void Diagnostics::clearPanicInfo() {
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_hasPanic");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicCause");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicAddr");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicExcv");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicTask");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicCore");
  g_nvsManager.removeKey(NVS_NAMESPACES::LOGS, "diag_panicInfo");
  _stats.panicInfo.hasPanicInfo = false;
}

// Nettoyer les informations de panic après génération du rapport (méthode publique)
void Diagnostics::clearPanicInfoAfterReport() {
  if (_stats.panicInfo.hasPanicInfo) {
    clearPanicInfo();
    Serial.println(F("[Diagnostics] ✅ Infos PANIC nettoyées après rapport"));
  }
}

bool Diagnostics::clearCoreDump() {
#if defined(CONFIG_ESP_COREDUMP_ENABLE) && (CONFIG_ESP_COREDUMP_ENABLE == 1)
  bool cleared = false;
  if (esp_core_dump_image_erase) {
    cleared = (esp_core_dump_image_erase() == ESP_OK);
  }
  if (!cleared) {
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                           nullptr);
    if (part) {
      cleared = (esp_partition_erase_range(part, 0, part->size) == ESP_OK);
    }
  }

  if (cleared) {
    _stats.crashStatus.coredumpPresent = false;
    _stats.crashStatus.coredumpSize = 0;
    Serial.println(F("[Diagnostics] ✅ Core dump effacé"));
    if (_stats.crashStatus.hasCrashInfo) {
      saveCrashStatus();
    }
  } else {
    Serial.println(F("[Diagnostics] ❌ Échec effacement core dump"));
  }
  return cleared;
#else
  Serial.println(F("[Diagnostics] ⚠️ Core dump désactivé - pas d'effacement"));
  return false;
#endif
}

void Diagnostics::loadCrashStatus() {
  _stats.crashStatus.hasCrashInfo = false;
  _stats.crashStatus.resetReason = -1;
  _stats.crashStatus.crashUptimeSec = 0;
  _stats.crashStatus.crashEpoch = 0;
  _stats.crashStatus.coredumpPresent = false;
  _stats.crashStatus.coredumpSize = 0;
  _stats.crashStatus.coredumpFormat = "UNKNOWN";

  bool hasCrash = false;
  g_nvsManager.loadBool(NVS_NAMESPACES::LOGS, "crash_has", hasCrash, false);
  _stats.crashStatus.hasCrashInfo = hasCrash;
  if (!hasCrash) {
    return;
  }

  g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "crash_reason", _stats.crashStatus.resetReason, -1);
  g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "crash_uptime", _stats.crashStatus.crashUptimeSec, 0);
  g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "crash_epoch", _stats.crashStatus.crashEpoch, 0);
  g_nvsManager.loadBool(NVS_NAMESPACES::LOGS, "crash_coredump", _stats.crashStatus.coredumpPresent, false);
  g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "crash_cd_size", _stats.crashStatus.coredumpSize, 0);
  g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "crash_cd_fmt", _stats.crashStatus.coredumpFormat, "UNKNOWN");
}

void Diagnostics::saveCrashStatus() {
  if (!_stats.crashStatus.hasCrashInfo) {
    return;
  }
  g_nvsManager.saveBool(NVS_NAMESPACES::LOGS, "crash_has", true);
  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "crash_reason", _stats.crashStatus.resetReason);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "crash_uptime", _stats.crashStatus.crashUptimeSec);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "crash_epoch", _stats.crashStatus.crashEpoch);
  g_nvsManager.saveBool(NVS_NAMESPACES::LOGS, "crash_coredump", _stats.crashStatus.coredumpPresent);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "crash_cd_size", _stats.crashStatus.coredumpSize);
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "crash_cd_fmt", _stats.crashStatus.coredumpFormat);
}

void Diagnostics::updateCoredumpStatus(bool persistIfCrash) {
  _stats.crashStatus.coredumpPresent = false;
  _stats.crashStatus.coredumpSize = 0;

#if defined(CONFIG_ESP_COREDUMP_ENABLE) && (CONFIG_ESP_COREDUMP_ENABLE == 1)
  _stats.crashStatus.coredumpFormat = "UNKNOWN";
  #if defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF) && (CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF == 1)
  _stats.crashStatus.coredumpFormat = "ELF";
  #elif defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_BIN) && (CONFIG_ESP_COREDUMP_DATA_FORMAT_BIN == 1)
  _stats.crashStatus.coredumpFormat = "BIN";
  #endif

  size_t imageAddr = 0;
  size_t imageSize = 0;
  bool hasDump = false;
  if (esp_core_dump_image_get) {
    if (esp_core_dump_image_get(&imageAddr, &imageSize) == ESP_OK && imageSize > 0) {
      hasDump = true;
      _stats.crashStatus.coredumpSize = static_cast<uint32_t>(imageSize);
    }
  }
  if (!hasDump) {
    const esp_partition_t* part = esp_partition_find_first(ESP_PARTITION_TYPE_DATA,
                                                           ESP_PARTITION_SUBTYPE_DATA_COREDUMP,
                                                           nullptr);
    if (part) {
      uint8_t header[32];
      if (esp_partition_read(part, 0, header, sizeof(header)) == ESP_OK) {
        if (!isBufferEmpty(header, sizeof(header))) {
          hasDump = true;
          _stats.crashStatus.coredumpSize = static_cast<uint32_t>(part->size);
        }
      }
    }
  }
  _stats.crashStatus.coredumpPresent = hasDump;
#else
  _stats.crashStatus.coredumpFormat = "DISABLED";
#endif

  if (persistIfCrash && _stats.crashStatus.hasCrashInfo) {
    saveCrashStatus();
  }
}