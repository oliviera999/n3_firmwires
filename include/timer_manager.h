#pragma once
#include <Arduino.h>

/**
 * Timer Manager centralisé pour optimiser les tâches périodiques
 * Regroupe tous les timers du système pour réduire la consommation CPU
 */
class TimerManager {
public:
    typedef void (*TimerCallback)();

    static constexpr size_t MAX_TIMERS = 10;

    // Structure pour un timer
    struct Timer {
        const char* name;
        uint32_t interval;
        uint32_t lastRun;
        TimerCallback callback;
        bool enabled;
        uint32_t callCount;
        uint32_t totalTime;
        bool inUse;
        
        Timer()
            : name(nullptr), interval(0), lastRun(0), callback(nullptr), enabled(false),
              callCount(0), totalTime(0), inUse(false) {}

        void configure(const char* n, uint32_t i, TimerCallback cb) {
            name = n;
            interval = i;
            lastRun = 0;
            callback = cb;
            enabled = true;
            callCount = 0;
            totalTime = 0;
            inUse = true;
        }
    };

private:
    static Timer timers[MAX_TIMERS];
    static size_t timerCount;
    static uint32_t lastCheck;
    static bool initialized;
    
public:
    /**
     * Initialise le Timer Manager
     */
    static void init();
    
    /**
     * Ajoute un timer au manager
     * @param name Nom du timer (pour debug)
     * @param interval Intervalle en millisecondes
     * @param callback Fonction à exécuter
     * @return ID du timer ou -1 si échec
     */
    static int addTimer(const char* name, uint32_t interval, TimerCallback callback);
    
    /**
     * Traite tous les timers (à appeler dans loop())
     */
    static void process();
    
    /**
     * Active/désactive un timer
     * @param timerId ID du timer
     * @param enable true pour activer, false pour désactiver
     */
    static void enableTimer(int timerId, bool enable);
    
    /**
     * Active/désactive un timer par nom
     * @param name Nom du timer
     * @param enable true pour activer, false pour désactiver
     */
    static void enableTimer(const char* name, bool enable);
    
    /**
     * Met à jour l'intervalle d'un timer
     * @param timerId ID du timer
     * @param newInterval Nouvel intervalle en ms
     */
    static void updateInterval(int timerId, uint32_t newInterval);
    
    /**
     * Obtient les statistiques d'un timer
     * @param timerId ID du timer
     * @return Pointeur vers les stats ou nullptr
     */
    static Timer* getTimerStats(int timerId);
    
    /**
     * Obtient les statistiques de tous les timers
     */
    static void logStats();
    
    /**
     * Désactive tous les timers (mode économie d'énergie)
     */
    static void suspendAll();
    
    /**
     * Réactive tous les timers
     */
    static void resumeAll();
    
    /**
     * Obtient le nombre de timers actifs
     */
    static size_t getActiveTimerCount();
    
    /**
     * Obtient le temps total d'exécution des timers
     */
    static uint32_t getTotalExecutionTime();
};
