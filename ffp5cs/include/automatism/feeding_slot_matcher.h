#pragma once

#include <cstdint>

namespace FeedingSlotMatcher {

struct SlotMatches {
    bool morning;
    bool noon;
    bool evening;

    bool any() const {
        return morning || noon || evening;
    }
};

inline bool isInFeedingWindow(int currentHour, uint8_t scheduleHour) {
    const int scheduled = static_cast<int>(scheduleHour);
    const int catchUp = (scheduled + 1) % 24;
    return currentHour == scheduled || currentHour == catchUp;
}

inline SlotMatches slotsForCurrentWindow(int currentHour,
                                         uint8_t morningHour,
                                         uint8_t noonHour,
                                         uint8_t eveningHour) {
    return {
        isInFeedingWindow(currentHour, morningHour),
        isInFeedingWindow(currentHour, noonHour),
        isInFeedingWindow(currentHour, eveningHour)
    };
}

/// Créneau définitivement manqué : la fenêtre (H) et le rattrapage (H+1) sont passés.
/// Pas de gestion du wrap minuit : un créneau à 23h a son rattrapage à 0h le jour
/// suivant (après reset des flags), il n'est jamais déclaré manqué le même jour.
inline bool isSlotMissed(int currentHour, uint8_t scheduleHour) {
    return currentHour > static_cast<int>(scheduleHour) + 1;
}

/// Config valide : heures dans [0..23] et strictement croissantes (matin < midi < soir).
/// Des heures égales provoqueraient un seul repas marquant plusieurs créneaux.
inline bool isScheduleConfigValid(uint8_t morningHour, uint8_t noonHour, uint8_t eveningHour) {
    return morningHour <= 23 && noonHour <= 23 && eveningHour <= 23 &&
           morningHour < noonHour && noonHour < eveningHour;
}

}  // namespace FeedingSlotMatcher
