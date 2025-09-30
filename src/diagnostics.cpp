#include "diagnostics.h"
#include <ArduinoJson.h>

Diagnostics::Diagnostics() 
    : _lastUpdate(0), _lastHeapSave(0), _lastSavedMinHeap(UINT32_MAX) {
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
}

void Diagnostics::begin() {
  // Charger les valeurs précédentes
  _prefs.begin("diagnostics", false);
  _stats.rebootCount = _prefs.getUInt("rebootCnt", 0) + 1;
  _stats.minFreeHeap = _prefs.getUInt("minHeap", UINT32_MAX);
  _stats.httpSuccessCount = _prefs.getUInt("httpOk", 0);
  _stats.httpFailCount = _prefs.getUInt("httpKo", 0);
  _stats.otaSuccessCount = _prefs.getUInt("otaOk", 0);
  _stats.otaFailCount = _prefs.getUInt("otaKo", 0);
  _prefs.putUInt("rebootCnt", _stats.rebootCount);
  _prefs.end();

  // Initialisation des variables de suivi
  _lastSavedMinHeap = _stats.minFreeHeap;
  _lastHeapSave = millis();

  _stats.lastRebootReason = getRebootReason();
  
  Serial.printf("[Diagnostics] Initialisé - reboot #%u, minHeap: %u\n", 
                _stats.rebootCount, _stats.minFreeHeap);
}

String Diagnostics::getRebootReason() const {
  esp_reset_reason_t r = esp_reset_reason();
  switch (r) {
    case ESP_RST_POWERON: return "POWERON";
    case ESP_RST_SW:      return "SW";
    case ESP_RST_PANIC:   return "PANIC";
    case ESP_RST_INT_WDT: return "INT_WDT";
    case ESP_RST_TASK_WDT:return "TASK_WDT";
    case ESP_RST_WDT:     return "WDT";
    case ESP_RST_DEEPSLEEP:return "DEEPSLEEP";
    default: return "OTHER";
  }
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
  
  // Optimisation NVS : sauvegarder le minHeap seulement si nécessaire
  if (_stats.freeHeap < _stats.minFreeHeap) {
    _stats.minFreeHeap = _stats.freeHeap;
    
    // Vérifier si une sauvegarde est nécessaire
    bool shouldSave = false;
    
    // 1. Première sauvegarde
    if (_lastHeapSave == 0) {
      shouldSave = true;
      Serial.println(F("[Diagnostics] Première sauvegarde minHeap"));
    }
    // 2. Sauvegarde si intervalle minimum écoulé ET différence significative
    else if ((now - _lastHeapSave > MIN_HEAP_SAVE_INTERVAL) && 
             (_lastSavedMinHeap - _stats.minFreeHeap > MIN_HEAP_DIFF_FOR_SAVE)) {
      shouldSave = true;
      Serial.printf("[Diagnostics] Sauvegarde minHeap (diff: %u bytes, interval: %lu ms)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap, now - _lastHeapSave);
    }
    // 3. Sauvegarde si perte très importante (plus de 10KB)
    else if (_lastSavedMinHeap - _stats.minFreeHeap > 10240) {
      shouldSave = true;
      Serial.printf("[Diagnostics] Sauvegarde minHeap forcée (perte importante: %u bytes)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap);
    }
    
    if (shouldSave) {
      _prefs.begin("diagnostics", false);
      _prefs.putUInt("minHeap", _stats.minFreeHeap);
      _prefs.end();
      
      _lastHeapSave = now;
      _lastSavedMinHeap = _stats.minFreeHeap;
      
      Serial.printf("[Diagnostics] minHeap sauvegardé: %u bytes\n", _stats.minFreeHeap);
    } else {
      Serial.printf("[Diagnostics] Sauvegarde minHeap ignorée (diff: %u bytes, interval: %lu ms)\n", 
                    _lastSavedMinHeap - _stats.minFreeHeap, now - _lastHeapSave);
    }
  }
}

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
} 

void Diagnostics::recordHttpResult(bool success, int httpCode) {
  _stats.lastHttpCode = httpCode;
  _prefs.begin("diagnostics", false);
  if (success) {
    _stats.httpSuccessCount++;
    _prefs.putUInt("httpOk", _stats.httpSuccessCount);
  } else {
    _stats.httpFailCount++;
    _prefs.putUInt("httpKo", _stats.httpFailCount);
  }
  _prefs.end();
}

void Diagnostics::recordOtaResult(bool success, const char* errorMsg) {
  _prefs.begin("diagnostics", false);
  if (success) {
    _stats.otaSuccessCount++;
    _prefs.putUInt("otaOk", _stats.otaSuccessCount);
    _stats.lastOtaError = "";
  } else {
    _stats.otaFailCount++;
    _prefs.putUInt("otaKo", _stats.otaFailCount);
    _stats.lastOtaError = errorMsg ? String(errorMsg) : String("");
  }
  _prefs.end();
}