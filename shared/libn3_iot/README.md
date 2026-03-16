# libn3_iot — Bibliothèque IoT générique n3

Bibliothèque partagée pour les firmwares ESP32 du projet n3 (n3pp, msp, futurs projets). Fournit des drivers de capteurs réutilisables et un registre pour faciliter l’intégration de nouveaux systèmes avec des capteurs et actionneurs différents.

## Rôle

- **N3SensorDriver** : interface abstraite pour tout capteur (lecture → chaîne)
- **N3DhtSensor** : DHT (température ou humidité), fallback 20°C / 50 % si `isnan`
- **N3AnalogSensor** : lecture analogique (humidité sol, luminosité, pont diviseur)
- **N3Ds18b20Sensor** : DS18B20 (température eau/sol)
- **N3SensorRegistry** : enregistrement des capteurs et lecture en lot

## Utilisation

```cpp
#include "n3_iot_sensors.h"
#include "n3_iot_registry.h"

// Créer les capteurs (avec sortie optionnelle pour mise à jour de variables globales)
N3DhtSensor sensorTemp(&dht, N3DhtSensor::Type::TEMPERATURE, "TempAir", &temperatureAir);
N3AnalogSensor sensorHumid(pin, "Humid1", &Humid1);

// Enregistrer et lire
N3SensorRegistry registry;
registry.add(&sensorTemp);
registry.add(&sensorHumid);

N3DataField buf[N3_IOT_MAX_FIELDS];
size_t n = registry.readAll(buf, N3_IOT_MAX_FIELDS);
// Les variables temperatureAir et Humid1 sont mises à jour
// buf contient les paires (name, value) pour le POST
```

## Projets utilisant libn3_iot

- **n3pp4_2** : DHT, 4× humidité sol, luminosité, pont diviseur
- **msp2_5** : 2× DHT (int/ext), DS18B20, humidité sol, pluie, pont diviseur

## Dépendances

- adafruit/DHT sensor library
- n3_data (POST/GET HTTP)
- Pour N3Ds18b20Sensor : OneWire + DallasTemperature, et `build_flags = -DN3_IOT_DS18B20` (ex. msp2_5)

## Fichiers

- `src/n3_iot_sensors.h`, `n3_iot_sensors.cpp` : drivers DHT, analogique, DS18B20
- `src/n3_iot_registry.h`, `n3_iot_registry.cpp` : registre et lecture en lot
