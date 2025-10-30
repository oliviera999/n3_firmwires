#pragma once

#include <Arduino.h>
#include "system_actuators.h"
#include "config_manager.h"
#include "mailer.h"
#include "power.h"

/**
 * Module Feeding pour Automatism
 * 
 * Responsabilité: Gestion du nourrissage automatique et manuel des poissons
 * - Nourrissage programmé (matin, midi, soir)
 * - Nourrissage manuel (petits/gros poissons)
 * - Traçage et notification des événements
 * - Persistance de l'état
 */
class AutomatismFeeding {
public:
    /**
     * Constructeur
     * @param acts Référence SystemActuators
     * @param cfg Référence ConfigManager
     * @param mail Référence Mailer
     * @param power Référence PowerManager (pour temps)
     */
    AutomatismFeeding(SystemActuators& acts, ConfigManager& cfg, Mailer& mail, PowerManager& power);
    
    // === NOURRISSAGE AUTOMATIQUE ===
    
    /**
     * Gère le nourrissage automatique selon l'heure
     * @param hour Heure actuelle
     * @param minute Minute actuelle
     * @param mail Adresse email pour notifications
     * @param mailNotif Email activé ou non
     * @param mailBlinkCallback Callback pour clignotement icône mail OLED
     */
    void handleSchedule(int hour, int minute, const char* emailAddr, 
                       bool mailNotif, std::function<void()> mailBlinkCallback);
    
    /**
     * Vérifie et gère le changement de jour (reset flags)
     */
    void checkNewDay();
    
    /**
     * Sauvegarde l'état du feeding (jour, flags)
     */
    void saveFeedingState();
    
    // === NOURRISSAGE MANUEL ===
    
    /**
     * Nourrissage manuel petits poissons
     * @param countdownCallback Callback pour mettre à jour countdown OLED
     */
    void feedSmallManual(std::function<void(const char*, unsigned long)> countdownCallback);
    
    /**
     * Nourrissage manuel gros poissons
     * @param countdownCallback Callback pour mettre à jour countdown OLED
     */
    void feedBigManual(std::function<void(const char*, unsigned long)> countdownCallback);
    
    /**
     * Définit le callback appelé à la fin du nourrissage pour reset serveur
     * @param callback Fonction appelée avec le type de nourrissage terminé
     */
    void setCompletionCallback(std::function<void(const char*)> callback);
    
    // === TRAÇAGE ET MESSAGES ===
    
    /**
     * Trace un événement de nourrissage (petits + gros)
     * @param sendUpdateCallback Callback pour sendFullUpdate()
     */
    void traceFeedingEvent(std::function<void(const char*)> sendUpdateCallback);
    
    /**
     * Trace un événement de nourrissage sélectif
     * @param feedSmall Si true, trace petits poissons
     * @param feedBig Si true, trace gros poissons
     * @param sendUpdateCallback Callback pour sendFullUpdate()
     */
    void traceFeedingEventSelective(bool feedSmall, bool feedBig, 
                                    std::function<void(const char*)> sendUpdateCallback);
    
    /**
     * Construit un message de nourrissage formaté dans un buffer fixe
     * @param buffer      Buffer de sortie (doit être non nul)
     * @param bufferSize  Taille du buffer de sortie
     * @param type        Type de nourrissage (ex: "Bouffe matin")
     * @return Nombre de caractères écrits (hors terminator nul)
     */
    size_t createMessage(char* buffer, size_t bufferSize, const char* type) const;
    
    // === CONFIGURATION ===
    
    /**
     * Configure les horaires de nourrissage
     */
    void setSchedule(uint8_t morning, uint8_t noon, uint8_t evening);
    
    /**
     * Configure les durées de nourrissage
     */
    void setDurations(uint16_t bigDur, uint16_t smallDur);
    
    /**
     * Obtient les durées configurées
     */
    uint16_t getBigDuration() const { return _tempsGros; }
    uint16_t getSmallDuration() const { return _tempsPetits; }
    
    /**
     * Obtient les horaires configurés
     */
    uint8_t getMorningHour() const { return _bouffeMatin; }
    uint8_t getNoonHour() const { return _bouffeMidi; }
    uint8_t getEveningHour() const { return _bouffeSoir; }
    
    // === ÉTAT ===
    
    /**
     * Structure représentant l'état du feeding
     */
    struct Status {
        bool morningDone;
        bool noonDone;
        bool eveningDone;
        int lastDay;
    };
    
    /**
     * Obtient l'état actuel du feeding
     */
    Status getStatus() const;
    
    /**
     * Phases de nourrissage (pour gestion servo)
     */
    enum class Phase {
        NONE,
        FEEDING_FORWARD,
        FEEDING_BACKWARD
    };
    
    /**
     * Obtient la phase actuelle
     */
    Phase getCurrentPhase() const { return _currentPhase; }
    
    /**
     * Vérifie si feeding en cours
     */
    bool isFeedingActive() const { return _currentPhase != Phase::NONE; }
    
private:
    SystemActuators& _acts;
    ConfigManager& _config;
    Mailer& _mailer;
    PowerManager& _power;
    
    // Configuration horaires
    uint8_t _bouffeMatin;
    uint8_t _bouffeMidi;
    uint8_t _bouffeSoir;
    
    // Configuration durées
    uint16_t _tempsGros;
    uint16_t _tempsPetits;
    
    // État persistant
    int _lastFeedDay;
    
    // Phases feeding (pour servo)
    Phase _currentPhase;
    unsigned long _phaseEnd;
    unsigned long _totalEnd;
    
    // Protection double exécution
    unsigned long _lastManualFeedSmallMs;
    unsigned long _lastManualFeedBigMs;
    
    // Callback fin de nourrissage pour reset serveur
    std::function<void(const char*)> _completionCallback;
    
    // Helpers privés
    void performFeedingCycle(bool isLarge, const char* emailAddr,
                             bool mailNotif, std::function<void()> mailBlinkCallback);
    bool shouldFeedNow(int hour, int minute) const;
};

