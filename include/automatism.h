#ifndef AUTOMATISM_H
#define AUTOMATISM_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include <vector>
#include <functional>

enum ConditionType {
    CONDITION_TIME,
    CONDITION_SENSOR,
    CONDITION_STATE,
    CONDITION_COMBINED
};

enum ConditionOperator {
    OP_EQUAL,
    OP_NOT_EQUAL,
    OP_GREATER,
    OP_LESS,
    OP_GREATER_EQUAL,
    OP_LESS_EQUAL,
    OP_BETWEEN,
    OP_AND,
    OP_OR
};

enum ActionType {
    ACTION_ACTUATOR,
    ACTION_NOTIFICATION,
    ACTION_LOG,
    ACTION_DELAY,
    ACTION_SEQUENCE
};

struct Condition {
    ConditionType type;
    String source;  // sensor name, actuator name, or time
    ConditionOperator op;
    float value1;
    float value2;  // for BETWEEN operator
    bool inverted;
    
    bool evaluate();
};

struct Action {
    ActionType type;
    String target;  // actuator name or notification type
    String command;  // on, off, pwm, etc.
    int value;
    unsigned long delay;
    
    bool execute();
};

class Rule {
private:
    String name;
    bool enabled;
    std::vector<Condition> conditions;
    std::vector<Action> actions;
    ConditionOperator combineOp;  // AND or OR for multiple conditions
    unsigned long lastTrigger;
    unsigned long cooldown;
    int triggerCount;
    
public:
    Rule(const String& name);
    
    void addCondition(const Condition& condition);
    void addAction(const Action& action);
    void setCombineOperator(ConditionOperator op) { combineOp = op; }
    void setCooldown(unsigned long ms) { cooldown = ms; }
    void enable(bool state) { enabled = state; }
    
    bool evaluate();
    bool execute();
    bool canTrigger();
    
    String getName() const { return name; }
    bool isEnabled() const { return enabled; }
    int getTriggerCount() const { return triggerCount; }
    unsigned long getLastTrigger() const { return lastTrigger; }
    
    JsonDocument toJson();
    bool fromJson(JsonDocument& doc);
};

class Schedule {
private:
    String name;
    bool enabled;
    uint8_t hour;
    uint8_t minute;
    uint8_t dayMask;  // bit mask for days of week
    std::vector<Action> actions;
    unsigned long lastRun;
    bool recurring;
    
public:
    Schedule(const String& name);
    
    void setTime(uint8_t h, uint8_t m);
    void setDays(uint8_t mask);
    void addAction(const Action& action);
    void enable(bool state) { enabled = state; }
    void setRecurring(bool recur) { recurring = recur; }
    
    bool shouldRun();
    bool execute();
    
    String getName() const { return name; }
    bool isEnabled() const { return enabled; }
    
    JsonDocument toJson();
    bool fromJson(JsonDocument& doc);
};

class AutomatismManager {
private:
    std::vector<Rule*> rules;
    std::vector<Schedule*> schedules;
    bool enabled;
    unsigned long lastProcess;
    unsigned long processInterval;
    
    static AutomatismManager* instance;
    AutomatismManager();
    
public:
    static AutomatismManager* getInstance();
    ~AutomatismManager();
    
    void addRule(Rule* rule);
    void removeRule(const String& name);
    Rule* getRule(const String& name);
    std::vector<Rule*> getAllRules();
    
    void addSchedule(Schedule* schedule);
    void removeSchedule(const String& name);
    Schedule* getSchedule(const String& name);
    std::vector<Schedule*> getAllSchedules();
    
    void enable(bool state) { enabled = state; }
    bool isEnabled() const { return enabled; }
    
    void process();
    void processRules();
    void processSchedules();
    
    bool saveToFile(const String& filename);
    bool loadFromFile(const String& filename);
    
    JsonDocument getStatus();
    JsonDocument getRulesJson();
    JsonDocument getSchedulesJson();
};

// Global functions
void initAutomatism();
void processAutomatismRules();
void addAutomatismRule(const String& json);
void removeAutomatismRule(const String& name);
void enableAutomatism(bool enable);

#endif // AUTOMATISM_H