#pragma once
/**
 * @file i2c_bus.h
 * @brief Bus I2C partagé : init unique + mutex pour sérialiser les accès (OLED, BME280, scan, futurs RTC/SD/INA).
 *
 * Usage:
 *   I2CBus::init();  // une fois au boot, avant tout usage Wire
 *   {
 *     I2CBusGuard guard;  // lock en entrée, unlock en sortie (RAII)
 *     if (!guard) return;  // échec acquisition
 *     // ... appels Wire ou libs (Adafruit_SSD1306, Adafruit_BME280, etc.)
 *   }
 *   // ou manuel : if (I2CBus::lock(500)) { ...; I2CBus::unlock(); }
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Arduino.h>
#include "config.h"
#include "pins.h"

namespace SensorConfig {
namespace I2CBusConfig {
  inline constexpr uint32_t LOCK_TIMEOUT_MS = 500;
  /// Fréquence I2C en Hz. 100000 = mode standard (100 kHz), plus fiable pour OLED erratique / câbles longs.
  /// 400000 possible si câblage court et pull-ups corrects (voir bonnes pratiques I2C).
  inline constexpr uint32_t I2C_CLOCK_HZ = 100000;
}
}

class I2CBus {
 public:
  /**
   * Initialise le bus I2C une seule fois (Wire.begin + délai stabilisation) et crée le mutex.
   * À appeler avant tout usage de Wire (display, scan, sensors).
   */
  static void init();

  /**
   * Prend le mutex I2C avec timeout.
   * @param timeoutMs Timeout en ms (0 = essai immédiat)
   * @return true si le mutex est acquis
   */
  static bool lock(uint32_t timeoutMs = SensorConfig::I2CBusConfig::LOCK_TIMEOUT_MS);

  /**
   * Relâche le mutex I2C.
   */
  static void unlock();

 private:
  static SemaphoreHandle_t _mutex;
  static bool _inited;
};

/**
 * Guard RAII : prend le mutex au constructeur, le relâche au destructeur.
 * Vérifier avec if (!guard) si l'acquisition a échoué avant d'utiliser Wire.
 */
class I2CBusGuard {
 public:
  explicit I2CBusGuard(uint32_t timeoutMs = SensorConfig::I2CBusConfig::LOCK_TIMEOUT_MS)
      : _held(I2CBus::lock(timeoutMs)) {}

  ~I2CBusGuard() {
    if (_held) {
      I2CBus::unlock();
    }
  }

  I2CBusGuard(const I2CBusGuard&) = delete;
  I2CBusGuard& operator=(const I2CBusGuard&) = delete;

  /** true si le mutex a été acquis */
  explicit operator bool() const { return _held; }

 private:
  bool _held;
};

