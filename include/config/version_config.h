#pragma once

// =============================================================================
// VERSION ET IDENTIFICATION DU PROJET FFP5CS
// =============================================================================
// Module: Configuration de version, identification et utilitaires
// Taille: ~66 lignes
// =============================================================================

#include <Arduino.h>

// =============================================================================
// VERSION ET IDENTIFICATION
// =============================================================================
namespace ProjectConfig {
    constexpr const char* VERSION = "11.118"; // v11.118: Optimisation mémoire - Buffers adaptatifs selon board
    
    // Type d'environnement (dev, test, prod)
    #if defined(PROFILE_DEV)
        constexpr const char* PROFILE_TYPE = "dev";
    #elif defined(PROFILE_TEST)
        constexpr const char* PROFILE_TYPE = "test";
    #elif defined(PROFILE_PROD)
        constexpr const char* PROFILE_TYPE = "prod";
    #else
        constexpr const char* PROFILE_TYPE = "unknown";
    #endif
    
    // Identification matérielle
    #if defined(BOARD_S3)
        constexpr const char* BOARD_TYPE = "esp32-s3";
    #else
        constexpr const char* BOARD_TYPE = "esp32-wroom";
    #endif
}

// =============================================================================
// HELPERS ET UTILITAIRES
// =============================================================================
namespace Utils {
    // Obtenir le nom du profil actuel
    inline const char* getProfileName() {
        #if defined(PROFILE_PROD)
            return "PRODUCTION";
        #elif defined(PROFILE_TEST)
            return "TEST";
        #elif defined(PROFILE_DEV)
            return "DEVELOPMENT";
        #else
            return "DEFAULT";
        #endif
    }
    
    // Obtenir une description complète du système
    inline void getSystemInfo(char* buffer, size_t bufferSize) {
        snprintf(buffer, bufferSize, "FFP5CS v%s [%s/%s]", 
                 ProjectConfig::VERSION, ProjectConfig::BOARD_TYPE, getProfileName());
    }
}
