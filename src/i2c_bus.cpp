/**
 * @file i2c_bus.cpp
 * @brief Implémentation du bus I2C partagé (mutex + init unique).
 *
 * Bonnes pratiques I2C (OLED / BME280 erratiques) :
 * - Câblage : VCC→3.3V, GND→GND, SDA→GPIO SDA, SCL→GPIO SCL. L'ordre des broches
 *   varie selon les modules (VCC/GND/SCL/SDA ou autre) : vérifier le marquage du module.
 * - Pull-ups : I2C est open-drain ; des pull-ups externes (2.2kΩ–4.7kΩ sur SDA et SCL)
 *   sont recommandés si câbles longs ou plusieurs périphériques. Les modules OLED ont
 *   souvent des résistances intégrées ; en cas d'affichage erratique, ajouter 4.7kΩ
 *   vers 3.3V sur SDA et SCL peut stabiliser le bus.
 * - Fréquence : 100 kHz (I2C_CLOCK_HZ) pour fiabilité ; 400 kHz possible si câblage court.
 * - Longueur des câbles : garder SDA/SCL courts (< 30 cm si possible).
 */

#include "i2c_bus.h"
#include <Wire.h>

SemaphoreHandle_t I2CBus::_mutex = nullptr;
bool I2CBus::_inited = false;

void I2CBus::init() {
  if (_inited) return;
  Wire.begin(Pins::I2C_SDA, Pins::I2C_SCL);
  Wire.setClock(static_cast<uint32_t>(SensorConfig::I2CBusConfig::I2C_CLOCK_HZ));
  delay(SensorConfig::I2C_STABILIZATION_DELAY_MS);
  _mutex = xSemaphoreCreateMutex();
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  // #region agent log
  Serial.printf("[I2C-DBG] init done mutex=%s\n", _mutex != nullptr ? "ok" : "null");
  // #endregion
#endif
  if (_mutex == nullptr) {
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
    Serial.println(F("[I2C] ERREUR: impossible de créer le mutex I2C"));
#endif
  }
  _inited = true;
}

bool I2CBus::lock(uint32_t timeoutMs) {
  if (_mutex == nullptr) {
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
    Serial.println(F("[I2C-DBG] lock fail mutex_null"));
#endif
    return false;
  }
  BaseType_t ok = xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs));
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)) || !defined(PROFILE_PROD)
  if (ok != pdTRUE) Serial.println(F("[I2C-DBG] lock fail timeout"));
#endif
  return (ok == pdTRUE);
}

void I2CBus::unlock() {
  if (_mutex != nullptr) {
    xSemaphoreGive(_mutex);
  }
}
