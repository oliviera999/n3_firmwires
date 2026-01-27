#pragma once
/**
 * @file tls_mutex.h
 * @brief Mutex global pour sérialiser les connexions TLS (SMTP + HTTPS)
 * 
 * Solution au crash coredump identifié :
 * - Cause: strcat(NULL) dans ESP Mail Client pendant connexion SMTP
 * - Raison: Mémoire insuffisante quand SMTP et HTTPS simultanés (~42KB chacun)
 * - Solution: Mutex pour empêcher les connexions TLS concurrentes
 * 
 * Usage:
 *   if (TLSMutex::acquire(5000)) {
 *       // Faire la connexion TLS
 *       TLSMutex::release();
 *   }
 * 
 * Note importante:
 * - Les opérations réseau via netTask (via g_netQueue) sont déjà sérialisées
 *   et n'ont PAS besoin de TLSMutex (sérialisation naturelle par la queue)
 * - TLSMutex est nécessaire uniquement pour les appels directs à web_client:
 *   * mailer.cpp: sendAlertSync() - appel direct avec TLSMutex
 *   * app_tasks.cpp: netTask boot - appel direct fetchRemoteState()
 */

#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <Arduino.h>

// Seuil minimum de heap requis pour une connexion TLS sécurisée
// SMTP/HTTPS + buffer = ~42KB, marge augmentée pour éviter allocations à la limite
// PISTE 2: Augmenté de 45KB à 52000 (52KB) pour marge de sécurité plus importante
// v11.157: Augmenté à 62000 (62KB) pour résoudre échecs allocation mémoire TLS
// Le handshake TLS nécessite ~42-46KB contiguë, avec fragmentation on a besoin de plus
constexpr uint32_t TLS_MIN_HEAP_BYTES = 62000;

// Flag global indiquant si le système entre en light sleep
// Permet de bloquer les nouvelles connexions TLS pendant la transition
extern volatile bool g_enteringLightSleep;

class TLSMutex {
public:
    /**
     * Initialise le mutex TLS (appeler une fois au boot)
     */
    static void init() {
        if (_mutex == nullptr) {
            _mutex = xSemaphoreCreateMutex();
            if (_mutex == nullptr) {
                Serial.println(F("[TLS] ❌ ERREUR CRITIQUE: Impossible de créer le mutex TLS"));
            } else {
                Serial.println(F("[TLS] ✅ Mutex TLS initialisé"));
            }
        }
    }
    
    /**
     * Acquiert le mutex TLS avec timeout
     * Vérifie aussi que le heap est suffisant et qu'on n'entre pas en sleep
     * 
     * @param timeoutMs Timeout en millisecondes (0 = essai immédiat)
     * @return true si le mutex est acquis et les conditions sont OK
     */
    static bool acquire(uint32_t timeoutMs = 5000) {
        // Vérification 1: Le système entre-t-il en light sleep ?
        if (g_enteringLightSleep) {
            Serial.println(F("[TLS] ⛔ Connexion refusée: système en transition vers light sleep"));
            return false;
        }
        
        // Vérification 2: Heap suffisant ?
        uint32_t freeHeap = ESP.getFreeHeap();
        if (freeHeap < TLS_MIN_HEAP_BYTES) {
            Serial.printf("[TLS] ⛔ Connexion refusée: heap insuffisant (%u < %u bytes)\n", 
                          freeHeap, TLS_MIN_HEAP_BYTES);
            return false;
        }
        
        // Vérification 3: Mutex initialisé ?
        if (_mutex == nullptr) {
            Serial.println(F("[TLS] ⚠️ Mutex non initialisé, initialisation..."));
            init();
            if (_mutex == nullptr) return false;
        }
        
        // Acquisition du mutex
        if (xSemaphoreTake(_mutex, pdMS_TO_TICKS(timeoutMs)) == pdTRUE) {
            // Double vérification après acquisition (le contexte peut avoir changé)
            if (g_enteringLightSleep) {
                Serial.println(
                  F("[TLS] ⛔ Light sleep détecté après acquisition, libération mutex"));
                xSemaphoreGive(_mutex);
                return false;
            }
            
            freeHeap = ESP.getFreeHeap();
            if (freeHeap < TLS_MIN_HEAP_BYTES) {
                Serial.printf(
                  "[TLS] ⛔ Heap insuffisant après acquisition (%u bytes), libération\n",
                  freeHeap);
                xSemaphoreGive(_mutex);
                return false;
            }
            
            Serial.printf("[TLS] 🔒 Mutex acquis (heap: %u bytes)\n", freeHeap);
            return true;
        }
        
        Serial.printf("[TLS] ⏳ Timeout acquisition mutex (%u ms)\n", timeoutMs);
        return false;
    }
    
    /**
     * Libère le mutex TLS
     */
    static void release() {
        if (_mutex != nullptr) {
            xSemaphoreGive(_mutex);
            Serial.printf("[TLS] 🔓 Mutex libéré (heap: %u bytes)\n", ESP.getFreeHeap());
        }
    }
    
    /**
     * Vérifie si une connexion TLS est possible (sans acquérir le mutex)
     */
    static bool canConnect() {
        if (g_enteringLightSleep) return false;
        if (ESP.getFreeHeap() < TLS_MIN_HEAP_BYTES) return false;
        if (_mutex == nullptr) return false;
        return true;
    }
    
    /**
     * Retourne le heap minimum requis pour TLS
     */
    static uint32_t getMinHeapRequired() {
        return TLS_MIN_HEAP_BYTES;
    }

private:
    static SemaphoreHandle_t _mutex;
};
