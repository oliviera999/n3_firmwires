#pragma once

#include <Arduino.h>

/**
 * Interface abstraite pour un capteur IoT n3.
 * Chaque driver lit sa valeur et la retourne sous forme de chaine.
 */
class N3SensorDriver {
public:
  virtual ~N3SensorDriver() = default;
  virtual const char* getName() = 0;
  /** Lit le capteur et retourne la valeur en chaine. Validation isnan/plages selon le type. */
  virtual String readValue() = 0;
};

/**
 * Capteur DHT (temperature ou humidite).
 * Fallback 20.0f / 50.0f si isnan (harmonise n3pp, msp, ffp5cs).
 */
class N3DhtSensor : public N3SensorDriver {
public:
  enum class Type { TEMPERATURE, HUMIDITY };

  /** outOptional : si non null, la valeur lue y est ecrite (pour automation/affichage) */
  N3DhtSensor(void* dht, Type type, const char* name, float* outOptional = nullptr);
  const char* getName() override;
  String readValue() override;

private:
  void* _dht;
  Type _type;
  const char* _name;
  float* _out;
};

/**
 * Capteur analogique (humidite sol, luminosite, pont diviseur).
 * Valeur brute 0-4095. Si 0, retourne "1" pour eviter division par zero cote serveur.
 */
class N3AnalogSensor : public N3SensorDriver {
public:
  /** outOptional : si non null, la valeur lue y est ecrite (pour automation/affichage) */
  N3AnalogSensor(int pin, const char* name, int* outOptional = nullptr);
  const char* getName() override;
  String readValue() override;

private:
  int _pin;
  const char* _name;
  int* _out;
};

#if defined(N3_IOT_DS18B20)
/**
 * Capteur DS18B20 (temperature eau/sol).
 * Fallback 20.0f si -127 ou defaut.
 * Nécessite build_flags = -DN3_IOT_DS18B20 et lib_deps OneWire + DallasTemperature.
 */
class N3Ds18b20Sensor : public N3SensorDriver {
public:
  /** sensors : DallasTemperature*, index : 0-based */
  N3Ds18b20Sensor(void* sensors, int index, const char* name, float* outOptional = nullptr);
  const char* getName() override;
  String readValue() override;

private:
  void* _sensors;
  int _index;
  const char* _name;
  float* _out;
};
#endif
