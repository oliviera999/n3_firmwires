#pragma once

#include <Preferences.h>

#include "pgl_types.h"

class PglCounter {
 public:
  static constexpr size_t MAX_EVENTS = 128;

  void begin();
  void addEvent(const PglStoredEvent& event);
  size_t peekBatch(PglStoredEvent* out, size_t maxItems) const;
  void popBatch(size_t count);

  uint32_t getTotalCount() const;
  uint32_t getTodayCount() const;
  uint16_t getPendingCount() const;

  void resetDailyIfNeeded(uint32_t nowEpoch);

 private:
  void load();
  void persist() const;
  void persistQueue() const;
  uint16_t dayKeyFromEpoch(uint32_t epoch) const;

  Preferences prefs_;
  uint32_t totalCount_ = 0;
  uint32_t todayCount_ = 0;
  uint16_t lastDayKey_ = 0;

  PglStoredEvent queue_[MAX_EVENTS] = {};
  uint16_t head_ = 0;
  uint16_t size_ = 0;
};
