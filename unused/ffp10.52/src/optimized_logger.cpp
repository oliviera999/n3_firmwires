#include "optimized_logger.h"

// Définition des variables statiques
OptimizedLogger::LogLevel OptimizedLogger::currentLevel = OptimizedLogger::LOG_INFO;
bool OptimizedLogger::initialized = false;
uint32_t OptimizedLogger::logCount = 0;
uint32_t OptimizedLogger::suppressedCount = 0;
char OptimizedLogger::logBuffer[256];

void OptimizedLogger::init(LogLevel level) {
    currentLevel = level;
    initialized = true;
    logCount = 0;
    suppressedCount = 0;
    
    // Log de démarrage avec le niveau configuré
    Serial.printf("[LOGGER] ✅ Initialisé - Niveau: %d\n", (int)level);
}

void OptimizedLogger::setLevel(LogLevel level) {
    LogLevel oldLevel = currentLevel;
    currentLevel = level;
    
    if (initialized) {
        Serial.printf("[LOGGER] Niveau changé: %d → %d\n", (int)oldLevel, (int)level);
    }
}

OptimizedLogger::LogLevel OptimizedLogger::getLevel() {
    return currentLevel;
}

void OptimizedLogger::logStats() {
    if (!initialized) {
        Serial.println("[LOGGER] Non initialisé");
        return;
    }
    
    Serial.println("\n=== STATISTIQUES LOGGER ===");
    Serial.printf("Niveau actuel: %d\n", (int)currentLevel);
    Serial.printf("Logs affichés: %lu\n", logCount);
    Serial.printf("Logs supprimés: %lu\n", suppressedCount);
    Serial.printf("Taux suppression: %.1f%%\n", 
                  logCount > 0 ? (100.0 * suppressedCount) / (logCount + suppressedCount) : 0.0);
    
    const char* levelNames[] = {"ERROR", "WARNING", "INFO", "DEBUG", "VERBOSE"};
    if (currentLevel < 5) {
        Serial.printf("Niveau: %s\n", levelNames[currentLevel]);
    }
    Serial.println("===========================\n");
}

void OptimizedLogger::resetStats() {
    logCount = 0;
    suppressedCount = 0;
    Serial.println("[LOGGER] Statistiques réinitialisées");
}

void OptimizedLogger::setEnabled(bool enabled) {
    if (enabled) {
        if (currentLevel == LOG_ERROR) {
            setLevel(LOG_INFO); // Réactiver les logs normaux
        }
    } else {
        setLevel(LOG_ERROR); // Seulement les erreurs
    }
}
