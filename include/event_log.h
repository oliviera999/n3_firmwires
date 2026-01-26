#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

// Lightweight ring buffer event logger (no dynamic allocations)
class EventLog {
 public:
  // Initialize mutex and reset buffer. Safe to call multiple times.
  static void begin();

  // Append an event with printf-style formatting. Timestamp is millis().
  static void addf(const char* fmt, ...);

  // Append a raw event message (C-string). Timestamp is millis().
  static void add(const char* msg);

  // Dump events with sequence number strictly greater than sinceSeq into out.
  // Returns the highest sequence number included in the dump.
  static uint32_t dumpSince(uint32_t sinceSeq, char* out, size_t outSize, size_t maxChars = 8192);

  // Return the current highest sequence number (monotonic, wraps at 32-bit).
  static uint32_t currentSeq();

 private:
  struct Entry {
    uint32_t seq;       // monotonically increasing sequence id
    uint32_t tsMs;      // millis() at insert time
    char     msg[120];  // message (truncated if needed)
  };

  static constexpr size_t kCapacity = 64; // ~64 * 120 = 7.5 KB plus overhead

  static Entry s_buffer[kCapacity];
  static size_t s_head;      // next position to write
  static bool   s_filled;    // has wrapped at least once
  static uint32_t s_seq;     // last assigned sequence id
  static SemaphoreHandle_t s_mutex;

  static void appendInternal(const char* msg);
};


