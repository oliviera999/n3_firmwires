#include "automatism/automatism_feeding_schedule.h"
#include "automatism/feeding_slot_matcher.h"
#include "config.h"
#include "wifi_manager.h"  // Pour WiFiHelpers
#include <cstring>

AutomatismFeedingSchedule::AutomatismFeedingSchedule(SystemActuators& acts, ConfigManager& cfg,
                                                     Mailer& mail, PowerManager& power)
    : _acts(acts), _config(cfg), _mailer(mail), _power(power) {
    // Charger les flags depuis NVS au démarrage
    _config.loadBouffeFlags();
}

void AutomatismFeedingSchedule::checkNewDay(int currentDay) {
    int lastDay = _config.getLastJourBouf();
    if (lastDay != currentDay && lastDay != -1) {
        // Nouveau jour détecté - réinitialiser les flags
        Serial.println(F("[FeedingSchedule] Nouveau jour détecté - réinitialisation flags"));
        _config.resetBouffeFlags();
        _config.setLastJourBouf(currentDay);
        _config.saveBouffeFlags();
        _dailyFeedSec = 0;
    } else if (lastDay == -1) {
        // Première initialisation
        _config.setLastJourBouf(currentDay);
        _config.saveBouffeFlags();
    }
}

void AutomatismFeedingSchedule::checkAndFeed(int hour, int minute, int dayOfYear, uint32_t uptimeMs,
                                             uint8_t morningHour, uint8_t noonHour, uint8_t eveningHour,
                                             uint16_t bigDuration, uint16_t smallDuration,
                                             const char* emailAddr, bool mailNotif,
                                             std::function<void()> mailBlinkCallback,
                                             std::function<void(const char*)> feedingStartCallback,
                                             std::function<void()> feedingCompleteCallback) {
    // Vérifier changement de jour
    checkNewDay(dayOfYear);

    warnIfScheduleConfigInvalid(morningHour, noonHour, eveningHour);

    // Désactiver le rattrapage pendant la période de boot (évite nourrissage au démarrage)
    const bool catchUpAllowed = (uptimeMs >= FEEDING_BOOT_GRACE_MS);

    // Repas définitivement manqués (réveil après H+1) : marquer + alerter.
    // Après la période de grâce uniquement, le temps que config et heure se stabilisent au boot.
    if (catchUpAllowed) {
        handleMissedSlots(hour, morningHour, noonHour, eveningHour, emailAddr, mailNotif);
    }

    // Vérifier chaque horaire (fenêtre heure + rattrapage limité si réveil après le créneau)
    const bool matinOk = _config.getBouffeMatinOk();
    const bool midiOk = _config.getBouffeMidiOk();
    const bool soirOk = _config.getBouffeSoirOk();
    bool shouldFeedMorning = (shouldFeedNow(hour, minute, morningHour) || (catchUpAllowed && shouldCatchUpFeed(hour, morningHour))) && !matinOk;
    bool shouldFeedNoon = (shouldFeedNow(hour, minute, noonHour) || (catchUpAllowed && shouldCatchUpFeed(hour, noonHour))) && !midiOk;
    bool shouldFeedEvening = (shouldFeedNow(hour, minute, eveningHour) || (catchUpAllowed && shouldCatchUpFeed(hour, eveningHour))) && !soirOk;

    const char* slotLabel = nullptr;
    uint8_t slotHour = 0;
    if (shouldFeedMorning) {
        slotLabel = "Bouffe matin";
        slotHour = morningHour;
    } else if (shouldFeedNoon) {
        slotLabel = "Bouffe midi";
        slotHour = noonHour;
    } else if (shouldFeedEvening) {
        slotLabel = "Bouffe soir";
        slotHour = eveningHour;
    }
    if (!slotLabel) {
        return;
    }

    // Plafond par distribution (la durée ne calibre pas une quantité : borner le pire cas)
    if (bigDuration > FEEDING_MAX_DURATION_SEC || smallDuration > FEEDING_MAX_DURATION_SEC) {
        Serial.printf("[FeedingSchedule] ⚠️ Durées plafonnées à %u s (gros=%u, petits=%u)\n",
                      FEEDING_MAX_DURATION_SEC, bigDuration, smallDuration);
        if (bigDuration > FEEDING_MAX_DURATION_SEC) bigDuration = FEEDING_MAX_DURATION_SEC;
        if (smallDuration > FEEDING_MAX_DURATION_SEC) smallDuration = FEEDING_MAX_DURATION_SEC;
    }

    // Plafond cumulé journalier anti-surdosage (horloge qui saute, flags corrompus...)
    const uint32_t feedTotalSec = static_cast<uint32_t>(bigDuration) + smallDuration;
    if (_dailyFeedSec + feedTotalSec > FEEDING_MAX_DAILY_SEC) {
        Serial.printf("[FeedingSchedule] 🛑 Plafond journalier atteint (%lu s + %lu s > %lu s) - %s annulé\n",
                      (unsigned long)_dailyFeedSec, (unsigned long)feedTotalSec,
                      (unsigned long)FEEDING_MAX_DAILY_SEC, slotLabel);
        // Marquer le créneau pour ne pas retenter en boucle, et alerter
        markSlotsDoneForScheduleHour(slotHour, morningHour, noonHour, eveningHour);
        char body[160];
        snprintf(body, sizeof(body),
                 "%s annulé : plafond anti-surdosage journalier atteint (%lu s déjà distribuées, max %lu s).",
                 slotLabel, (unsigned long)_dailyFeedSec, (unsigned long)FEEDING_MAX_DAILY_SEC);
        sendAlertEmail("FFP5CS - 🛑 Plafond nourrissage atteint", body, emailAddr, mailNotif);
        return;
    }

    Serial.printf("[FeedingSchedule] 🍽️ Nourrissage automatique %s (%02d:%02d)\n", slotLabel, hour, minute);
    if (!performFeeding(bigDuration, smallDuration, emailAddr, mailNotif, mailBlinkCallback, feedingStartCallback, feedingCompleteCallback)) {
        // Séquence refusée (déjà en cours) : ne PAS marquer le créneau ni envoyer l'email
        // de confirmation (évite les faux positifs). Nouvelle tentative au prochain passage.
        Serial.println(F("[FeedingSchedule] ⚠️ Séquence refusée (déjà en cours) - créneau non marqué"));
        return;
    }
    _dailyFeedSec += feedTotalSec;
    markSlotsDoneForScheduleHour(slotHour, morningHour, noonHour, eveningHour);
    sendFeedingEmail(slotLabel, bigDuration, smallDuration, emailAddr, mailNotif);
}

