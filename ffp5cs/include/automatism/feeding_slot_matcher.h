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

}  // namespace FeedingSlotMatcher
