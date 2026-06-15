#pragma once

#include <Arduino.h>
#include <functional>
#include "system_actuators.h"
#include "config_manager.h"
#include "mailer.h"
#include "power.h"

/**
 * Module FeedingSchedule pour Automatism
 * 
 * Responsabilité: Gestion du nourrissage automatique programmé
 * - Vérification des horaires (matin, midi, soir)
 * - Détection du changement de jour
 * - Déclenchement du nourrissage séquentiel
 * - Persistance via ConfigManager
 */
class AutomatismFeedingSchedule {
public:
    /**
     * Constructeur
     * @param acts Référence IActuators (interface — cf. iactuators.h)
     * @param cfg Référence ConfigManager (pour persistance)
     * @param mail Référence Mailer (pour notifications)
     * @param power Référence PowerManager (pour temps)
     */
    AutomatismFeedingSchedule(IActuators& acts, ConfigManager& cfg,
                              IMailer& mail, PowerManager& power);
    
    /** Délai après boot pendant lequel le rattrapage (catch-up) est désactivé. */
    static constexpr uint32_t FEEDING_BOOT_GRACE_MS = 120000;  // 2 min

    /** Délai (secondes) entre nourrissage gros et petits poissons (aligné feedSequential). */
    static constexpr uint16_t FEEDING_DELAY_BETWEEN_SEC = 2;

    /** Plafond par distribution (s). La durée ne calibre pas une quantité en grammes :
     *  ce plafond borne le pire cas mécanique en cas de config aberrante. */
    static constexpr uint16_t FEEDING_MAX_DURATION_SEC = 60;

    /** Plafond cumulé journalier (s, gros + petits confondus) — anti-surdosage si
     *  l'horloge saute ou si les flags sont corrompus (3 repas max à durée max).
     *  Le cumul est persisté en NVS (ConfigManager) : il survit aux reboots et est
     *  remis à zéro au changement de jour. */
    static constexpr uint32_t FEEDING_MAX_DAILY_SEC = 3 * 2 * FEEDING_MAX_DURATION_SEC;

    /**
     * Configuration du planning de nourrissage (heures des créneaux + durées).
     * Regroupée dans une struct pour éviter les inversions d'arguments positionnels
     * sur le chemin chaud : au site d'appel, utiliser les designated initializers
     * (`.morningHour = ...`) rend les permutations silencieuses impossibles.
     */
    struct FeedingParams {
        uint8_t morningHour;     ///< Heure du créneau matin (0-23)
        uint8_t noonHour;        ///< Heure du créneau midi (0-23)
        uint8_t eveningHour;     ///< Heure du créneau soir (0-23)
        uint16_t bigDuration;    ///< Durée nourrissage gros poissons (s)
        uint16_t smallDuration;  ///< Durée nourrissage petits poissons (s)
    };

    /**
     * Vérifie et déclenche le nourrissage automatique selon l'heure
     * @param hour Heure actuelle (0-23)
     * @param minute Minute actuelle (0-59)
     * @param dayOfYear Jour de l'année (0-365)
     * @param uptimeMs Uptime en ms (pour désactiver le rattrapage au boot)
     * @param params Heures des créneaux et durées de distribution
     * @param emailAddr Adresse email pour notifications
     * @param mailNotif Email activé ou non
     * @param mailBlinkCallback Callback pour clignotement icône mail OLED
     * @param feedingStartCallback Callback appelé au début du nourrissage (avec type)
     * @param feedingCompleteCallback Callback appelé après nourrissage pour sync serveur
     */
    void checkAndFeed(int hour, int minute, int dayOfYear, uint32_t uptimeMs,
                     const FeedingParams& params,
                     const char* emailAddr, bool mailNotif,
                     std::function<void()> mailBlinkCallback,
                     std::function<void(const char*)> feedingStartCallback = nullptr,
                     std::function<void()> feedingCompleteCallback = nullptr);
    
    /**
     * Vérifie et gère le changement de jour (reset flags)
     * @param currentDay Jour de l'année actuel
     */
    void checkNewDay(int currentDay);
    
    /**
     * Obtient l'état actuel du nourrissage
     */
    struct Status {
        bool morningDone;
        bool noonDone;
        bool eveningDone;
        int lastDay;
    };
    
    Status getStatus() const;
    
private:
    IActuators& _acts;
    ConfigManager& _config;
    IMailer& _mailer;
    PowerManager& _power;
    
    // Dernière config invalide signalée (évite le spam de warnings dans la boucle)
    uint32_t _lastWarnedConfig = 0xFFFFFFFF;

    // Helpers privés
    bool shouldFeedNow(int hour, int minute, uint8_t scheduleHour) const;
    bool shouldCatchUpFeed(int hour, uint8_t scheduleHour) const;
    void warnIfScheduleConfigInvalid(uint8_t morningHour, uint8_t noonHour, uint8_t eveningHour);
    /// Détecte les créneaux dont la fenêtre + rattrapage sont passés sans nourrissage,
    /// les marque faits (l'heure est passée, ne pas nourrir en décalé) et alerte par email.
    void handleMissedSlots(int hour, uint8_t morningHour, uint8_t noonHour, uint8_t eveningHour,
                           const char* emailAddr, bool mailNotif);
    /// Retourne true si la séquence a démarré (false = refusée, ne pas marquer le créneau)
    bool performFeeding(uint16_t bigDuration, uint16_t smallDuration,
                       const char* emailAddr, bool mailNotif,
                       std::function<void()> mailBlinkCallback,
                       std::function<void(const char*)> feedingStartCallback = nullptr,
                       std::function<void()> feedingCompleteCallback = nullptr);
    void sendFeedingEmail(const char* type, uint16_t bigDur, uint16_t smallDur,
                         const char* emailAddr, bool mailNotif);
    void sendAlertEmail(const char* subject, const char* body,
                        const char* emailAddr, bool mailNotif);

    /// Marque « fait » tous les créneaux dont l’heure programmée == scheduleHour (évite 3 repas si 105/106/107 identiques).
    void markSlotsDoneForScheduleHour(uint8_t scheduleHour,
                                      uint8_t morningHour, uint8_t noonHour, uint8_t eveningHour);
};
