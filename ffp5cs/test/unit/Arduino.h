#pragma once

// -----------------------------------------------------------------------------
// Minimal Arduino API shim for PlatformIO native unit tests
// -----------------------------------------------------------------------------
// This header is intentionally tiny: it provides only what the current unit
// tests (TimerManager / RateLimiter) need to compile on the host.
//
// It is picked up via env:native build_flags: -Itest/unit
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>
#include <cstring>
#include <string>

#ifdef UNIT_TEST
#include "test_mocks.h"
#endif

// Arduino compatibility
#ifndef F
#define F(x) (x)
#endif

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

  bool startsWith(const String& prefix) const {
    return _s.rfind(prefix._s, 0) == 0;
  }

  bool startsWith(const char* prefix) const {
    if (!prefix) return false;
    return _s.rfind(prefix, 0) == 0;
  }

  friend String operator+(const String& a, const String& b) {
    return String(a._s + b._s);
  }

  friend String operator+(const String& a, const char* b) {
    return String(a._s + (b ? b : ""));
  }

  friend String operator+(const char* a, const String& b) {
    return String((a ? a : std::string()) + b._s);
  }

  bool operator<(const String& other) const { return _s < other._s; }
  bool operator==(const String& other) const { return _s == other._s; }

 private:
  std::string _s;
};

