#include "timer_manager.h"

// Définition des variables statiques
std::vector<TimerManager::Timer> TimerManager::timers;
uint32_t TimerManager::lastCheck = 0;
bool TimerManager::initialized = false;

void TimerManager::init() {
    if (initialized) return;
    
    timers.clear();
    timers.reserve(10); // Réserver de l'espace pour 10 timers
    lastCheck = millis();
    initialized = true;
    
    Serial.println("[TimerManager] ✅ Initialisé");
}

int TimerManager::addTimer(const char* name, uint32_t interval, std::function<void()> callback) {
    if (!initialized) {
        Serial.println("[TimerManager] ❌ Non initialisé - utilisez init() d'abord");
        return -1;
    }
    
    // Vérifier si le nom existe déjà
    for (size_t i = 0; i < timers.size(); i++) {
        if (strcmp(timers[i].name, name) == 0) {
            Serial.printf("[TimerManager] ⚠️ Timer '%s' existe déjà\n", name);
            return i;
        }
    }
    
    // Ajouter le nouveau timer
    timers.emplace_back(name, interval, callback);
    int id = timers.size() - 1;
    
    Serial.printf("[TimerManager] ✅ Timer '%s' ajouté (ID: %d, interval: %lu ms)\n", name, id, interval);
    return id;
}

void TimerManager::process() {
    if (!initialized || timers.empty()) return;
    
    uint32_t now = millis();
    
    // Traiter tous les timers
    for (size_t i = 0; i < timers.size(); i++) {
        Timer& timer = timers[i];
        
        if (!timer.enabled) continue;
        
        // Vérifier si le timer doit s'exécuter
        if (now - timer.lastRun >= timer.interval) {
            uint32_t startTime = micros();
            
            // Exécution du callback (sans try/catch car exceptions désactivées)
            timer.callback();
            timer.callCount++;
            timer.lastRun = now;
            
            uint32_t executionTime = micros() - startTime;
            timer.totalTime += executionTime;
            
            // Log si l'exécution est lente (> 10ms)
            if (executionTime > 10000) {
                Serial.printf("[TimerManager] ⚠️ Timer '%s' lent: %lu μs\n", timer.name, executionTime);
            }
        }
    }
    
    lastCheck = now;
}

void TimerManager::enableTimer(int timerId, bool enable) {
    if (timerId < 0 || timerId >= (int)timers.size()) {
        Serial.printf("[TimerManager] ❌ ID timer invalide: %d\n", timerId);
        return;
    }
    
    timers[timerId].enabled = enable;
    Serial.printf("[TimerManager] Timer ID %d %s\n", timerId, enable ? "activé" : "désactivé");
}

void TimerManager::enableTimer(const char* name, bool enable) {
    for (size_t i = 0; i < timers.size(); i++) {
        if (strcmp(timers[i].name, name) == 0) {
            timers[i].enabled = enable;
            Serial.printf("[TimerManager] Timer '%s' %s\n", name, enable ? "activé" : "désactivé");
            return;
        }
    }
    
    Serial.printf("[TimerManager] ⚠️ Timer '%s' non trouvé\n", name);
}

void TimerManager::updateInterval(int timerId, uint32_t newInterval) {
    if (timerId < 0 || timerId >= (int)timers.size()) {
        Serial.printf("[TimerManager] ❌ ID timer invalide: %d\n", timerId);
        return;
    }
    
    uint32_t oldInterval = timers[timerId].interval;
    timers[timerId].interval = newInterval;
    Serial.printf("[TimerManager] Timer ID %d: intervalle %lu → %lu ms\n", timerId, oldInterval, newInterval);
}

TimerManager::Timer* TimerManager::getTimerStats(int timerId) {
    if (timerId < 0 || timerId >= (int)timers.size()) {
        return nullptr;
    }
    
    return &timers[timerId];
}

void TimerManager::logStats() {
    if (!initialized) {
        Serial.println("[TimerManager] Non initialisé");
        return;
    }
    
    Serial.println("\n=== STATISTIQUES TIMER MANAGER ===");
    Serial.printf("Timers actifs: %zu/%zu\n", getActiveTimerCount(), timers.size());
    Serial.printf("Temps total d'exécution: %lu μs\n", getTotalExecutionTime());
    
    for (size_t i = 0; i < timers.size(); i++) {
        const Timer& timer = timers[i];
        uint32_t avgTime = timer.callCount > 0 ? timer.totalTime / timer.callCount : 0;
        
        Serial.printf("[%zu] %s: %s, %lu ms, %lu appels, %lu μs moy\n",
                     i, timer.name, timer.enabled ? "ACTIF" : "INACTIF",
                     timer.interval, timer.callCount, avgTime);
    }
    Serial.println("=====================================\n");
}

void TimerManager::suspendAll() {
    int count = 0;
    for (auto& timer : timers) {
        if (timer.enabled) {
            timer.enabled = false;
            count++;
        }
    }
    Serial.printf("[TimerManager] %d timers suspendus (mode économie)\n", count);
}

void TimerManager::resumeAll() {
    int count = 0;
    for (auto& timer : timers) {
        if (!timer.enabled) {
            timer.enabled = true;
            count++;
        }
    }
    Serial.printf("[TimerManager] %d timers réactivés\n", count);
}

size_t TimerManager::getActiveTimerCount() {
    size_t count = 0;
    for (const auto& timer : timers) {
        if (timer.enabled) count++;
    }
    return count;
}

uint32_t TimerManager::getTotalExecutionTime() {
    uint32_t total = 0;
    for (const auto& timer : timers) {
        total += timer.totalTime;
    }
    return total;
}
