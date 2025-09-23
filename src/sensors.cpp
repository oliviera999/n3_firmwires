#include "sensors.h"
#include "project_config.h"
#include <algorithm>

SensorManager* SensorManager::instance = nullptr;

// Base Sensor implementation
Sensor::Sensor(const String& name, SensorType type, int pin) 
    : name(name), type(type), pin(pin), enabled(true), offset(0), scale(1.0), 
      lastReadTime(0), readInterval(SENSOR_READ_INTERVAL) {
    lastReading.type = type;
    lastReading.valid = false;
}

void Sensor::calibrate(float offset, float scale) {
    this->offset = offset;
    this->scale = scale;
}

// DHT Sensor implementation
DHTSensor::DHTSensor(const String& name, int pin, uint8_t type) 
    : Sensor(name, SENSOR_TEMPERATURE, pin), dhtType(type) {
    dht = new DHT(pin, type);
}

DHTSensor::~DHTSensor() {
    delete dht;
}

bool DHTSensor::init() {
    dht->begin();
    delay(2000); // DHT sensor needs time to stabilize
    
    // Test read
    float t = dht->readTemperature();
    float h = dht->readHumidity();
    
    if (isnan(t) || isnan(h)) {
        Serial.printf("DHT sensor init failed on pin %d\n", pin);
        return false;
    }
    
    Serial.printf("DHT sensor initialized: T=%.1f°C, H=%.1f%%\n", t, h);
    return true;
}

SensorReading DHTSensor::read() {
    return readTemperature(); // Default to temperature
}

SensorReading DHTSensor::readTemperature() {
    SensorReading reading;
    reading.type = SENSOR_TEMPERATURE;
    reading.timestamp = millis();
    
    float t = dht->readTemperature();
    
    if (isnan(t)) {
        reading.valid = false;
        reading.error = "Failed to read temperature";
    } else {
        reading.rawValue = t;
        reading.value = (t + offset) * scale;
        reading.valid = true;
        reading.unit = "°C";
    }
    
    lastReading = reading;
    return reading;
}

SensorReading DHTSensor::readHumidity() {
    SensorReading reading;
    reading.type = SENSOR_HUMIDITY;
    reading.timestamp = millis();
    
    float h = dht->readHumidity();
    
    if (isnan(h)) {
        reading.valid = false;
        reading.error = "Failed to read humidity";
    } else {
        reading.rawValue = h;
        reading.value = (h + offset) * scale;
        reading.valid = true;
        reading.unit = "%";
    }
    
    return reading;
}

// DS18B20 Sensor implementation
DS18B20Sensor::DS18B20Sensor(const String& name, int pin) 
    : Sensor(name, SENSOR_TEMPERATURE, pin) {
    oneWire = new OneWire(pin);
    sensors = new DallasTemperature(oneWire);
}

DS18B20Sensor::~DS18B20Sensor() {
    delete sensors;
    delete oneWire;
}

bool DS18B20Sensor::init() {
    sensors->begin();
    
    if (sensors->getDeviceCount() == 0) {
        Serial.printf("No DS18B20 found on pin %d\n", pin);
        return false;
    }
    
    if (!sensors->getAddress(deviceAddress, 0)) {
        Serial.println("Unable to find address for DS18B20");
        return false;
    }
    
    sensors->setResolution(deviceAddress, 12); // 12-bit resolution
    
    // Test read
    sensors->requestTemperatures();
    float t = sensors->getTempC(deviceAddress);
    
    if (t == DEVICE_DISCONNECTED_C) {
        Serial.println("DS18B20 read error");
        return false;
    }
    
    Serial.printf("DS18B20 initialized: T=%.2f°C\n", t);
    return true;
}

SensorReading DS18B20Sensor::read() {
    SensorReading reading;
    reading.type = SENSOR_TEMPERATURE;
    reading.timestamp = millis();
    
    sensors->requestTemperatures();
    float t = sensors->getTempC(deviceAddress);
    
    if (t == DEVICE_DISCONNECTED_C) {
        reading.valid = false;
        reading.error = "Sensor disconnected";
    } else {
        reading.rawValue = t;
        reading.value = (t + offset) * scale;
        reading.valid = true;
        reading.unit = "°C";
    }
    
    lastReading = reading;
    return reading;
}

// Analog Sensor implementation
AnalogSensor::AnalogSensor(const String& name, SensorType type, int pin)
    : Sensor(name, type, pin), minValue(0), maxValue(4095), 
      minOutput(0), maxOutput(100), numSamples(10) {
}

bool AnalogSensor::init() {
    pinMode(pin, INPUT);
    
    // Test read
    int value = analogRead(pin);
    Serial.printf("Analog sensor on pin %d initialized: raw=%d\n", pin, value);
    
    return true;
}

SensorReading AnalogSensor::read() {
    SensorReading reading;
    reading.type = type;
    reading.timestamp = millis();
    
    // Average multiple samples for stability
    long sum = 0;
    for (int i = 0; i < numSamples; i++) {
        sum += analogRead(pin);
        delay(10);
    }
    int avgValue = sum / numSamples;
    
    // Map to output range
    float mappedValue = map(avgValue, minValue, maxValue, minOutput * 100, maxOutput * 100) / 100.0;
    
    reading.rawValue = avgValue;
    reading.value = (mappedValue + offset) * scale;
    reading.valid = true;
    
    // Set unit based on sensor type
    switch (type) {
        case SENSOR_SOIL_MOISTURE:
            reading.unit = "%";
            break;
        case SENSOR_LIGHT:
            reading.unit = "lux";
            break;
        case SENSOR_WATER_LEVEL:
            reading.unit = "%";
            break;
        default:
            reading.unit = "";
    }
    
    lastReading = reading;
    return reading;
}

