#pragma once
#include <cstddef>
#include <Arduino.h>

/**
 * Pool de buffers pour emails - évite les allocations répétées
 * Utilise un buffer statique réutilisable pour réduire la fragmentation heap
 */
class EmailBufferPool {
private:
    static constexpr size_t BUFFER_SIZE = 6000;  // Taille max email
    static char buffer[BUFFER_SIZE];
    static bool inUse;
    static unsigned long lastUsedMs;
    
public:
    /**
     * Acquiert un buffer pour l'email
     * @return Pointeur vers le buffer ou nullptr si déjà utilisé
     */
    static char* acquire() {
        if (inUse) {
            // Vérifier si le buffer a été utilisé récemment (timeout 30s)
            if (millis() - lastUsedMs < 30000) {
                return nullptr; // Buffer encore en cours d'utilisation
            }
            // Force release si timeout
            inUse = false;
        }
        
        inUse = true;
        lastUsedMs = millis();
        buffer[0] = '\0'; // Clear buffer
        return buffer;
    }
    
    /**
     * Libère le buffer
     */
    static void release() {
        inUse = false;
    }
    
    /**
     * Vérifie si le buffer est disponible
     */
    static bool isAvailable() {
        if (!inUse) return true;
        return (millis() - lastUsedMs) >= 30000; // Timeout 30s
    }
    
    /**
     * Obtient la taille maximale du buffer
     */
    static size_t getMaxSize() {
        return BUFFER_SIZE;
    }
    
    /**
     * Obtient l'état du pool (pour debugging)
     */
    static void logStatus() {
        Serial.printf("[EmailPool] Buffer %s, Last used: %lu ms ago\n", 
                     inUse ? "OCCUPÉ" : "LIBRE", 
                     millis() - lastUsedMs);
    }
};
