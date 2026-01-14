#pragma once

// Mocks pour les fonctions Arduino utilisées dans les tests unitaires
// À inclure dans chaque fichier de test

#ifdef UNIT_TEST

#include <cstdint>
#include <cstdio>
#include <cstring>

// Mocks pour millis() et micros()
// C++17 inline variables: une seule instance partagée entre translation units
inline unsigned long g_mock_millis = 0;
inline unsigned long g_mock_micros = 0;

inline unsigned long millis() {
  return g_mock_millis;
}

inline unsigned long micros() {
  return g_mock_micros;
}

inline void setup_mock_time(unsigned long ms, unsigned long us = 0) {
  g_mock_millis = ms;
  g_mock_micros = us;
}

inline void advance_time(unsigned long ms, unsigned long us = 0) {
  g_mock_millis += ms;
  g_mock_micros += us;
  if (g_mock_micros >= 1000000) {
    g_mock_millis += g_mock_micros / 1000;
    g_mock_micros %= 1000000;
  }
}

inline void reset_mock_time() {
  g_mock_millis = 0;
  g_mock_micros = 0;
}

// Mock pour Serial
class HardwareSerial {
public:
  void begin(unsigned long) {}
  void print(const char*) {}
  void println(const char*) {}
  void printf(const char*, ...) {}
  template<typename T> void print(T) {}
  template<typename T> void println(T) {}
};

// Définir Serial globalement (utilisé dans le code source)
static HardwareSerial Serial;

// Types Arduino basiques
using byte = uint8_t;
using word = uint16_t;

// Macro pour delay (no-op en test)
#define delay(ms) ((void)0)

// Définitions minimales si nécessaires
#ifndef HIGH
#define HIGH 1
#define LOW 0
#endif

#ifndef INPUT
#define INPUT 0
#define OUTPUT 1
#endif

#endif // UNIT_TEST
