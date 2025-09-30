#include "event_log.h"
#include <stdarg.h>

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
    xSemaphoreTake(s_mutex, portMAX_DELAY);
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
  xSemaphoreTake(s_mutex, portMAX_DELAY);
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

uint32_t EventLog::dumpSince(uint32_t sinceSeq, String& out, size_t maxChars) {
  if (!s_mutex) return 0;
  xSemaphoreTake(s_mutex, portMAX_DELAY);
  uint32_t maxSeq = s_seq;
  if (out.length() > 0) out.reserve(out.length() + 128);
  size_t produced = 0;

  // Determine start
  size_t count = s_filled ? kCapacity : s_head;
  for (size_t i = 0; i < count; ++i) {
    const Entry& e = s_buffer[(s_head + i) % kCapacity]; // chronological order
    if (e.seq == 0) continue;
    if ((int32_t)(e.seq - sinceSeq) <= 0) continue; // only newer
    size_t len = strlen(e.msg);
    if (produced + len + 1 > maxChars) break;
    out += e.msg;
    out += '\n';
    produced += len + 1;
  }
  xSemaphoreGive(s_mutex);
  return maxSeq;
}

uint32_t EventLog::currentSeq() {
  return s_seq;
}