void AnalogSensor::setRange(int min, int max, float minOut, float maxOut) {
    minValue = min;
    maxValue = max;
    minOutput = minOut;
    maxOutput = maxOut;
}

void AnalogSensor::setSampling(int samples) {
    numSamples = samples;
}

// Sensor Manager implementation
SensorManager::SensorManager() 
    : historySize(SENSOR_CACHE_SIZE), autoRead(false), 
      autoReadInterval(5000), lastAutoRead(0) {
}

SensorManager* SensorManager::getInstance() {
    if (instance == nullptr) {
        instance = new SensorManager();
    }
    return instance;
}

SensorManager::~SensorManager() {
    for (auto sensor : sensors) {
        delete sensor;
    }
    sensors.clear();
}

void SensorManager::addSensor(Sensor* sensor) {
    sensors.push_back(sensor);
}

void SensorManager::removeSensor(const String& name) {
    sensors.erase(
        std::remove_if(sensors.begin(), sensors.end(),
            [&name](Sensor* s) { 
                if (s->getName() == name) {
                    delete s;
                    return true;
                }
                return false;
            }),
        sensors.end()
    );
}

Sensor* SensorManager::getSensor(const String& name) {
    for (auto sensor : sensors) {
        if (sensor->getName() == name) {
            return sensor;
        }
    }
    return nullptr;
}

std::vector<Sensor*> SensorManager::getSensorsByType(SensorType type) {
    std::vector<Sensor*> result;
    for (auto sensor : sensors) {
        if (sensor->getType() == type) {
            result.push_back(sensor);
        }
    }
    return result;
}

bool SensorManager::initAll() {
    bool allSuccess = true;
    for (auto sensor : sensors) {
        if (!sensor->init()) {
            Serial.printf("Failed to init sensor: %s\n", sensor->getName().c_str());
            allSuccess = false;
        }
    }
    return allSuccess;
}

void SensorManager::readAll() {
    for (auto sensor : sensors) {
        if (sensor->isEnabled()) {
            SensorReading reading = sensor->read();
            if (reading.valid) {
                addToHistory(reading);
            }
        }
    }
}

SensorReading SensorManager::readSensor(const String& name) {
    Sensor* sensor = getSensor(name);
    if (sensor && sensor->isEnabled()) {
        return sensor->read();
    }
    
    SensorReading invalid;
    invalid.valid = false;
    invalid.error = "Sensor not found or disabled";
    return invalid;
}

void SensorManager::addToHistory(const SensorReading& reading) {
    if (history[reading.type].size() >= historySize) {
        history[reading.type].erase(history[reading.type].begin());
    }
    history[reading.type].push_back(reading);
}

// Global sensor manager instance
SensorManager* sensorManager = nullptr;

// Global helper functions
void initSensors() {
    sensorManager = SensorManager::getInstance();
    
    // Create and add sensors based on configuration
    DHTSensor* dht = new DHTSensor("DHT22", DHT_PIN, DHT_TYPE);
    sensorManager->addSensor(dht);
    
    DS18B20Sensor* ds18b20 = new DS18B20Sensor("DS18B20", DS18B20_PIN);
    sensorManager->addSensor(ds18b20);
    
    AnalogSensor* soilMoisture = new AnalogSensor("SoilMoisture", SENSOR_SOIL_MOISTURE, SOIL_MOISTURE_PIN);
    soilMoisture->setRange(0, 4095, 0, 100);
    sensorManager->addSensor(soilMoisture);
    
    AnalogSensor* lightSensor = new AnalogSensor("Light", SENSOR_LIGHT, LIGHT_SENSOR_PIN);
    lightSensor->setRange(0, 4095, 0, 10000);
    sensorManager->addSensor(lightSensor);
    
    AnalogSensor* waterLevel = new AnalogSensor("WaterLevel", SENSOR_WATER_LEVEL, WATER_LEVEL_PIN);
    waterLevel->setRange(0, 4095, 0, 100);
    sensorManager->addSensor(waterLevel);
    
    // Initialize all sensors
    sensorManager->initAll();
    
    // Enable auto-read
    sensorManager->enableAutoRead(true, SENSOR_READ_INTERVAL);
}

void readSensors() {
    if (sensorManager) {
        sensorManager->readAll();
    }
}

float getCurrentTemperature() {
    if (!sensorManager) return 0;
    
    auto temps = sensorManager->getSensorsByType(SENSOR_TEMPERATURE);
    if (temps.empty()) return 0;
    
    SensorReading reading = temps[0]->getLastReading();
    return reading.valid ? reading.value : 0;
}

float getCurrentHumidity() {
    if (!sensorManager) return 0;
    
    Sensor* dht = sensorManager->getSensor("DHT22");
    if (!dht) return 0;
    
    DHTSensor* dhtSensor = static_cast<DHTSensor*>(dht);
    SensorReading reading = dhtSensor->readHumidity();
    return reading.valid ? reading.value : 0;
}