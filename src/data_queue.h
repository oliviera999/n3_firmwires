#pragma once

#include <Arduino.h>
#include <cstring>

/**
 * File d'attente RAM simple pour données non envoyées au serveur
 * 
 * Version simplifiée: tableau circulaire en RAM (5 entrées max)
 * Garantit FIFO: première entrée = première envoyée
 * 
 * Architecture:
 * - Tableau circulaire en RAM, stockage fixe (pas de malloc)
 * - Max: DATA_QUEUE_MAX_ENTRIES entrées (limite logique <= capacité fixe)
 * - Pas de persistance (données perdues au reboot)
 * 
 * Thread-Safety:
 * - DataQueue est thread-safe par design (pas de mutex nécessaire)
 * - Utilisé uniquement depuis automationTask via AutomatismSync
 * - Sérialisation naturelle garantie par l'exécution séquentielle de automationTask
 * - Aucun accès concurrent possible (une seule tâche y accède)
 */
class DataQueue {
public:
    /**
     * Constructeur
     * @param maxEntries Nombre maximum d'entrées (défaut: 5)
     */
    DataQueue(uint16_t maxEntries = 5);
    
    /**
     * Destructeur (stockage fixe, rien à libérer)
     */
    ~DataQueue();
    
    /**
     * Initialise la queue (pas d'opération nécessaire pour RAM)
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
    static constexpr size_t PAYLOAD_SIZE = 1024;
    static constexpr uint16_t DATA_QUEUE_MAX_ENTRIES = 10;

    uint16_t _maxEntries;
    uint16_t _head;
    uint16_t _tail;
    uint16_t _count;
    char _storage[DATA_QUEUE_MAX_ENTRIES][PAYLOAD_SIZE];
    bool _initialized;
};

