#pragma once
// config_logging.h — niveaux de log, macros LOG_*/SENSOR_LOG_*, et stub
// NullSerial + `#define Serial` (désactivation Serial en prod). Extrait de
// config.h (découpe). DOIT être inclus EN DERNIER par config.h (macro Serial).
#include <Arduino.h>

// -----------------------------------------------------------------------------
// 6. LOGGING ET DEBUG
// -----------------------------------------------------------------------------
namespace LogConfig {
    enum LogLevel {
        LOG_NONE = 0,
        LOG_ERROR = 1,
        LOG_WARN = 2,
        LOG_INFO = 3,
        LOG_DEBUG = 4,
        LOG_VERBOSE = 5
    };
    
    // Configuration par défaut selon l'environnement
    #if defined(PROFILE_PROD)
        // Production: ERROR uniquement (et INFO critique)
        inline constexpr LogLevel DEFAULT_LEVEL = LOG_ERROR;
        inline constexpr bool SERIAL_ENABLED = false;
        inline constexpr bool SENSOR_LOGS_ENABLED = false;
    #else
        // Test/Dev: INFO par défaut
        inline constexpr LogLevel DEFAULT_LEVEL = LOG_INFO;
        inline constexpr bool SERIAL_ENABLED = true;
        inline constexpr bool SENSOR_LOGS_ENABLED = true;
    #endif
}

// Macros de logging unifiées
#if defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 1)
    #define LOG_PRINT(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
    #define LOG_PRINTLN(msg) Serial.println(msg)
    
    #define LOG_ERROR(fmt, ...) Serial.printf("[ERR] " fmt "\n", ##__VA_ARGS__)
    #define LOG_WARN(fmt, ...)  Serial.printf("[WARN] " fmt "\n", ##__VA_ARGS__)
    #define LOG_INFO(fmt, ...)  Serial.printf("[INFO] " fmt "\n", ##__VA_ARGS__)
    
    // Debug seulement si niveau suffisant (à implémenter proprement si nécessaire)
    #define LOG_DEBUG(fmt, ...) Serial.printf("[DBG] " fmt "\n", ##__VA_ARGS__)
    
    // Macros conditionnelles capteurs
    #if defined(ENABLE_SENSOR_LOGS) && (ENABLE_SENSOR_LOGS == 1)
        #define SENSOR_LOG_PRINTF(fmt, ...) Serial.printf(fmt, ##__VA_ARGS__)
        #define SENSOR_LOG_PRINTLN(msg) Serial.println(msg)
    #else
        #define SENSOR_LOG_PRINTF(fmt, ...) ((void)0)
        #define SENSOR_LOG_PRINTLN(msg) ((void)0)
    #endif

#else
    // Logs désactivés
    #define LOG_PRINT(fmt, ...) ((void)0)
    #define LOG_PRINTLN(msg) ((void)0)
    #define LOG_ERROR(fmt, ...) ((void)0)
    #define LOG_WARN(fmt, ...) ((void)0)
    #define LOG_INFO(fmt, ...) ((void)0)
    #define LOG_DEBUG(fmt, ...) ((void)0)
    #define SENSOR_LOG_PRINTF(fmt, ...) ((void)0)
    #define SENSOR_LOG_PRINTLN(msg) ((void)0)
#endif

// -----------------------------------------------------------------------------
// 9. DÉSACTIVATION SÛRE DE SERIAL EN PROD
// -----------------------------------------------------------------------------
// Quand ENABLE_SERIAL_MONITOR=0 (ou profil PROD sans override), on redirige Serial
// vers un stub inline constexpr pour éliminer à la compilation les appels Serial.* et
// réduire la taille flash (wroom-prod est proche de la limite de partition).
#if (defined(ENABLE_SERIAL_MONITOR) && (ENABLE_SERIAL_MONITOR == 0)) || \
    (!defined(ENABLE_SERIAL_MONITOR) && defined(PROFILE_PROD))
namespace LogConfig {
    struct NullSerialType {
        template<typename... Args>
        inline constexpr size_t printf(const char*, Args...) const { return 0U; }
        inline constexpr size_t println() const { return 0U; }
        template<typename T>
        inline constexpr size_t println(const T&) const { return 0U; }
        template<typename T>
        inline constexpr size_t print(const T&) const { return 0U; }
        inline void begin(unsigned long) const {}
        inline void end() const {}
        inline void flush() const {}
        inline constexpr int available() const { return 0; }
        inline constexpr int read() const { return -1; }
        inline constexpr size_t write(uint8_t) const { return 0U; }
        inline constexpr size_t write(const uint8_t*, size_t) const { return 0U; }
        inline constexpr operator bool() const { return false; }
    };
    static inline constexpr NullSerialType NullSerial{};
}  // namespace LogConfig

#define Serial LogConfig::NullSerial
#define Serial1 LogConfig::NullSerial
#define Serial2 LogConfig::NullSerial
#endif
