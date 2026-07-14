// Mock Arduino minimal pour les tests natifs (hôte) des libs shared/.
// Fournit juste ce dont les tests ont besoin : analogRead injectable, delay
// no-op, types entiers, un String minimal (n3_data/n3_mail) et un Serial no-op.
// NE PAS utiliser hors tests.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <deque>
#include <string>

// --- File d'échantillons injectables pour analogRead (contrôle des tests) ---
inline std::deque<int>& n3MockAnalogQueue() {
  static std::deque<int> q;
  return q;
}
inline void n3MockAnalogPush(int value) { n3MockAnalogQueue().push_back(value); }
inline void n3MockAnalogClear() { n3MockAnalogQueue().clear(); }

// --- API Arduino mockée ---
inline int analogRead(uint8_t /*pin*/) {
  std::deque<int>& q = n3MockAnalogQueue();
  if (q.empty()) return 0;
  int v = q.front();
  q.pop_front();
  return v;
}
inline void delay(unsigned long /*ms*/) {}
inline void delayMicroseconds(unsigned int /*us*/) {}
inline unsigned long millis() { return 0; }

#ifndef F
#define F(x) (x)
#endif

// --- String minimal (sous-ensemble Arduino) utilisé par n3_data / n3_mail ---
// Suffisant pour la construction de body URL-encodé et les helpers de mail.
class String {
 public:
  String() = default;
  String(const char* s) : _s(s ? s : "") {}
  String(const std::string& s) : _s(s) {}
  String(char c) : _s(1, c) {}
  String(int v) : _s(std::to_string(v)) {}
  String(unsigned int v) : _s(std::to_string(v)) {}
  String(long v) : _s(std::to_string(v)) {}
  String(unsigned long v) : _s(std::to_string(v)) {}

  const char* c_str() const { return _s.c_str(); }
  size_t length() const { return _s.size(); }
  bool isEmpty() const { return _s.empty(); }
  void clear() { _s.clear(); }
  void reserve(size_t n) { _s.reserve(n); }

  char operator[](size_t i) const { return _s[i]; }

  // Sous-ensemble Arduino String::substring (indices bornés, endIndex exclusif).
  String substring(size_t beginIndex) const { return substring(beginIndex, _s.size()); }
  String substring(size_t beginIndex, size_t endIndex) const {
    if (beginIndex > _s.size()) beginIndex = _s.size();
    if (endIndex > _s.size()) endIndex = _s.size();
    if (beginIndex >= endIndex) return String();
    return String(_s.substr(beginIndex, endIndex - beginIndex));
  }

  String& operator+=(const String& o) { _s += o._s; return *this; }
  String& operator+=(const char* o) { if (o) _s += o; return *this; }
  String& operator+=(char c) { _s += c; return *this; }

  String& operator=(const char* o) { _s = (o ? o : ""); return *this; }

  bool operator==(const String& o) const { return _s == o._s; }
  bool operator<(const String& o) const { return _s < o._s; }

  friend String operator+(const String& a, const String& b) { return String(a._s + b._s); }
  friend String operator+(const String& a, const char* b) { return String(a._s + (b ? b : "")); }
  friend String operator+(const char* a, const String& b) { return String((a ? a : std::string()) + b._s); }

 private:
  std::string _s;
};

// --- Serial no-op (les libs loggent via Serial.printf / println) ---
struct MockSerial {
  template <typename... Args>
  void printf(const char* /*fmt*/, Args... /*args*/) {}
  void println(const char* /*s*/ = "") {}
  void print(const char* /*s*/ = "") {}
};
inline MockSerial Serial;

// --- ESP mock minimal (n3_data heartbeat lit ESP.getFreeHeap()) ---
// Valeur fixe déterministe pour des assertions stables en natif.
struct MockEsp {
  uint32_t getFreeHeap() { return 123456; }
};
inline MockEsp ESP;
