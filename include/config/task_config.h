#pragma once

// =============================================================================
// CONFIGURATION TÂCHES FREERTOS - STRATÉGIE WEB DÉDIÉ
// =============================================================================
// Module: Configuration des tâches FreeRTOS (stack, priorités, cores)
// Taille: ~24 lignes
// =============================================================================

#include <Arduino.h>

namespace TaskConfig {
    // Tailles de stack (optimisées - réduites de 25% avec marge de sécurité)
    constexpr uint32_t SENSOR_TASK_STACK_SIZE = 6144;        // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t WEB_TASK_STACK_SIZE = 6144;           // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t AUTOMATION_TASK_STACK_SIZE = 9216;    // Réduit de 12288 à 9216 (économie: 3KB)
    constexpr uint32_t DISPLAY_TASK_STACK_SIZE = 6144;       // Réduit de 8192 à 6144 (économie: 2KB)
    constexpr uint32_t OTA_TASK_STACK_SIZE = 9216;           // Réduit de 12288 à 9216 (économie: 3KB)
    
    // Priorités des tâches - Stratégie Web Dédié Optimisée
    constexpr uint8_t SENSOR_TASK_PRIORITY = 5;             // CRITIQUE - Capteurs prioritaires absolus
    constexpr uint8_t WEB_TASK_PRIORITY = 4;                 // TRÈS HAUTE - Interface web ultra-réactive
    constexpr uint8_t AUTOMATION_TASK_PRIORITY = 2;          // MOYENNE - Logique métier pure
    constexpr uint8_t DISPLAY_TASK_PRIORITY = 1;             // BASSE - Affichage en arrière-plan

    // Core d'exécution
    constexpr uint8_t SENSOR_TASK_CORE_ID = 1;               // Acquisition capteurs sur core 1 (loop Arduino)
    constexpr uint8_t AUTOMATION_TASK_CORE_ID = 1;           // Logique métier alignée avec capteurs
    constexpr uint8_t WEB_TASK_CORE_ID = 0;                  // Serveur web/WebSocket sur core 0 (stack WiFi)
    constexpr uint8_t DISPLAY_TASK_CORE_ID = 0;              // Affichage OLED sur core 0

    // Configuration Web Server optimisée
    constexpr uint8_t WEB_SERVER_CORE_ID = 0;              // Serveur web sur Core 0 (CPU principal)
    constexpr uint8_t WEB_SERVER_PRIORITY = 4;             // Priorité élevée pour le serveur web
    constexpr uint32_t WEB_SERVER_STACK_SIZE = 9216;       // Stack réduit pour le serveur web (économie: 3KB)
}
