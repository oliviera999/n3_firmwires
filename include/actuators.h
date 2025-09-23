#ifndef ACTUATORS_H
#define ACTUATORS_H

#include <Arduino.h>
#include <map>
#include <functional>

enum ActuatorType {
    ACTUATOR_RELAY,
    ACTUATOR_PWM,
    ACTUATOR_SERVO,
    ACTUATOR_STEPPER
};

enum ActuatorState {
    STATE_OFF,
    STATE_ON,
    STATE_PWM,
    STATE_POSITION
};

struct ActuatorStatus {
    ActuatorType type;
    ActuatorState state;
    int value;  // PWM value or position
    unsigned long lastChange;
    unsigned long runTime;
    bool manual;
    String error;
};

class Actuator {
protected:
    String name;
    ActuatorType type;
    int pin;
    int channel;  // PWM channel
    ActuatorStatus status;
    bool inverted;
    int minValue;
    int maxValue;
    unsigned long safetyTimeout;
    unsigned long lastActivation;
    
public:
    Actuator(const String& name, ActuatorType type, int pin);
    virtual ~Actuator() {}
    
    virtual bool init() = 0;
    virtual bool setState(ActuatorState state, int value = 0) = 0;
    virtual ActuatorStatus getStatus() = 0;
    
    String getName() const { return name; }
    ActuatorType getType() const { return type; }
    void setInverted(bool inv) { inverted = inv; }
    void setSafetyTimeout(unsigned long timeout) { safetyTimeout = timeout; }
    void setLimits(int min, int max) { minValue = min; maxValue = max; }
};

class RelayActuator : public Actuator {
public:
    RelayActuator(const String& name, int pin);
    bool init() override;
    bool setState(ActuatorState state, int value = 0) override;
    ActuatorStatus getStatus() override;
};

class PWMActuator : public Actuator {
private:
    int frequency;
    int resolution;
    
public:
    PWMActuator(const String& name, int pin, int channel);
    bool init() override;
    bool setState(ActuatorState state, int value = 0) override;
    ActuatorStatus getStatus() override;
    void configure(int freq, int res);
};

class ActuatorManager {
private:
    std::map<String, Actuator*> actuators;
    std::map<String, std::function<void(ActuatorStatus)>> callbacks;
    bool safetyEnabled;
    unsigned long lastSafetyCheck;
    
    static ActuatorManager* instance;
    ActuatorManager();
    
public:
    static ActuatorManager* getInstance();
    ~ActuatorManager();
    
    void addActuator(Actuator* actuator);
    void removeActuator(const String& name);
    Actuator* getActuator(const String& name);
    
    bool initAll();
    bool setActuatorState(const String& name, ActuatorState state, int value = 0);
    ActuatorStatus getActuatorStatus(const String& name);
    std::map<String, ActuatorStatus> getAllStatus();
    
    void enableSafety(bool enable) { safetyEnabled = enable; }
    void checkSafety();
    
    void registerCallback(const String& name, std::function<void(ActuatorStatus)> callback);
    void triggerCallbacks(const String& name);
    
    // Convenience methods
    bool turnOn(const String& name);
    bool turnOff(const String& name);
    bool setPWM(const String& name, int value);
    bool toggle(const String& name);
};

// Global functions
void initActuators();
void updateActuatorStates();
bool getPumpState();
bool getFanState();
bool getLightState();
void checkSafetyConditions();

#endif // ACTUATORS_H