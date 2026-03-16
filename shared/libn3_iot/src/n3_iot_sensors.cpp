#include "n3_iot_sensors.h"
#include <DHT.h>
#include <cmath>

#if defined(N3_IOT_DS18B20)
#include <DallasTemperature.h>
#endif

#define DHT_FALLBACK_TEMP 20.0f
#define DHT_FALLBACK_HUMID 50.0f

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
    if (isnan(v)) v = DHT_FALLBACK_TEMP;
  } else {
    v = d->readHumidity();
    if (isnan(v)) v = DHT_FALLBACK_HUMID;
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
  int v = analogRead(_pin);
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
  if (v == -127.0f) {
    delay(200);
    v = s->getTempCByIndex((uint8_t)_index);
  }
  if (v == 25.0f) {
    delay(200);
    v = s->getTempCByIndex((uint8_t)_index);
  }
  if (isnan(v) || v < -50.0f || v > 100.0f) v = DS18B20_FALLBACK;
  if (_out) *_out = v;
  return String(v);
}
#endif
