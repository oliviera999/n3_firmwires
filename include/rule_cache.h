#ifndef RULE_CACHE_H
#define RULE_CACHE_H

#include <Arduino.h>
#include <map>
#include <vector>
#include <functional>
#include "sensors.h"

// Cache configuration
#define CACHE_DEFAULT_TTL 1000      // Default cache TTL in ms
#define CACHE_MAX_ENTRIES 100        // Maximum cache entries
#define CACHE_CLEANUP_INTERVAL 60000 // Cleanup interval in ms

template<typename K, typename V>
class LRUCache {
private:
    struct CacheEntry {
        V value;
        unsigned long timestamp;
        unsigned long accessCount;
        unsigned long ttl;
    };
    
    std::map<K, CacheEntry> cache;
    size_t maxSize;
    unsigned long defaultTTL;
    unsigned long lastCleanup;
    SemaphoreHandle_t mutex;
    
    void evictOldest() {
        if (cache.empty()) return;
        
        // Find least recently used entry
        auto oldest = cache.begin();
        unsigned long oldestTime = ULONG_MAX;
        
        for (auto it = cache.begin(); it != cache.end(); ++it) {
            if (it->second.timestamp < oldestTime) {
                oldestTime = it->second.timestamp;
                oldest = it;
            }
        }
        
        cache.erase(oldest);
    }
    
    void cleanup() {
        unsigned long now = millis();
        
        // Remove expired entries
        for (auto it = cache.begin(); it != cache.end(); ) {
            if (now - it->second.timestamp > it->second.ttl) {
                it = cache.erase(it);
            } else {
                ++it;
            }
        }
        
        lastCleanup = now;
    }
    
public:
    LRUCache(size_t maxSize = CACHE_MAX_ENTRIES, unsigned long ttl = CACHE_DEFAULT_TTL) 
        : maxSize(maxSize), defaultTTL(ttl), lastCleanup(0) {
        mutex = xSemaphoreCreateMutex();
    }
    
    ~LRUCache() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    bool put(const K& key, const V& value, unsigned long ttl = 0) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Use default TTL if not specified
        if (ttl == 0) ttl = defaultTTL;
        
        // Check if we need to evict
        if (cache.size() >= maxSize && cache.find(key) == cache.end()) {
            evictOldest();
        }
        
        // Add or update entry
        CacheEntry entry = {
            .value = value,
            .timestamp = millis(),
            .accessCount = 0,
            .ttl = ttl
        };
        
        cache[key] = entry;
        
        xSemaphoreGive(mutex);
        return true;
    }
    
    bool get(const K& key, V& value) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Periodic cleanup
        if (millis() - lastCleanup > CACHE_CLEANUP_INTERVAL) {
            cleanup();
        }
        
        auto it = cache.find(key);
        if (it == cache.end()) {
            xSemaphoreGive(mutex);
            return false; // Cache miss
        }
        
        // Check if expired
        if (millis() - it->second.timestamp > it->second.ttl) {
            cache.erase(it);
            xSemaphoreGive(mutex);
            return false; // Expired
        }
        
        // Update access info
        it->second.accessCount++;
        it->second.timestamp = millis(); // Update timestamp for LRU
        
        value = it->second.value;
        
        xSemaphoreGive(mutex);
        return true; // Cache hit
    }
    
    void invalidate(const K& key) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        cache.erase(key);
        xSemaphoreGive(mutex);
    }
    
    void invalidateAll() {
        xSemaphoreTake(mutex, portMAX_DELAY);
        cache.clear();
        xSemaphoreGive(mutex);
    }
    
    size_t size() {
        xSemaphoreTake(mutex, portMAX_DELAY);
        size_t s = cache.size();
        xSemaphoreGive(mutex);
        return s;
    }
    
    float getHitRate() {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        unsigned long totalAccess = 0;
        for (const auto& [key, entry] : cache) {
            totalAccess += entry.accessCount;
        }
        
        xSemaphoreGive(mutex);
        
        // Simple approximation
        return cache.empty() ? 0.0f : (float)totalAccess / (totalAccess + cache.size());
    }
};

// Specialized cache for rule evaluations
class RuleEvaluationCache {
private:
    struct EvalResult {
        bool result;
        unsigned long timestamp;
        std::vector<float> sensorValues; // Store sensor values used
    };
    
    LRUCache<String, EvalResult> cache;
    std::map<SensorType, unsigned long> sensorUpdateTimes;
    SemaphoreHandle_t mutex;
    
public:
    RuleEvaluationCache(size_t maxSize = 50) : cache(maxSize, 3000) { // 3000ms TTL for rules (optimisé pour SENSOR_READ_INTERVAL_MS = 4000ms)
        mutex = xSemaphoreCreateMutex();
    }
    
    ~RuleEvaluationCache() {
        if (mutex) {
            vSemaphoreDelete(mutex);
        }
    }
    
    // Cache rule evaluation result
    void cacheResult(const String& ruleId, bool result, const std::vector<float>& sensorValues) {
        EvalResult evalResult = {
            .result = result,
            .timestamp = millis(),
            .sensorValues = sensorValues
        };
        
        cache.put(ruleId, evalResult);
    }
    
    // Get cached result if valid
    bool getCachedResult(const String& ruleId, bool& result) {
        EvalResult evalResult;
        
        if (cache.get(ruleId, evalResult)) {
            result = evalResult.result;
            return true;
        }
        
        return false;
    }
    
    // Invalidate cache for rules using specific sensor
    void invalidateSensorRules(SensorType sensorType) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        // Mark sensor as updated
        sensorUpdateTimes[sensorType] = millis();
        
        // In real implementation, we'd track which rules use which sensors
        // For now, invalidate all on any sensor change
        cache.invalidateAll();
        
        xSemaphoreGive(mutex);
    }
    
    // Check if sensor has been updated since last evaluation
    bool isSensorUpdated(SensorType sensorType, unsigned long sinceTime) {
        xSemaphoreTake(mutex, portMAX_DELAY);
        
        auto it = sensorUpdateTimes.find(sensorType);
        bool updated = (it != sensorUpdateTimes.end() && it->second > sinceTime);
        
        xSemaphoreGive(mutex);
        return updated;
    }
    
    // Get cache statistics
    void getStats(size_t& size, float& hitRate) {
        size = cache.size();
        hitRate = cache.getHitRate();
    }
};

// Global cache instance
extern RuleEvaluationCache* ruleCache;

#endif // RULE_CACHE_H