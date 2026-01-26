#pragma once

#include <Arduino.h>
#include <LittleFS.h>

/**
 * File d'attente persistante pour données non envoyées au serveur
 * 
 * Utilise LittleFS pour stocker les payloads POST en attente
 * Format: JSON Lines (1 ligne = 1 payload)
 * Garantit FIFO: première entrée = première envoyée
 * 
 * Architecture:
 * - Fichier: /queue/pending_data.txt
 * - Max: 50 entrées (configurable)
 * - Append-only pour performance
 * - Rotation automatique si dépassement
 */
class DataQueue {
public:
    /**
     * Constructeur
     * @param maxEntries Nombre maximum d'entrées (défaut: 50)
     */
    DataQueue(uint16_t maxEntries = 50);
    
    /**
     * Initialise la queue (monte LittleFS, crée répertoire)
     * @return true si succès
     */
    bool begin();
    
    /**
     * Ajoute un payload à la queue
     * @param payload Payload à enregistrer
     * @return true si succès
     */
    bool push(const char* payload);
    
    /**
     * Récupère et supprime le premier payload
     * @param buffer Buffer pour recevoir le payload
     * @param bufferSize Taille du buffer
     * @return true si succès, false si queue vide ou buffer trop petit
     */
    bool pop(char* buffer, size_t bufferSize);
    
    /**
     * Lit le premier payload sans le supprimer
     * @param buffer Buffer pour recevoir le payload
     * @param bufferSize Taille du buffer
     * @return true si succès, false si queue vide ou buffer trop petit
     */
    bool peek(char* buffer, size_t bufferSize);
    
    /**
     * Nombre d'entrées en attente
     * @return Nombre de payloads en queue
     */
    uint16_t size();
    
    /**
     * Vide complètement la queue
     */
    void clear();
    
    /**
     * Obtient l'utilisation mémoire estimée
     * @return Taille totale en bytes
     */
    size_t getMemoryUsage();
    
    /**
     * Vérifie si la queue est pleine
     * @return true si nombre max atteint
     */
    bool isFull();
    
    /**
     * Vérifie si la queue est vide
     * @return true si aucune entrée
     */
    bool isEmpty();

private:
    static constexpr const char* QUEUE_DIR = "/queue";
    static constexpr const char* QUEUE_FILE = "/queue/pending_data.txt";
    static constexpr const char* TEMP_FILE = "/queue/temp.txt";
    
    uint16_t _maxEntries;
    bool _initialized;
    
    /**
     * Recompte les entrées dans le fichier
     */
    uint16_t countEntries();
    
    /**
     * Rotation: supprime les entrées les plus anciennes si dépassement
     * @param currentSize Nombre actuel d'entrées (évite de recalculer)
     */
    void rotateIfNeeded(uint16_t currentSize);
    
    /**
     * Crée les répertoires nécessaires
     */
    bool ensureDirectoryExists();
};

