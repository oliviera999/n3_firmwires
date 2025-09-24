#include "automatism.h"
#include "rule_cache.h"
#include "sensors.h"
#include "actuators.h"
#include <algorithm>

// Global cache instance
RuleEvaluationCache* ruleCache = nullptr;

// Optimized AutomatismManager implementation
class OptimizedAutomatismManager : public AutomatismManager {
private:
    // Index rules by trigger type for faster evaluation
    std::map<SensorType, std::vector<Rule*>> sensorRules;
    std::map<String, std::vector<Rule*>> stateRules;
    std::vector<Rule*> timeRules;
    std::vector<Rule*> combinedRules;
    
    // Evaluation scheduling
    struct RuleSchedule {
        Rule* rule;
        unsigned long lastEval;
        unsigned long evalInterval;
        int priority;
    };
    std::vector<RuleSchedule> scheduledRules;
    
    // Performance metrics
    unsigned long totalEvaluations;
    unsigned long cacheHits;
    unsigned long totalExecutions;
    unsigned long avgEvalTime;
    
    static const char* TAG;
    
public:
    OptimizedAutomatismManager() : totalEvaluations(0), cacheHits(0), 
                                   totalExecutions(0), avgEvalTime(0) {
        if (!ruleCache) {
            ruleCache = new RuleEvaluationCache(100);
        }
        TAG = "AutoOpt";
    }
    
    ~OptimizedAutomatismManager() {
        if (ruleCache) {
            delete ruleCache;
            ruleCache = nullptr;
        }
    }
    
    // Override addRule to index by trigger type
    void addRule(Rule* rule) override {
        if (!rule) return;
        
        AutomatismManager::addRule(rule);
        
        // Analyze rule conditions and index appropriately
        indexRule(rule);
        
        // Schedule rule evaluation
        scheduleRule(rule);
        
        ESP_LOGI(TAG, "Rule '%s' added and indexed", rule->getName().c_str());
    }
    
    // Optimized process function
    void process() override {
        if (!enabled) return;
        
        unsigned long startTime = micros();
        unsigned long now = millis();
        
        // Process only relevant rules based on recent changes
        processScheduledRules(now);
        
        // Update performance metrics
        unsigned long evalTime = micros() - startTime;
        avgEvalTime = (avgEvalTime * 9 + evalTime) / 10; // Moving average
        
        // Log performance stats periodically
        static unsigned long lastStatsLog = 0;
        if (now - lastStatsLog > 30000) { // Every 30 seconds
            lastStatsLog = now;
            logPerformanceStats();
        }
    }
    
private:
    void indexRule(Rule* rule) {
        // Parse rule conditions to determine trigger types
        // This is a simplified version - real implementation would parse actual conditions
        
        // For demonstration, categorize based on rule name patterns
        String name = rule->getName();
        name.toLowerCase();
        
        if (name.indexOf("temp") >= 0 || name.indexOf("humidity") >= 0) {
            sensorRules[SENSOR_TEMPERATURE].push_back(rule);
        } else if (name.indexOf("light") >= 0) {
            sensorRules[SENSOR_LIGHT].push_back(rule);
        } else if (name.indexOf("moisture") >= 0) {
            sensorRules[SENSOR_SOIL_MOISTURE].push_back(rule);
        } else if (name.indexOf("time") >= 0 || name.indexOf("schedule") >= 0) {
            timeRules.push_back(rule);
        } else if (name.indexOf("state") >= 0 || name.indexOf("pump") >= 0 || name.indexOf("fan") >= 0) {
            stateRules["actuator"].push_back(rule);
        } else {
            combinedRules.push_back(rule);
        }
    }
    
    void scheduleRule(Rule* rule) {
        // Determine evaluation frequency based on rule type
        unsigned long evalInterval = 1000; // Default 1 second
        int priority = 1; // Default priority
        
        String name = rule->getName();
        name.toLowerCase();
        
        // Critical rules - evaluate more frequently
        if (name.indexOf("safety") >= 0 || name.indexOf("emergency") >= 0) {
            evalInterval = 100;  // 100ms for safety rules
            priority = 10;
        }
        // Sensor rules - moderate frequency
        else if (name.indexOf("sensor") >= 0 || name.indexOf("temp") >= 0) {
            evalInterval = 500;  // 500ms for sensor rules
            priority = 5;
        }
        // Time-based rules - less frequent
        else if (name.indexOf("time") >= 0 || name.indexOf("schedule") >= 0) {
            evalInterval = 5000; // 5 seconds for time rules
            priority = 1;
        }
        
        RuleSchedule schedule = {
            .rule = rule,
            .lastEval = 0,
            .evalInterval = evalInterval,
            .priority = priority
        };
        
        scheduledRules.push_back(schedule);
        
        // Sort by priority
        std::sort(scheduledRules.begin(), scheduledRules.end(),
                 [](const RuleSchedule& a, const RuleSchedule& b) {
                     return a.priority > b.priority;
                 });
    }
    
