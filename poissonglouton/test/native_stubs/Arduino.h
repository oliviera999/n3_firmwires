#pragma once

// -----------------------------------------------------------------------------
// Shim Arduino minimal pour les tests natifs (hote) de poissonglouton.
// Calque sur ffp5cs/test/unit/Arduino.h et shared/tests_native/mocks/Arduino.h.
// Fournit uniquement ce dont pgl_counter.cpp / pgl_event_journal.cpp ont besoin
// sur l'hote : types entiers, millis() controlable, Serial no-op, String minimal.
// NE PAS utiliser hors tests.
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cstdarg>
#include <string>
#include <cmath>

// --- Horloge mockee (controlable depuis les tests) -------------------------
// Variables inline C++17 : une seule instance partagee entre TUs.
inline unsigned long g_pgl_mock_millis = 0;
inline unsigned long millis() { return g_pgl_mock_millis; }
inline void pglMockSetMillis(unsigned long ms) { g_pgl_mock_millis = ms; }
inline void pglMockAdvanceMillis(unsigned long ms) { g_pgl_mock_millis += ms; }

inline void delay(unsigned long /*ms*/) {}
inline void delayMicroseconds(unsigned int /*us*/) {}

#ifndef F
#define F(x) (x)
#endif

#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif
#ifndef INPUT
#define INPUT 0
#define OUTPUT 1
#endif

using byte = uint8_t;
using word = uint16_t;

// --- Serial no-op ----------------------------------------------------------
struct MockSerial {
  void begin(unsigned long) {}
  void printf(const char* /*fmt*/, ...) {}
  void print(const char* = "") {}
  void println(const char* = "") {}
  template <typename T> void print(T) {}
  template <typename T> void println(T) {}
};
inline MockSerial Serial;

// --- ESP no-op (pgl_log.h::pglLogMemory y fait reference, jamais appele ici) -
struct MockEsp {
  unsigned long getFreeHeap() { return 0; }
  unsigned long getMinFreeHeap() { return 0; }
  unsigned long getFreePsram() { return 0; }
  unsigned long getFlashChipSize() { return 0; }
  int getChipRevision() { return 0; }
  int getCpuFreqMHz() { return 0; }
};
inline MockEsp ESP;

// --- String minimal --------------------------------------------------------
class String {
 public:
  String() = default;
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned int v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}

  const char* c_str() const { return _s.c_str(); }
  size_t length() const { return _s.size(); }
  bool operator==(const String& o) const { return _s == o._s; }
  bool operator<(const String& o) const { return _s < o._s; }
  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, const char* b) { return String(a._s + (b ? b : "")); }

 private:
  std::string _s;
};
