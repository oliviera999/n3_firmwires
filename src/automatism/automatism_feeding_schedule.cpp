#include "automatism/automatism_feeding_schedule.h"
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
    // #region agent log
    const int didReset = (lastDay != currentDay && lastDay != -1) ? 1 : 0;
    Serial.printf("{\"sessionId\":\"9f8a7c\",\"location\":\"checkNewDay\",\"message\":\"day_check\",\"data\":{\"lastDay\":%d,\"currentDay\":%d,\"reset\":%d},\"timestamp\":%lu,\"hypothesisId\":\"H1\"}\n", lastDay, currentDay, didReset, (unsigned long)millis());
    // #endregion
    if (lastDay != currentDay && lastDay != -1) {
        // Nouveau jour détecté - réinitialiser les flags
        Serial.println(F("[FeedingSchedule] Nouveau jour détecté - réinitialisation flags"));
        _config.resetBouffeFlags();
        _config.setLastJourBouf(currentDay);
        _config.saveBouffeFlags();
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
    
    // Désactiver le rattrapage pendant la période de boot (évite nourrissage au démarrage)
    const bool catchUpAllowed = (uptimeMs >= FEEDING_BOOT_GRACE_MS);

    // Vérifier chaque horaire (fenêtre heure + rattrapage limité si réveil après le créneau)
    const bool matinOk = _config.getBouffeMatinOk();
    const bool midiOk = _config.getBouffeMidiOk();
    const bool soirOk = _config.getBouffeSoirOk();
    bool shouldFeedMorning = (shouldFeedNow(hour, minute, morningHour) || (catchUpAllowed && shouldCatchUpFeed(hour, morningHour))) && !matinOk;
    bool shouldFeedNoon = (shouldFeedNow(hour, minute, noonHour) || (catchUpAllowed && shouldCatchUpFeed(hour, noonHour))) && !midiOk;
    bool shouldFeedEvening = (shouldFeedNow(hour, minute, eveningHour) || (catchUpAllowed && shouldCatchUpFeed(hour, eveningHour))) && !soirOk;
    // #region agent log
    const int triggering = (shouldFeedMorning || shouldFeedNoon || shouldFeedEvening) ? 1 : 0;
    Serial.printf("{\"sessionId\":\"9f8a7c\",\"location\":\"checkAndFeed\",\"message\":\"feed_eval\",\"data\":{\"hour\":%d,\"min\":%d,\"dayOfYear\":%d,\"matinOk\":%d,\"midiOk\":%d,\"soirOk\":%d,\"shouldM\":%d,\"shouldMi\":%d,\"shouldS\":%d,\"triggering\":%d},\"timestamp\":%lu,\"hypothesisId\":\"H2,H3,H4\"}\n", hour, minute, dayOfYear, matinOk ? 1 : 0, midiOk ? 1 : 0, soirOk ? 1 : 0, shouldFeedMorning ? 1 : 0, shouldFeedNoon ? 1 : 0, shouldFeedEvening ? 1 : 0, triggering, (unsigned long)millis());
    // #endregion
    if (shouldFeedMorning) {
        Serial.printf("[FeedingSchedule] 🍽️ Nourrissage automatique MATIN (%02d:%02d)\n", hour, minute);
        performFeeding(bigDuration, smallDuration, emailAddr, mailNotif, mailBlinkCallback, feedingStartCallback, feedingCompleteCallback);
        _config.setBouffeMatinOk(true);
        _config.saveBouffeFlags();
        sendFeedingEmail("Bouffe matin", bigDuration, smallDuration, emailAddr, mailNotif);
    } else if (shouldFeedNoon) {
        Serial.printf("[FeedingSchedule] 🍽️ Nourrissage automatique MIDI (%02d:%02d)\n", hour, minute);
        performFeeding(bigDuration, smallDuration, emailAddr, mailNotif, mailBlinkCallback, feedingStartCallback, feedingCompleteCallback);
        _config.setBouffeMidiOk(true);
        _config.saveBouffeFlags();
        sendFeedingEmail("Bouffe midi", bigDuration, smallDuration, emailAddr, mailNotif);
    } else if (shouldFeedEvening) {
        Serial.printf("[FeedingSchedule] 🍽️ Nourrissage automatique SOIR (%02d:%02d)\n", hour, minute);
        performFeeding(bigDuration, smallDuration, emailAddr, mailNotif, mailBlinkCallback, feedingStartCallback, feedingCompleteCallback);
        _config.setBouffeSoirOk(true);
        _config.saveBouffeFlags();
        sendFeedingEmail("Bouffe soir", bigDuration, smallDuration, emailAddr, mailNotif);
    }
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

void AutomatismFeedingSchedule::performFeeding(uint16_t bigDuration, uint16_t smallDuration,
                                              const char* emailAddr, bool mailNotif,
                                              std::function<void()> mailBlinkCallback,
                                              std::function<void(const char*)> feedingStartCallback,
                                              std::function<void()> feedingCompleteCallback) {
    // Déclencher le clignotement mail si callback fourni
    if (mailBlinkCallback) {
        mailBlinkCallback();
    }
    
    // Notifier le début du nourrissage (phase "Gros")
    if (feedingStartCallback) {
        feedingStartCallback("Gros");
    }
    
    // Nourrissage séquentiel (gros puis petits avec délai de 2 secondes)
    const uint16_t delayBetweenSec = 2;
    _acts.feedSequential(bigDuration, smallDuration, delayBetweenSec);
    
    Serial.println(F("[FeedingSchedule] Automatic feeding triggered"));
    
    // Notifier la fin du nourrissage pour synchroniser avec le serveur
    if (feedingCompleteCallback) {
        feedingCompleteCallback();
    }
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

AutomatismFeedingSchedule::Status AutomatismFeedingSchedule::getStatus() const {
    Status s;
    s.morningDone = _config.getBouffeMatinOk();
    s.noonDone = _config.getBouffeMidiOk();
    s.eveningDone = _config.getBouffeSoirOk();
    s.lastDay = _config.getLastJourBouf();
    return s;
}