    void processScheduledRules(unsigned long now) {
        for (auto& schedule : scheduledRules) {
            // Skip if evaluated recently
            if (now - schedule.lastEval < schedule.evalInterval) {
                continue;
            }
            
            // Try to get cached result first
            bool cachedResult;
            String ruleId = schedule.rule->getName();
            
            totalEvaluations++;
            
            if (ruleCache->getCachedResult(ruleId, cachedResult)) {
                cacheHits++;
                
                // Use cached result
                if (cachedResult && schedule.rule->canTrigger()) {
                    executeRule(schedule.rule);
                }
            } else {
                // Evaluate rule
                bool result = evaluateRule(schedule.rule);
                
                // Cache the result
                std::vector<float> sensorValues; // Would collect actual sensor values used
                ruleCache->cacheResult(ruleId, result, sensorValues);
                
                // Execute if needed
                if (result && schedule.rule->canTrigger()) {
                    executeRule(schedule.rule);
                }
            }
            
            schedule.lastEval = now;
        }
    }
    
    bool evaluateRule(Rule* rule) {
        // Measure evaluation time
        unsigned long startTime = micros();
        
        bool result = rule->evaluate();
        
        unsigned long evalTime = micros() - startTime;
        
        // Log slow evaluations
        if (evalTime > 1000) { // More than 1ms
            ESP_LOGW(TAG, "Slow rule evaluation: '%s' took %lu us", 
                    rule->getName().c_str(), evalTime);
        }
        
        return result;
    }
    
    void executeRule(Rule* rule) {
        totalExecutions++;
        
        ESP_LOGI(TAG, "Executing rule: %s", rule->getName().c_str());
        
        // Execute with monitoring
        unsigned long startTime = micros();
        bool success = rule->execute();
        unsigned long execTime = micros() - startTime;
        
        if (!success) {
            ESP_LOGE(TAG, "Rule execution failed: %s", rule->getName().c_str());
        }
        
        // Log slow executions
        if (execTime > 10000) { // More than 10ms
            ESP_LOGW(TAG, "Slow rule execution: '%s' took %lu us", 
                    rule->getName().c_str(), execTime);
        }
    }
    
    void logPerformanceStats() {
        float hitRate = (totalEvaluations > 0) ? 
                       (float)cacheHits / totalEvaluations * 100 : 0;
        
        ESP_LOGI(TAG, "=== Automation Performance Stats ===");
        ESP_LOGI(TAG, "Total evaluations: %lu", totalEvaluations);
        ESP_LOGI(TAG, "Cache hits: %lu (%.1f%%)", cacheHits, hitRate);
        ESP_LOGI(TAG, "Total executions: %lu", totalExecutions);
        ESP_LOGI(TAG, "Avg eval time: %lu us", avgEvalTime);
        ESP_LOGI(TAG, "Active rules: %d", scheduledRules.size());
        
        size_t cacheSize;
        float cacheHitRate;
        ruleCache->getStats(cacheSize, cacheHitRate);
        ESP_LOGI(TAG, "Cache size: %d, Hit rate: %.1f%%", cacheSize, cacheHitRate * 100);
        ESP_LOGI(TAG, "=====================================");
    }
    
public:
    // Notification when sensor changes to invalidate cache
    void onSensorChange(SensorType type) {
        // Invalidate cache for rules using this sensor
        ruleCache->invalidateSensorRules(type);
        
        // Trigger immediate evaluation of relevant rules
        unsigned long now = millis();
        
        auto it = sensorRules.find(type);
        if (it != sensorRules.end()) {
            for (Rule* rule : it->second) {
                // Find in scheduled rules and reset last eval time
                for (auto& schedule : scheduledRules) {
                    if (schedule.rule == rule) {
                        schedule.lastEval = 0; // Force immediate evaluation
                        break;
                    }
                }
            }
        }
    }
    
    // Notification when actuator state changes
    void onActuatorChange(const String& actuatorName) {
        // Similar to sensor change, invalidate relevant caches
        auto it = stateRules.find("actuator");
        if (it != stateRules.end()) {
            for (Rule* rule : it->second) {
                String ruleId = rule->getName();
                ruleCache->invalidate(ruleId);
            }
        }
    }
};

const char* OptimizedAutomatismManager::TAG = "AutoOpt";

// Global optimized instance
OptimizedAutomatismManager* optimizedAutomatism = nullptr;

// Initialize optimized automation
void initOptimizedAutomatism() {
    if (!optimizedAutomatism) {
        optimizedAutomatism = new OptimizedAutomatismManager();
        ESP_LOGI("AutoOpt", "Optimized automation system initialized");
    }
}

// Process with optimization
void processOptimizedAutomatism() {
    if (optimizedAutomatism) {
        optimizedAutomatism->process();
    }
}