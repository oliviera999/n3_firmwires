/**
 * n3_bme280 — capteur air BME280 (température / humidité / pression) sur I2C,
 * avec détection à l'init : si le module est absent, present() reste faux et
 * l'appelant garde son chemin DHT existant (aucun reflash par station, la même
 * image firmware sert aux montages DHT et BME280).
 *
 * Pattern repris du firmware ffp5cs (src/sensor_air.cpp, garde
 * USE_AIR_SENSOR_AUTO : sonde BME280 au boot, repli DHT sinon), mutualisé
 * ici pour n3pp et msp. Adresses : 0x76 (SDO à GND, défaut) / 0x77 (SDO à VDD).
 */
#pragma once

#include <Arduino.h>
#include <Adafruit_BME280.h>

class N3Bme280 {
 public:
  /** Sonde le capteur (Wire.begin() inclus, idempotent sur ESP32).
   *  À appeler au setup, APRÈS la mise sous tension du rail capteurs.
   *  Renvoie true si un BME280 répond à cette adresse. */
  bool begin(uint8_t i2cAddress);

  /** Capteur détecté au dernier begin() ? */
  bool present() const { return _ok; }

  /** Lecture bornée : temp -40..85 °C, humidité 0..100 %, pression
   *  300..1100 hPa. Renvoie false (sans toucher aux sorties) si le capteur
   *  est absent ou si une valeur sort des bornes physiques. */
  bool read(float& tempC, float& humidityPct, float& pressureHpa);

 private:
  Adafruit_BME280 _bme;
  bool _ok = false;
};
