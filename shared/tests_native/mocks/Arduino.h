// Mock Arduino minimal pour les tests natifs (hôte) des libs shared/.
// Fournit juste ce dont n3_analog_sensors a besoin : analogRead injectable,
// delay no-op, types entiers. NE PAS utiliser hors tests.
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <deque>

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