void AutomatismFeedingSchedule::warnIfScheduleConfigInvalid(uint8_t morningHour, uint8_t noonHour,
                                                            uint8_t eveningHour) {
    if (FeedingSlotMatcher::isScheduleConfigValid(morningHour, noonHour, eveningHour)) {
        _lastWarnedConfig = 0xFFFFFFFF;
        return;
    }
    const uint32_t configKey = (static_cast<uint32_t>(morningHour) << 16) |
                               (static_cast<uint32_t>(noonHour) << 8) | eveningHour;
    if (configKey == _lastWarnedConfig) {
        return;  // Déjà signalé pour cette config (évite le spam dans la boucle)
    }
    _lastWarnedConfig = configKey;
    Serial.printf("[FeedingSchedule] ⚠️ Config horaires invalide (matin=%u midi=%u soir=%u) : "
                  "heures attendues distinctes, croissantes, dans [0..23]. "
                  "Un repas marquera tous les créneaux partageant la même heure.\n",
                  morningHour, noonHour, eveningHour);
}

void AutomatismFeedingSchedule::handleMissedSlots(int hour, uint8_t morningHour, uint8_t noonHour,
                                                  uint8_t eveningHour,
                                                  const char* emailAddr, bool mailNotif) {
    struct MissedSlot {
        const char* name;
        uint8_t schedHour;
    };
    // Les flags sont relus à chaque itération : un créneau partageant l'heure d'un créneau
    // déjà traité a été marqué par markSlotsDoneForScheduleHour au tour précédent.
    const MissedSlot slots[3] = {
        {"matin", morningHour},
        {"midi", noonHour},
        {"soir", eveningHour},
    };
    for (const MissedSlot& slot : slots) {
        const bool done = (slot.schedHour == morningHour && _config.getBouffeMatinOk()) ||
                          (slot.schedHour == noonHour && _config.getBouffeMidiOk()) ||
                          (slot.schedHour == eveningHour && _config.getBouffeSoirOk());
        if (done || !FeedingSlotMatcher::isSlotMissed(hour, slot.schedHour)) {
            continue;
        }
        Serial.printf("[FeedingSchedule] ⚠️ Repas %s MANQUÉ (créneau %02u h, il est %02d h) - non rattrapé\n",
                      slot.name, slot.schedHour, hour);
        // Marquer le créneau : l'heure est passée, nourrir en décalé décalerait le rythme,
        // et cela évite de répéter l'alerte à chaque passage
        markSlotsDoneForScheduleHour(slot.schedHour, morningHour, noonHour, eveningHour);
        char subject[64];
        snprintf(subject, sizeof(subject), "FFP5CS - ⚠️ Repas %s manqué", slot.name);
        char body[192];
        snprintf(body, sizeof(body),
                 "Le repas du %s (créneau %02u h) n'a pas été distribué : l'appareil était "
                 "probablement endormi ou éteint pendant la fenêtre %02u h - %02u h. "
                 "Aucun rattrapage ne sera effectué.",
                 slot.name, slot.schedHour, slot.schedHour,
                 (slot.schedHour + 1) % 24);
        sendAlertEmail(subject, body, emailAddr, mailNotif);
    }
}

