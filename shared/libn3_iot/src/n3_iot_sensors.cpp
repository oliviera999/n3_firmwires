#include "n3_iot_sensors.h"
#include "n3_analog_sensors.h"
#include <DHT.h>
#include <cmath>

#if defined(N3_IOT_DS18B20)
#include <DallasTemperature.h>
#endif

#define DHT_FALLBACK_TEMP 20.0f
#define DHT_FALLBACK_HUMID 50.0f

// Bornes physiques DHT (DHT11/DHT22).
static const float N3_IOT_DHT_TEMP_MIN = -40.0f;
static const float N3_IOT_DHT_TEMP_MAX = 80.0f;
static const float N3_IOT_DHT_HUM_MIN = 0.0f;
static const float N3_IOT_DHT_HUM_MAX = 100.0f;

/* --- N3DhtSensor --- */
N3DhtSensor::N3DhtSensor(void* dht, Type type, const char* name, float* outOptional)
  : _dht(dht), _type(type), _name(name), _out(outOptional) {}

const char* N3DhtSensor::getName() {
  return _name;
}

String N3DhtSensor::readValue() {
  DHT* d = static_cast<DHT*>(_dht);
  float v;
  if (_type == Type::TEMPERATURE) {
    v = d->readTemperature();
    // v1.1 : valider isnan + bornes physiques (avant : isnan uniquement)
    if (isnan(v) || v < N3_IOT_DHT_TEMP_MIN || v > N3_IOT_DHT_TEMP_MAX) {
      v = DHT_FALLBACK_TEMP;
    }
  } else {
    v = d->readHumidity();
    if (isnan(v) || v < N3_IOT_DHT_HUM_MIN || v > N3_IOT_DHT_HUM_MAX) {
      v = DHT_FALLBACK_HUMID;
    }
  }
  if (_out) *_out = v;
  delay(500);  /* DHT demande ~2s entre lectures ; 500ms minimum entre temp et humid */
  return String(v);
}

/* --- N3AnalogSensor --- */
N3AnalogSensor::N3AnalogSensor(int pin, const char* name, int* outOptional)
  : _pin(pin), _name(name), _out(outOptional) {}

const char* N3AnalogSensor::getName() {
  return _name;
}

String N3AnalogSensor::readValue() {
  // v1.1 : utilise n3_analog_sensors (mediane + rejet outliers) au lieu d'un
  // simple analogRead brut. Configuration par defaut : 8 echantillons, 2 ms,
  // mediane puis moyenne, plage 0-4095, fallback 1.
  N3Analog::AnalogConfig cfg = {
    .pin = (uint8_t)_pin, .numSamples = 8, .delayMs = 2,
    .filterMode = N3Analog::MEDIANE_PUIS_MOYENNE, .outlierMax = 100,
    .minValid = 0, .maxValid = 4095, .fallback = 1, .emaAlpha = 0.0f
  };
  N3Analog::AnalogResult r = N3Analog::readFilteredAnalog(cfg);
  int v = r.valid ? (int)r.value : 1;
  if (v == 0) v = 1;
  if (_out) *_out = v;
  return String(v);
}

#if defined(N3_IOT_DS18B20)
/* --- N3Ds18b20Sensor --- */
#define DS18B20_FALLBACK 20.0f

N3Ds18b20Sensor::N3Ds18b20Sensor(void* sensors, int index, const char* name, float* outOptional)
  : _sensors(sensors), _index(index), _name(name), _out(outOptional) {}

const char* N3Ds18b20Sensor::getName() {
  return _name;
}

String N3Ds18b20Sensor::readValue() {
  DallasTemperature* s = static_cast<DallasTemperature*>(_sensors);
  s->requestTemperatures();
  float v = s->getTempCByIndex((uint8_t)_index);
  // v1.1 : test explicite DEVICE_DISCONNECTED_C (au lieu de la valeur magique
  // -127.0f) ; le retry sur 25.00 est retire (25 C est une vraie temperature).
  if (v == DEVICE_DISCONNECTED_C || isnan(v)) {
    delay(200);
    s->requestTemperatures();
    v = s->getTempCByIndex((uint8_t)_index);
  }
  if (v == DEVICE_DISCONNECTED_C || isnan(v) || v < -50.0f || v > 100.0f) {
    v = DS18B20_FALLBACK;
  }
  if (_out) *_out = v;
  return String(v);
}
#endif
