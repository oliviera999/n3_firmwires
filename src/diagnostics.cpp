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
#include <cstring>

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
  memset(_stats.lastRebootReason, 0, sizeof(_stats.lastRebootReason));
  strncpy(_stats.lastRebootReason, "UNKNOWN", sizeof(_stats.lastRebootReason) - 1);
  memset(_stats.taskStats, 0, sizeof(_stats.taskStats));
  _stats.idlePercent = 255; // inconnu
  _stats.httpSuccessCount = 0;
  _stats.httpFailCount = 0;
  _stats.lastHttpCode = 0;
  _stats.otaSuccessCount = 0;
  _stats.otaFailCount = 0;
  memset(_stats.lastOtaError, 0, sizeof(_stats.lastOtaError));
  _stats.panicInfo.hasPanicInfo = false;
  memset(_stats.panicInfo.exceptionCause, 0, sizeof(_stats.panicInfo.exceptionCause));
  _stats.panicInfo.exceptionAddress = 0;
  _stats.panicInfo.excvaddr = 0;
  memset(_stats.panicInfo.taskName, 0, sizeof(_stats.panicInfo.taskName));
  _stats.panicInfo.core = 0;
  memset(_stats.panicInfo.additionalInfo, 0, sizeof(_stats.panicInfo.additionalInfo));
  _stats.crashStatus.hasCrashInfo = false;
  _stats.crashStatus.resetReason = -1;
  _stats.crashStatus.crashUptimeSec = 0;
  _stats.crashStatus.crashEpoch = 0;
  _stats.crashStatus.coredumpPresent = false;
  _stats.crashStatus.coredumpSize = 0;
  memset(_stats.crashStatus.coredumpFormat, 0, sizeof(_stats.crashStatus.coredumpFormat));
  strncpy(_stats.crashStatus.coredumpFormat, "UNKNOWN", sizeof(_stats.crashStatus.coredumpFormat) - 1);
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
  const char* reasonStr = getRebootReason();
  strncpy(_stats.lastRebootReason, reasonStr, sizeof(_stats.lastRebootReason) - 1);
  _stats.lastRebootReason[sizeof(_stats.lastRebootReason) - 1] = '\0';
  
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
    Serial.printf(" [PANIC: %s]\n", _stats.panicInfo.exceptionCause);
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

  const char* reasonStr = getRebootReason();
  strncpy(_stats.lastRebootReason, reasonStr, sizeof(_stats.lastRebootReason) - 1);
  _stats.lastRebootReason[sizeof(_stats.lastRebootReason) - 1] = '\0';
  loadCrashStatus();
  loadPanicInfo();
  updateCoredumpStatus(false);
}