void AutomatismFeedingSchedule::markSlotsDoneForScheduleHour(uint8_t scheduleHour,
                                                             uint8_t morningHour, uint8_t noonHour,
                                                             uint8_t eveningHour) {
    if (morningHour == scheduleHour) {
        _config.setBouffeMatinOk(true);
    }
    if (noonHour == scheduleHour) {
        _config.setBouffeMidiOk(true);
    }
    if (eveningHour == scheduleHour) {
        _config.setBouffeSoirOk(true);
    }
    _config.saveBouffeFlags();
}

bool AutomatismFeedingSchedule::shouldFeedNow(int hour, int minute, uint8_t scheduleHour) const {
    // À l'heure programmée, fenêtre = toute l'heure (H:00 à H:59)
    return (hour == scheduleHour);
}

bool AutomatismFeedingSchedule::shouldCatchUpFeed(int hour, uint8_t scheduleHour) const {
    // Rattrapage limité à l'heure suivant le créneau (réveil sleep léger après l'horaire)
    const int nextHour = (static_cast<int>(scheduleHour) + 1) % 24;
    return (hour == nextHour);
}

bool AutomatismFeedingSchedule::performFeeding(uint16_t bigDuration, uint16_t smallDuration,
                                              const char* emailAddr, bool mailNotif,
                                              std::function<void()> mailBlinkCallback,
                                              std::function<void(const char*)> feedingStartCallback,
                                              std::function<void()> feedingCompleteCallback) {
    // Démarrer la séquence AVANT toute notification : si elle est refusée
    // (déjà en cours), rien ne doit être marqué ni notifié (évite les faux positifs)
    if (!_acts.feedSequential(bigDuration, smallDuration, FEEDING_DELAY_BETWEEN_SEC)) {
        return false;
    }

    // Déclencher le clignotement mail si callback fourni
    if (mailBlinkCallback) {
        mailBlinkCallback();
    }

    // Notifier le début du nourrissage (phase "Gros")
    if (feedingStartCallback) {
        feedingStartCallback("Gros");
    }

    Serial.println(F("[FeedingSchedule] Automatic feeding triggered"));

    // Notifier la fin du nourrissage pour synchroniser avec le serveur
    if (feedingCompleteCallback) {
        feedingCompleteCallback();
    }
    return true;
}

