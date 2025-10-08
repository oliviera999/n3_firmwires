#pragma once
#include <Arduino.h>

/**
 * Logger optimisé avec niveaux configurables pour réduire la verbosité
 * et améliorer les performances
 */
class OptimizedLogger {
public:
    enum LogLevel {
        LOG_ERROR = 0,      // Erreurs critiques seulement
        LOG_WARNING = 1,    // Warnings + erreurs
        LOG_INFO = 2,       // Infos importantes + warnings + erreurs
        LOG_DEBUG = 3,      // Debug complet (mode développement)
        LOG_VERBOSE = 4     // Très verbeux (mode debug avancé)
    };

private:
    static LogLevel currentLevel;
    static bool initialized;
    static uint32_t logCount;
    static uint32_t suppressedCount;
    
    // Cache pour éviter les allocations répétées
    static char logBuffer[256];
    
public:
    /**
     * Initialise le logger avec le niveau configuré
     */
    static void init(LogLevel level = LOG_INFO);
    
    /**
     * Log une erreur (toujours affiché)
     */
    template<typename... Args>
    static void error(const char* format, Args... args) {
        if (currentLevel >= LOG_ERROR) {
            log("ERROR", format, args...);
        }
    }
    
    /**
     * Log un warning
     */
    template<typename... Args>
    static void warning(const char* format, Args... args) {
        if (currentLevel >= LOG_WARNING) {
            log("WARN", format, args...);
        } else {
            suppressedCount++;
        }
    }
    
    /**
     * Log une information
     */
    template<typename... Args>
    static void info(const char* format, Args... args) {
        if (currentLevel >= LOG_INFO) {
            log("INFO", format, args...);
        } else {
            suppressedCount++;
        }
    }
    
    /**
     * Log debug (seulement si niveau DEBUG ou plus)
     */
    template<typename... Args>
    static void debug(const char* format, Args... args) {
        if (currentLevel >= LOG_DEBUG) {
            log("DEBUG", format, args...);
        } else {
            suppressedCount++;
        }
    }
    
    /**
     * Log verbeux (seulement si niveau VERBOSE)
     */
    template<typename... Args>
    static void verbose(const char* format, Args... args) {
        if (currentLevel >= LOG_VERBOSE) {
            log("VERBOSE", format, args...);
        } else {
            suppressedCount++;
        }
    }
    
    /**
     * Change le niveau de log dynamiquement
     */
    static void setLevel(LogLevel level);
    
    /**
     * Obtient le niveau actuel
     */
    static LogLevel getLevel();
    
    /**
     * Obtient les statistiques du logger
     */
    static void logStats();
    
    /**
     * Réinitialise les compteurs
     */
    static void resetStats();
    
    /**
     * Active/désactive les logs (mode silence total)
     */
    static void setEnabled(bool enabled);
    
private:
    template<typename... Args>
    static void log(const char* level, const char* format, Args... args) {
        if (!initialized) return;
        
        logCount++;
        int len = snprintf(logBuffer, sizeof(logBuffer), "[%s] ", level);
        len += snprintf(logBuffer + len, sizeof(logBuffer) - len, format, args...);
        
        // Tronquer si nécessaire
        if (len >= (int)sizeof(logBuffer) - 1) {
            logBuffer[sizeof(logBuffer) - 2] = '\n';
            logBuffer[sizeof(logBuffer) - 1] = '\0';
        } else {
            logBuffer[len] = '\n';
            logBuffer[len + 1] = '\0';
        }
        
        Serial.print(logBuffer);
    }
};

// Macros de compatibilité pour faciliter la migration (sans conflit)
#define OPT_LOG_ERROR(fmt, ...) OptimizedLogger::error(fmt, ##__VA_ARGS__)
#define OPT_LOG_WARNING(fmt, ...) OptimizedLogger::warning(fmt, ##__VA_ARGS__)
#define OPT_LOG_INFO(fmt, ...) OptimizedLogger::info(fmt, ##__VA_ARGS__)
#define OPT_LOG_DEBUG(fmt, ...) OptimizedLogger::debug(fmt, ##__VA_ARGS__)
#define OPT_LOG_VERBOSE(fmt, ...) OptimizedLogger::verbose(fmt, ##__VA_ARGS__)