const char* Diagnostics::getRebootReason() const {
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
  strncpy(_stats.taskStats, buf, sizeof(_stats.taskStats) - 1);
  _stats.taskStats[sizeof(_stats.taskStats) - 1] = '\0';
  #else
  _stats.taskStats[0] = '\0';
  #endif
  
  // Estimation Idle%: parse simple de taskStats (lignes "IDLE" et/ou "IDLE1/IDLE0")
  // v11.156: Utilisation de char[] au lieu de String pour éviter fragmentation mémoire
  uint8_t idlePct = 255;
  const size_t taskStatsLen = strlen(_stats.taskStats);
  if (taskStatsLen > 0) {
    const char* taskStatsPtr = _stats.taskStats;
    // Cherche la dernière colonne en % sur les lignes contenant "IDLE"
    uint16_t sumIdle = 0;
    uint8_t cnt = 0;
    const char* searchPtr = taskStatsPtr;
    while ((searchPtr = strstr(searchPtr, "IDLE")) != nullptr) {
      int idx = searchPtr - taskStatsPtr;
      const char* lineEndPtr = strchr(searchPtr, '\n');
      int lineEnd = (lineEndPtr != nullptr) ? (lineEndPtr - taskStatsPtr) : taskStatsLen;
      
      // Buffer fixe au lieu de String::substring() pour éviter allocations
      int lineLen = lineEnd - idx;
      if (lineLen > 0 && lineLen < 120) {  // Limite raisonnable pour une ligne
        char line[120];
        strncpy(line, taskStatsPtr + idx, lineLen);
        line[lineLen] = '\0';
        
        // Chercher le '%' depuis la fin de la ligne
        int pctPos = -1;
        for (int i = lineLen - 1; i >= 0; i--) {
          if (line[i] == '%') {
            pctPos = i;
            break;
          }
        }
        
        if (pctPos > 0) {
          // Remonter pour trouver le début du nombre
          int start = pctPos - 1;
          while (start >= 0 && isdigit(line[start])) start--;
          start++;
          int len = pctPos - start;
          if (len > 0 && len <= 3) {
            // Extraire le nombre avec atoi() au lieu de substring().toInt()
            char numStr[4];
            strncpy(numStr, line + start, len);
            numStr[len] = '\0';
            uint16_t val = (uint16_t) atoi(numStr);
            sumIdle += val;
            cnt++;
          }
        }
      }
      searchPtr = (lineEndPtr != nullptr) ? lineEndPtr + 1 : taskStatsPtr + taskStatsLen;
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
  if (strlen(_stats.taskStats) > 0) doc["taskStats"] = _stats.taskStats;
  // Réseau/OTA
  doc["httpOk"] = _stats.httpSuccessCount;
  doc["httpKo"] = _stats.httpFailCount;
  doc["lastHttpCode"] = _stats.lastHttpCode;
  doc["otaOk"] = _stats.otaSuccessCount;
  doc["otaKo"] = _stats.otaFailCount;
  if (strlen(_stats.lastOtaError) > 0) doc["lastOtaError"] = _stats.lastOtaError;
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
    _stats.lastOtaError[0] = '\0';
  } else {
    _stats.otaFailCount++;
    g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_otaKo", _stats.otaFailCount);
    if (errorMsg) {
      strncpy(_stats.lastOtaError, errorMsg, sizeof(_stats.lastOtaError) - 1);
      _stats.lastOtaError[sizeof(_stats.lastOtaError) - 1] = '\0';
    } else {
      _stats.lastOtaError[0] = '\0';
    }
  }
}

void Diagnostics::generateRestartReport(char* buffer, size_t bufferSize) const {
  if (!buffer || bufferSize == 0) return;
  
  buffer[0] = '\0';
  size_t offset = 0;
  size_t remaining = bufferSize;
  int written = 0;
  
  // Raison du redémarrage actuel
  esp_reset_reason_t resetReason = esp_reset_reason();
  written = snprintf(buffer + offset, remaining, 
                    "Raison du redémarrage: %s (code %d)\n",
                    resetReasonToString(resetReason), (int)resetReason);
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  // Informations détaillées de PANIC si disponibles
  if (_stats.panicInfo.hasPanicInfo) {
    written = snprintf(buffer + offset, remaining,
                      "\n⚠️ DÉTAILS DU PANIC (Guru Meditation) ⚠️\n"
                      "Exception: %s\n"
                      "Adresse PC: 0x%08X\n",
                      _stats.panicInfo.exceptionCause, _stats.panicInfo.exceptionAddress);
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    if (_stats.panicInfo.excvaddr != 0) {
      written = snprintf(buffer + offset, remaining,
                         "Adresse mémoire fautive: 0x%08X\n", _stats.panicInfo.excvaddr);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    if (strlen(_stats.panicInfo.taskName) > 0) {
      written = snprintf(buffer + offset, remaining, "Tâche: %s\n", _stats.panicInfo.taskName);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    if (_stats.panicInfo.core >= 0) {
      written = snprintf(buffer + offset, remaining, "CPU Core: %d\n", _stats.panicInfo.core);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    if (strlen(_stats.panicInfo.additionalInfo) > 0) {
      written = snprintf(buffer + offset, remaining, "Info supplémentaire: %s\n", 
                        _stats.panicInfo.additionalInfo);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    written = snprintf(buffer + offset, remaining, "\n");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
  }

  if (_stats.crashStatus.hasCrashInfo) {
    written = snprintf(buffer + offset, remaining,
                      "\n-- DERNIER CRASH ENREGISTRÉ --\n"
                      "Reset reason: %s (code %d)\n",
                      resetReasonToString(static_cast<esp_reset_reason_t>(_stats.crashStatus.resetReason)),
                      _stats.crashStatus.resetReason);
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    if (_stats.crashStatus.crashUptimeSec > 0) {
      written = snprintf(buffer + offset, remaining, "Uptime avant crash: %u s\n",
                         _stats.crashStatus.crashUptimeSec);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    if (_stats.crashStatus.crashEpoch > 0) {
      written = snprintf(buffer + offset, remaining, "Epoch crash/boot: %u\n",
                         _stats.crashStatus.crashEpoch);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
  }
  
  written = snprintf(buffer + offset, remaining, "Core Dump: ");
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  if (_stats.crashStatus.coredumpPresent) {
    written = snprintf(buffer + offset, remaining, "PRÉSENT");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    if (_stats.crashStatus.coredumpSize > 0) {
      written = snprintf(buffer + offset, remaining, " (%u bytes)", _stats.crashStatus.coredumpSize);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    if (strlen(_stats.crashStatus.coredumpFormat) > 0) {
      written = snprintf(buffer + offset, remaining, " - %s", _stats.crashStatus.coredumpFormat);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
  } else {
    written = snprintf(buffer + offset, remaining, "ABSENT");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
  }
  
  written = snprintf(buffer + offset, remaining, "\n");
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  // Détails supplémentaires en mode DEBUG/PROFILE_TEST pour diagnostic
  #if defined(PROFILE_TEST) || defined(DEBUG_MODE)
  // Informations détaillées pour WATCHDOG
  if (resetReason == ESP_RST_TASK_WDT || resetReason == ESP_RST_INT_WDT || resetReason == ESP_RST_WDT) {
    written = snprintf(buffer + offset, remaining, "\n🔍 DÉTAILS WATCHDOG (Mode Debug) 🔍\n");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    // Informations sur l'uptime avant reboot (si disponible dans NVS)
    unsigned long lastUptime = 0;
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_lastUptime", lastUptime, 0);
    if (lastUptime > 0) {
      unsigned long hours = lastUptime / 3600;
      unsigned long mins = (lastUptime % 3600) / 60;
      unsigned long secs = lastUptime % 60;
      written = snprintf(buffer + offset, remaining, "Uptime avant reboot: %luh %lum %lus\n",
                         hours, mins, secs);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    // État mémoire avant reboot
    uint32_t lastHeap = 0;
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_lastHeap", lastHeap, 0);
    if (lastHeap > 0) {
      written = snprintf(buffer + offset, remaining, "Heap libre avant reboot: %u bytes\n", lastHeap);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    // Configuration watchdog
    written = snprintf(buffer + offset, remaining,
                      "Watchdog timeout configuré: 300 secondes (5 minutes)\n"
                      "Note: Une tâche n'a pas reset le watchdog pendant >300s\n");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    // Core Dump info
    #if CONFIG_ESP_COREDUMP_ENABLE
    written = snprintf(buffer + offset, remaining,
                      "Core Dump: ACTIVÉ (Flash)\n"
                      "Note: Utilisez 'pio run -t monitor' ou esp-coredump pour extraire le dump\n");
    #else
    written = snprintf(buffer + offset, remaining, "Core Dump: DÉSACTIVÉ\n");
    #endif
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;

    written = snprintf(buffer + offset, remaining, "\n");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
  }
  
  // Informations supplémentaires pour PANIC en mode debug
  if (_stats.panicInfo.hasPanicInfo) {
    written = snprintf(buffer + offset, remaining,
                      "🔍 DÉTAILS COMPLÉMENTAIRES PANIC (Mode Debug) 🔍\n"
                      "Reset reason code: %d\n", (int)resetReason);
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    RESET_REASON rtcReason = rtc_get_reset_reason(0);
    written = snprintf(buffer + offset, remaining, "RTC reason code (Core 0): %d\n", (int)rtcReason);
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
    
    RESET_REASON rtcReason1 = rtc_get_reset_reason(1);
    if (rtcReason1 != NO_MEAN) {
      written = snprintf(buffer + offset, remaining, "RTC reason code (Core 1): %d\n", (int)rtcReason1);
      if (written < 0 || (size_t)written >= remaining) {
        buffer[bufferSize - 1] = '\0';
        return;
      }
      offset += written;
      remaining -= written;
    }
    
    written = snprintf(buffer + offset, remaining, "\n");
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
  }
  #endif
  
  // Informations sur le redémarrage
  written = snprintf(buffer + offset, remaining,
                    "Compteur de redémarrages: %u\n"
                    "Dernière raison sauvegardée: %s\n",
                    _stats.rebootCount, _stats.lastRebootReason);
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  // État de la mémoire
  written = snprintf(buffer + offset, remaining,
                    "Heap libre au démarrage: %u bytes\n"
                    "Heap libre minimum historique: %u bytes\n",
                    ESP.getFreeHeap(), _stats.minFreeHeap);
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  // Informations système
  written = snprintf(buffer + offset, remaining,
                    "Modèle chip: %s\n"
                    "Révision chip: %d\n"
                    "Nombre de cores: %d\n"
                    "Fréquence CPU: %d MHz\n"
                    "Version SDK: %s\n",
                    ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(),
                    getCpuFrequencyMhz(), ESP.getSdkVersion());
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  // Statistiques réseau/OTA
  written = snprintf(buffer + offset, remaining,
                    "Requêtes HTTP réussies: %u\n"
                    "Requêtes HTTP échouées: %u\n"
                    "Mises à jour OTA réussies: %u\n"
                    "Mises à jour OTA échouées: %u\n",
                    _stats.httpSuccessCount, _stats.httpFailCount,
                    _stats.otaSuccessCount, _stats.otaFailCount);
  if (written < 0 || (size_t)written >= remaining) {
    buffer[bufferSize - 1] = '\0';
    return;
  }
  offset += written;
  remaining -= written;
  
  if (strlen(_stats.lastOtaError) > 0) {
    written = snprintf(buffer + offset, remaining, "Dernière erreur OTA: %s\n", _stats.lastOtaError);
    if (written < 0 || (size_t)written >= remaining) {
      buffer[bufferSize - 1] = '\0';
      return;
    }
    offset += written;
    remaining -= written;
  }
  
  // S'assurer que le buffer est terminé par \0
  buffer[offset] = '\0';
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
  _stats.panicInfo.taskName[0] = '\0';
  _stats.panicInfo.additionalInfo[0] = '\0';
  
  // Décoder la raison RTC en détails
  // Note: On utilise seulement les constantes disponibles dans toutes les versions du SDK
  const char* exceptionCauseStr = nullptr;
  switch (rtcReason) {
    case RTCWDT_RTC_RESET:
      exceptionCauseStr = "RTC Watchdog Reset";
      break;
#ifdef TGWDT_CPU_RESET
    case TGWDT_CPU_RESET:
      exceptionCauseStr = "Timer Group Watchdog (CPU)";
      break;
#endif
    case TG0WDT_SYS_RESET:
      exceptionCauseStr = "Timer Group 0 Watchdog (System)";
      break;
    case TG1WDT_SYS_RESET:
      exceptionCauseStr = "Timer Group 1 Watchdog (System)";
      break;
#ifdef SW_CPU_RESET
    case SW_CPU_RESET:
      exceptionCauseStr = "Software CPU Reset";
      break;
#endif
    case RTCWDT_CPU_RESET:
      exceptionCauseStr = "RTC Watchdog CPU Reset";
      break;
    case INTRUSION_RESET:
      exceptionCauseStr = "Intrusion Test Reset";
      break;
    case DEEPSLEEP_RESET:
      exceptionCauseStr = "Deep Sleep Reset";
      break;
#ifdef SDIO_RESET
    case SDIO_RESET:
      exceptionCauseStr = "SDIO Reset";
      break;
#endif
    case POWERON_RESET:
      exceptionCauseStr = "Power-On Reset";
      break;
#ifdef SW_RESET
    case SW_RESET:
      exceptionCauseStr = "Software Reset";
      break;
#endif
    default:
      // Pour les codes inconnus, on affiche le code numérique
      exceptionCauseStr = "Unknown Exception";
      snprintf(_stats.panicInfo.additionalInfo, sizeof(_stats.panicInfo.additionalInfo),
               " (Reset code: %d)", (int)rtcReason);
      break;
  }
  if (exceptionCauseStr) {
    strncpy(_stats.panicInfo.exceptionCause, exceptionCauseStr, sizeof(_stats.panicInfo.exceptionCause) - 1);
    _stats.panicInfo.exceptionCause[sizeof(_stats.panicInfo.exceptionCause) - 1] = '\0';
  }
  
  // Vérifier les deux cores pour identifier lequel a paniqué
  RESET_REASON rtcReason1 = rtc_get_reset_reason(1);
  if (rtcReason1 != rtcReason && rtcReason1 != NO_MEAN) {
    _stats.panicInfo.core = 1;
    size_t currentLen = strlen(_stats.panicInfo.additionalInfo);
    if (currentLen > 0) {
      snprintf(_stats.panicInfo.additionalInfo + currentLen, 
               sizeof(_stats.panicInfo.additionalInfo) - currentLen,
               "; Core 1 reason differs: %d", (int)rtcReason1);
    } else {
      snprintf(_stats.panicInfo.additionalInfo, sizeof(_stats.panicInfo.additionalInfo),
               "Core 1 reason differs: %d", (int)rtcReason1);
    }
  }
  
  // Ajouter le code ESP reset reason pour plus de détails
  size_t currentLen = strlen(_stats.panicInfo.additionalInfo);
  if (currentLen > 0) {
    snprintf(_stats.panicInfo.additionalInfo + currentLen,
             sizeof(_stats.panicInfo.additionalInfo) - currentLen,
             "; ESP reset reason: %d, RTC reason: %d", (int)resetReason, (int)rtcReason);
  } else {
    snprintf(_stats.panicInfo.additionalInfo, sizeof(_stats.panicInfo.additionalInfo),
             "ESP reset reason: %d, RTC reason: %d", (int)resetReason, (int)rtcReason);
  }
  
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
                _stats.panicInfo.exceptionCause, 
                _stats.panicInfo.core);
  Serial.printf("[Diagnostics] ℹ️ Pour plus de détails, consulter les logs série complets\n");
  Serial.printf("[Diagnostics] ℹ️ ou activer le Core Dump pour analyse post-mortem\n");
}

// Sauvegarder les informations de panic dans NVS
void Diagnostics::savePanicInfo() {
  if (!_stats.panicInfo.hasPanicInfo) return;
  
  g_nvsManager.saveBool(NVS_NAMESPACES::LOGS, "diag_hasPanic", true);
  String tempStr = _stats.panicInfo.exceptionCause;
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicCause", tempStr);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_panicAddr", _stats.panicInfo.exceptionAddress);
  g_nvsManager.saveULong(NVS_NAMESPACES::LOGS, "diag_panicExcv", _stats.panicInfo.excvaddr);
  tempStr = _stats.panicInfo.taskName;
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicTask", tempStr);
  g_nvsManager.saveInt(NVS_NAMESPACES::LOGS, "diag_panicCore", _stats.panicInfo.core);
  tempStr = _stats.panicInfo.additionalInfo;
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "diag_panicInfo", tempStr);
  
  Serial.println(F("[Diagnostics] 💾 Informations de panic sauvegardées"));
}

// Charger les informations de panic depuis NVS
void Diagnostics::loadPanicInfo() {
  g_nvsManager.loadBool(NVS_NAMESPACES::LOGS, "diag_hasPanic", _stats.panicInfo.hasPanicInfo, false);
  
  if (_stats.panicInfo.hasPanicInfo) {
    String tempStr;
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicCause", tempStr, "Unknown");
    strncpy(_stats.panicInfo.exceptionCause, tempStr.c_str(), sizeof(_stats.panicInfo.exceptionCause) - 1);
    _stats.panicInfo.exceptionCause[sizeof(_stats.panicInfo.exceptionCause) - 1] = '\0';
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicAddr", _stats.panicInfo.exceptionAddress, 0);
    g_nvsManager.loadULong(NVS_NAMESPACES::LOGS, "diag_panicExcv", _stats.panicInfo.excvaddr, 0);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicTask", tempStr, "");
    strncpy(_stats.panicInfo.taskName, tempStr.c_str(), sizeof(_stats.panicInfo.taskName) - 1);
    _stats.panicInfo.taskName[sizeof(_stats.panicInfo.taskName) - 1] = '\0';
    g_nvsManager.loadInt(NVS_NAMESPACES::LOGS, "diag_panicCore", _stats.panicInfo.core, -1);
    g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "diag_panicInfo", tempStr, "");
    strncpy(_stats.panicInfo.additionalInfo, tempStr.c_str(), sizeof(_stats.panicInfo.additionalInfo) - 1);
    _stats.panicInfo.additionalInfo[sizeof(_stats.panicInfo.additionalInfo) - 1] = '\0';
    
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
  strncpy(_stats.crashStatus.coredumpFormat, "UNKNOWN", sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';

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
  String tempStr;
  g_nvsManager.loadString(NVS_NAMESPACES::LOGS, "crash_cd_fmt", tempStr, "UNKNOWN");
  strncpy(_stats.crashStatus.coredumpFormat, tempStr.c_str(), sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';
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
  String tempStr = _stats.crashStatus.coredumpFormat;
  g_nvsManager.saveString(NVS_NAMESPACES::LOGS, "crash_cd_fmt", tempStr);
}

void Diagnostics::updateCoredumpStatus(bool persistIfCrash) {
  _stats.crashStatus.coredumpPresent = false;
  _stats.crashStatus.coredumpSize = 0;

#if defined(CONFIG_ESP_COREDUMP_ENABLE) && (CONFIG_ESP_COREDUMP_ENABLE == 1)
  strncpy(_stats.crashStatus.coredumpFormat, "UNKNOWN", sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';
  #if defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF) && (CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF == 1)
  strncpy(_stats.crashStatus.coredumpFormat, "ELF", sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';
  #elif defined(CONFIG_ESP_COREDUMP_DATA_FORMAT_BIN) && (CONFIG_ESP_COREDUMP_DATA_FORMAT_BIN == 1)
  strncpy(_stats.crashStatus.coredumpFormat, "BIN", sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';
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
  strncpy(_stats.crashStatus.coredumpFormat, "DISABLED", sizeof(_stats.crashStatus.coredumpFormat) - 1);
  _stats.crashStatus.coredumpFormat[sizeof(_stats.crashStatus.coredumpFormat) - 1] = '\0';
#endif

  if (persistIfCrash && _stats.crashStatus.hasCrashInfo) {
    saveCrashStatus();
  }
}