void AutomatismFeedingSchedule::sendFeedingEmail(const char* type, uint16_t bigDur, uint16_t smallDur,
                                                 const char* emailAddr, bool mailNotif) {
    if (!mailNotif || !emailAddr || strlen(emailAddr) == 0) {
        return;
    }
    
    // Construire le message dans un buffer fixe
    char message[512];
    char sysInfo[128];
    Utils::getSystemInfo(sysInfo, sizeof(sysInfo));
    
    // Uptime formaté
    unsigned long totalSec = millis() / 1000UL;
    unsigned int days = totalSec / 86400UL;
    totalSec %= 86400UL;
    unsigned int hours = totalSec / 3600UL;
    totalSec %= 3600UL;
    unsigned int mins = totalSec / 60UL;
    unsigned int secs = totalSec % 60UL;
    char uptimeStr[32];
    snprintf(uptimeStr, sizeof(uptimeStr), "%ud %02u:%02u:%02u", days, hours, mins, secs);
    
    // État réseau
    bool wifiConnected = WiFi.status() == WL_CONNECTED;
    const char* wifiStatus;
    char wifiDetail[64] = "";
    if (wifiConnected) {
        char ssidBuf[33];
        WiFiHelpers::getSSID(ssidBuf, sizeof(ssidBuf));
        snprintf(wifiDetail, sizeof(wifiDetail), " (%s)", ssidBuf);
        wifiStatus = "Connecté";
    } else {
        wifiStatus = "Déconnecté";
    }
    
    char timeStr[64];
    _power.getCurrentTimeStringForMail(timeStr, sizeof(timeStr));
    
    int n = snprintf(message, sizeof(message),
        "%s\n\n"
        "Système: %s\n"
        "Heure: %s\n"
        "Durée Gros: %u s\n"
        "Durée Petits: %u s\n"
        "Mode: Automatique\n"
        "Uptime: %s\n"
        "WiFi: %s%s\n",
        type,
        sysInfo,
        timeStr,
        bigDur, smallDur,
        uptimeStr,
        wifiStatus, wifiDetail);
    
    if (n > 0 && (size_t)n < sizeof(message)) {
        char subject[128];
        snprintf(subject, sizeof(subject), "FFP5CS - %s [%s]", type, sysInfo);
        
        bool queued = _mailer.send(subject, message, "System", emailAddr);
        if (queued) {
            Serial.printf("[FeedingSchedule] ✅ Email de nourrissage ajouté à la queue: %s\n", type);
        } else {
            Serial.printf("[FeedingSchedule] ❌ Échec ajout à la queue email: %s\n", type);
        }
    }
}

void AutomatismFeedingSchedule::sendAlertEmail(const char* subject, const char* body,
                                               const char* emailAddr, bool mailNotif) {
    if (!mailNotif || !emailAddr || strlen(emailAddr) == 0) {
        return;
    }

    char timeStr[64];
    _power.getCurrentTimeStringForMail(timeStr, sizeof(timeStr));

    char message[320];
    snprintf(message, sizeof(message), "%s\n\nHeure: %s\n", body, timeStr);

    bool queued = _mailer.send(subject, message, "System", emailAddr);
    Serial.printf("[FeedingSchedule] %s Alerte email: %s\n", queued ? "✅" : "❌", subject);
}

AutomatismFeedingSchedule::Status AutomatismFeedingSchedule::getStatus() const {
    Status s;
    s.morningDone = _config.getBouffeMatinOk();
    s.noonDone = _config.getBouffeMidiOk();
    s.eveningDone = _config.getBouffeSoirOk();
    s.lastDay = _config.getLastJourBouf();
    return s;
}
