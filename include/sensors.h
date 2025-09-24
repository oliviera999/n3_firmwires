#ifndef SENSORS_H
#define SENSORS_H

#include <Arduino.h>
#include <DHT.h>
#include <OneWire.h>
#include <DallasTemperature.h>
#include <Wire.h>
#include <vector>
#include <map>

// Sensor types
enum SensorType {
    SENSOR_TEMPERATURE,
    SENSOR_HUMIDITY,
    SENSOR_PRESSURE,
    SENSOR_LIGHT,
    SENSOR_SOIL_MOISTURE,
    SENSOR_WATER_LEVEL,
    SENSOR_PH,
    SENSOR_EC,
    SENSOR_CO2,
    SENSOR_MOTION
};

// Sensor reading structure
struct SensorReading {
    SensorType type;
    float value;
    float rawValue;
    unsigned long timestamp;
    bool valid;
    String unit;
    String error;
};

// Base sensor class
class Sensor {
protected:
    String name;
    SensorType type;
    int pin;
    bool enabled;
    float offset;
    float scale;
    unsigned long lastReadTime;
    unsigned long readInterval;
    SensorReading lastReading;
    
public:
    Sensor(const String& name, SensorType type, int pin);
    virtual ~Sensor() {}
    
    virtual bool init() = 0;
    virtual SensorReading read() = 0;
    virtual void calibrate(float offset, float scale);
    
    String getName() const { return name; }
    SensorType getType() const { return type; }
    bool isEnabled() const { return enabled; }
    void enable(bool state) { enabled = state; }
    SensorReading getLastReading() const { return lastReading; }
};

// DHT sensor implementation - Non-blocking
class DHTSensor : public Sensor {
private:
    DHT* dht;
    uint8_t dhtType;
    
    // Non-blocking initialization
    enum InitState {
        INIT_NOT_STARTED,
        INIT_WAITING,
        INIT_READY,
        INIT_FAILED
    };
    InitState initState;
    unsigned long initStartTime;
    
public:
    DHTSensor(const String& name, int pin, uint8_t type);
    ~DHTSensor();
    bool init() override;
    bool isReady();
    SensorReading read() override;
    SensorReading readTemperature();
    SensorReading readHumidity();
};

// DS18B20 sensor implementation
class DS18B20Sensor : public Sensor {
private:
    OneWire* oneWire;
    DallasTemperature* sensors;
    DeviceAddress deviceAddress;
    
public:
    DS18B20Sensor(const String& name, int pin);
    ~DS18B20Sensor();
    bool init() override;
    SensorReading read() override;
};

// Analog sensor implementation
class AnalogSensor : public Sensor {
private:
    int minValue;
    int maxValue;
    float minOutput;
    float maxOutput;
    int numSamples;
    
public:
    AnalogSensor(const String& name, SensorType type, int pin);
    bool init() override;
    SensorReading read() override;
    void setRange(int min, int max, float minOut, float maxOut);
    void setSampling(int samples);
};

// Sensor manager
class SensorManager {
private:
    std::vector<Sensor*> sensors;
    std::map<SensorType, std::vector<SensorReading>> history;
    unsigned long historySize;
    bool autoRead;
    unsigned long autoReadInterval;
    unsigned long lastAutoRead;
    
    static SensorManager* instance;
    SensorManager();
    
public:
    static SensorManager* getInstance();
    ~SensorManager();
    
    void addSensor(Sensor* sensor);
    void removeSensor(const String& name);
    Sensor* getSensor(const String& name);
    std::vector<Sensor*> getSensorsByType(SensorType type);
    
    bool initAll();
    void readAll();
    SensorReading readSensor(const String& name);
    std::vector<SensorReading> getAllReadings();
    
    void enableAutoRead(bool enable, unsigned long interval = 5000);
    void processAutoRead();
    
    void addToHistory(const SensorReading& reading);
    std::vector<SensorReading> getHistory(SensorType type, int count = 10);
    void clearHistory();
    
    float getAverage(SensorType type, int samples = 10);
    float getMin(SensorType type, int samples = 10);
    float getMax(SensorType type, int samples = 10);
    
    // Threshold management
    void setThreshold(SensorType type, float min, float max);
    bool checkThresholds();
    std::vector<String> getThresholdViolations();
};

// Global functions
void initSensors();
void readSensors();
float getCurrentTemperature();
float getCurrentHumidity();
float getCurrentPressure();
float getCurrentLight();
float getSoilMoisture();
float getWaterLevel();
void checkSensorThresholds();

#endif // SENSORS_H