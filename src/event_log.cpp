#include "event_log.h"
#include <stdarg.h>
#include <cstring>

EventLog::Entry EventLog::s_buffer[EventLog::kCapacity];
size_t EventLog::s_head = 0;
bool EventLog::s_filled = false;
uint32_t EventLog::s_seq = 0;
SemaphoreHandle_t EventLog::s_mutex = nullptr;

void EventLog::begin() {
  if (s_mutex == nullptr) {
    s_mutex = xSemaphoreCreateMutex();
  }
  if (s_mutex) {
    // CORRECTION : Timeout au lieu de portMAX_DELAY pour éviter les deadlocks
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
      Serial.println("[EventLog] ⚠️ Mutex timeout dans begin(), skip");
      return;
    }
    s_head = 0;
    s_filled = false;
    s_seq = 0;
    for (size_t i = 0; i < kCapacity; ++i) {
      s_buffer[i].seq = 0;
      s_buffer[i].tsMs = 0;
      s_buffer[i].msg[0] = '\0';
    }
    xSemaphoreGive(s_mutex);
  }
}

void EventLog::appendInternal(const char* msg) {
  if (!s_mutex) return; // not initialized
  
  // CORRECTION : Timeout au lieu de portMAX_DELAY pour éviter les deadlocks
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    Serial.println("[EventLog] ⚠️ Mutex timeout dans appendInternal(), skip");
    return;
  }
  size_t idx = s_head;
  s_head = (s_head + 1) % kCapacity;
  if (s_head == 0) s_filled = true;
  uint32_t nextSeq = ++s_seq;
  Entry& e = s_buffer[idx];
  e.seq = nextSeq;
  e.tsMs = millis();
  // Format: "[1234567ms] message"
  char buf[sizeof(e.msg)];
  int n = snprintf(buf, sizeof(buf), "[%lu ms] %s", (unsigned long)e.tsMs, msg ? msg : "");
  if (n < 0) n = 0;
  if ((size_t)n >= sizeof(e.msg)) {
    n = sizeof(e.msg) - 1;
  }
  memcpy(e.msg, buf, (size_t)n);
  e.msg[n] = '\0';
  xSemaphoreGive(s_mutex);
}

void EventLog::add(const char* msg) {
  appendInternal(msg);
}

void EventLog::addf(const char* fmt, ...) {
  char tmp[100];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(tmp, sizeof(tmp), fmt, ap);
  va_end(ap);
  appendInternal(tmp);
}

uint32_t EventLog::dumpSince(uint32_t sinceSeq, char* out, size_t outSize, size_t maxChars) {
  if (!s_mutex || !out || outSize == 0) return 0;
  
  // CORRECTION : Timeout au lieu de portMAX_DELAY pour éviter les deadlocks
  if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) != pdTRUE) {
    Serial.println("[EventLog] ⚠️ Mutex timeout dans dumpSince(), skip");
    return 0;
  }
  uint32_t maxSeq = s_seq;
  size_t produced = 0;
  out[0] = '\0';  // Initialize output buffer

  // Determine start
  size_t count = s_filled ? kCapacity : s_head;
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = s_buffer[(s_head + i) % kCapacity]; // chronological order
    if (e.seq == 0) continue;
    if ((int32_t)(e.seq - sinceSeq) <= 0) continue; // only newer
    size_t len = strlen(e.msg);
    if (produced + len + 1 > maxChars || produced + len + 1 >= outSize) break;
    
    // Append message and newline
    strncat(out, e.msg, outSize - produced - 1);
    produced += len;
    if (produced + 1 < outSize) {
      out[produced] = '\n';
      out[produced + 1] = '\0';
      produced++;
    }
  }
  xSemaphoreGive(s_mutex);
  return maxSeq;
}

uint32_t EventLog::currentSeq() {
  return s_seq;
}


