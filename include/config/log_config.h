#pragma once

// =============================================================================
// CONFIGURATION SYSTÈME DE LOGS
// =============================================================================
// Module: Configuration des logs, niveaux de debug et moniteur série
// Taille: ~122 lignes
// =============================================================================

#include <Arduino.h>

// =============================================================================
// CONFIGURATION SYSTÈME DE LOGS
// =============================================================================
namespace LogConfig {
    // Niveaux de log
    enum LogLevel {
        LOG_NONE = 0,
        LOG_ERROR = 1,
        LOG_WARN = 2,
        LOG_INFO = 3,
        LOG_DEBUG = 4,
        LOG_VERBOSE = 5
    };
    
    // =============================================================================
    // CONFIGURATION MONITEUR SÉRIE
    // =============================================================================
    // Le flag SERIAL_MONITOR_ENABLED contrôle l'activation/désactivation du moniteur série.
    // Le flag SENSOR_LOGS_ENABLED contrôle spécifiquement les messages des capteurs.
    // 
    // UTILISATION:
    // - Définir ENABLE_SERIAL_MONITOR=0 dans platformio.ini pour désactiver le moniteur série
    // - Définir ENABLE_SERIAL_MONITOR=1 dans platformio.ini pour activer le moniteur série
    // - Définir ENABLE_SENSOR_LOGS=0 pour désactiver uniquement les logs de capteurs
    // - Définir ENABLE_SENSOR_LOGS=1 pour activer les logs de capteurs
    // - Si non défini, utilise la configuration par défaut selon le profil
    // 
    // EXEMPLES dans platformio.ini:
    // build_flags = -DENABLE_SERIAL_MONITOR=0    # Désactiver le moniteur série
    // build_flags = -DENABLE_SERIAL_MONITOR=1    # Activer le moniteur série
    // build_flags = -DENABLE_SENSOR_LOGS=0       # Désactiver les logs de capteurs uniquement
    // build_flags = -DENABLE_SENSOR_LOGS=1       # Activer les logs de capteurs
    //
    // Flag global pour activer/désactiver le moniteur série
    // Peut être surchargé par les profils ou défini explicitement
    #ifndef ENABLE_SERIAL_MONITOR
    #if defined(PROFILE_PROD)
        constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        constexpr bool SERIAL_ENABLED = false;
        constexpr bool SERIAL_MONITOR_ENABLED = false;  // Nouveau flag explicite
    #elif defined(PROFILE_TEST)
        constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        constexpr bool SERIAL_ENABLED = true;
        constexpr bool SERIAL_MONITOR_ENABLED = true;   // Nouveau flag explicite
    #else // PROFILE_DEV ou default
        constexpr LogLevel DEFAULT_LEVEL = LOG_DEBUG;
        constexpr bool SERIAL_ENABLED = true;
        constexpr bool SERIAL_MONITOR_ENABLED = true;   // Nouveau flag explicite
    #endif
    #else
        // Utiliser la définition explicite ENABLE_SERIAL_MONITOR si fournie
        constexpr bool SERIAL_MONITOR_ENABLED = (ENABLE_SERIAL_MONITOR == 1);
        #if defined(PROFILE_PROD)
            constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #elif defined(PROFILE_TEST)
            constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #else // PROFILE_DEV ou default
            constexpr LogLevel DEFAULT_LEVEL = LOG_DEBUG;
            constexpr bool SERIAL_ENABLED = SERIAL_MONITOR_ENABLED;
        #endif
    #endif
    
    // Flag pour désactiver les messages des capteurs dans le moniteur série
    // Permet de masquer les logs des capteurs pour se concentrer sur d'autres messages
    #ifndef ENABLE_SENSOR_LOGS
        // Par défaut, les logs de capteurs sont activés si le moniteur série est activé
        constexpr bool SENSOR_LOGS_ENABLED = SERIAL_MONITOR_ENABLED;
    #else
        // Utiliser la définition explicite ENABLE_SENSOR_LOGS si fournie
        constexpr bool SENSOR_LOGS_ENABLED = (ENABLE_SENSOR_LOGS == 1) && SERIAL_MONITOR_ENABLED;
    #endif
    
    // Macros de log unifiées
    #define LOG_ERROR(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_ERROR && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[ERROR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_WARN && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_INFO && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    #define LOG_DEBUG(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_DEBUG && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[DEBUG] " fmt "\n", ##__VA_ARGS__)
    #define LOG_VERBOSE(fmt, ...) if(LogConfig::DEFAULT_LEVEL >= LogConfig::LOG_VERBOSE && LogConfig::SERIAL_ENABLED && LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf("[VERB] " fmt "\n", ##__VA_ARGS__)

    // Macros conditionnelles pour rediriger les appels Serial directs
    // Ces macros permettent de désactiver complètement les appels Serial.print quand SERIAL_MONITOR_ENABLED = false
    #define SERIAL_PRINT_IF_ENABLED(x) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.print(x); } while(0)
    #define SERIAL_PRINTLN_IF_ENABLED(x) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.println(x); } while(0)
    #define SERIAL_PRINTF_IF_ENABLED(fmt, ...) do { if (LogConfig::SERIAL_MONITOR_ENABLED) Serial.printf(fmt, ##__VA_ARGS__); } while(0)

    // Macros spécialisées pour les logs de capteurs - peuvent être désactivées indépendamment
    // Utilisez ces macros pour tous les messages concernant les capteurs ([Sensor], [AirSensor], [SystemSensors], etc.)
    #define SENSOR_LOG_PRINT(x) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.print(x); } while(0)
    #define SENSOR_LOG_PRINTLN(x) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.println(x); } while(0)
    #define SENSOR_LOG_PRINTF(fmt, ...) do { if (LogConfig::SENSOR_LOGS_ENABLED) Serial.printf(fmt, ##__VA_ARGS__); } while(0)
}

// Désactivation complète de Serial en production quand le moniteur est off
// v11.116: Implémentation sûre qui évite les crashs et optimise le code compilé
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 0)) || (!defined(ENABLE_SERIAL_MONITOR) && defined(PROFILE_PROD))
    // Structure NullSerial optimisée pour être complètement éliminée à la compilation
    namespace LogConfig {
        struct NullSerialType {
            // Toutes les méthodes sont inline constexpr pour élimination à la compilation
            template<typename... Args>
            inline constexpr size_t printf(const char*, Args...) const { return 0U; }
            inline constexpr size_t println() const { return 0U; }
            template<typename T>
            inline constexpr size_t println(const T&) const { return 0U; }
            template<typename T>
            inline constexpr size_t print(const T&) const { return 0U; }
            // Méthodes supplémentaires pour compatibilité complète
            inline constexpr void begin(unsigned long) const {}
            inline constexpr void end() const {}
            inline constexpr void flush() const {}
            inline constexpr int available() const { return 0; }
            inline constexpr int read() const { return -1; }
            inline constexpr size_t write(uint8_t) const { return 0U; }
            inline constexpr size_t write(const uint8_t*, size_t) const { return 0U; }
            inline constexpr operator bool() const { return false; }
        };
        static constexpr NullSerialType NullSerial{};
    }
    // Redéfinition de Serial uniquement si PROFILE_PROD et SERIAL désactivé
    #define Serial LogConfig::NullSerial
    // Désactiver aussi Serial1 et Serial2 s'ils existent
    #define Serial1 LogConfig::NullSerial
    #define Serial2 LogConfig::NullSerial
#endif
