#include "pgl_counter.h"

#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "pgl_log.h"

void PglCounter::begin() {
  load();
}

void PglCounter::addEvent(const PglStoredEvent& event) {
  totalCount_ += event.countDelta;
  todayCount_ += event.countDelta;

  if (size_ < MAX_EVENTS) {
    queue_[(head_ + size_) % MAX_EVENTS] = event;
    size_++;
  } else {
    // FIFO plein: on remplace l'ancien plus vieux pour rester robuste.
    queue_[head_] = event;
    head_ = (head_ + 1) % MAX_EVENTS;
  }

  // Persistance lazy : on marque "sale" et on n'écrit que tous les
  // PGL_PERSIST_EVERY_EVENTS événements (ou file pleine). flushIfDue()
  // assure l'écriture au plus tard PGL_PERSIST_MAX_DELAY_MS après le
  // premier événement non persisté, et flush() est appelé avant le sleep.
  if (!dirty_) {
    dirty_ = true;
    firstDirtyMs_ = millis();
  }
  eventsSincePersist_++;
  if (eventsSincePersist_ >= PGL_PERSIST_EVERY_EVENTS || size_ >= MAX_EVENTS) {
    persist();
  }
}

size_t PglCounter::peekBatch(PglStoredEvent* out, size_t maxItems) const {
  if (!out || maxItems == 0) return 0;
  const size_t n = (size_ < maxItems) ? size_ : maxItems;
  for (size_t i = 0; i < n; ++i) {
    out[i] = queue_[(head_ + i) % MAX_EVENTS];
  }
  return n;
}

void PglCounter::popBatch(size_t count) {
  if (count == 0 || size_ == 0) return;
  const size_t n = (count > size_) ? size_ : count;
  head_ = static_cast<uint16_t>((head_ + n) % MAX_EVENTS);
  size_ = static_cast<uint16_t>(size_ - n);
  persist();
}

uint32_t PglCounter::getTotalCount() const { return totalCount_; }
uint32_t PglCounter::getTodayCount() const { return todayCount_; }
uint16_t PglCounter::getPendingCount() const { return size_; }

void PglCounter::flush() {
  if (dirty_) persist();
}

void PglCounter::flushIfDue() {
  if (!dirty_) return;
  if ((millis() - firstDirtyMs_) >= PGL_PERSIST_MAX_DELAY_MS) {
    persist();
  }
}

void PglCounter::resetDailyIfNeeded(uint32_t nowEpoch) {
  const uint16_t nowKey = dayKeyFromEpoch(nowEpoch);
  if (nowKey == 0) return;
  if (lastDayKey_ == 0) {
    lastDayKey_ = nowKey;
    persist();
    return;
  }
  if (nowKey != lastDayKey_) {
    todayCount_ = 0;
    lastDayKey_ = nowKey;
    persist();
  }
}

void PglCounter::load() {
  prefs_.begin("pglcnt", false);
  totalCount_ = prefs_.getULong("total", 0);
  todayCount_ = prefs_.getULong("today", 0);
  lastDayKey_ = prefs_.getUShort("day", 0);
  head_ = prefs_.getUShort("head", 0);
  size_ = prefs_.getUShort("size", 0);

  const size_t bytes = prefs_.getBytesLength("queue");
  const size_t expected = sizeof(queue_);
  if (bytes == expected) {
    prefs_.getBytes("queue", queue_, expected);
  } else {
    memset(queue_, 0, expected);
    head_ = 0;
    size_ = 0;
  }
  prefs_.end();

  if (head_ >= MAX_EVENTS) head_ = 0;
  if (size_ > MAX_EVENTS) size_ = MAX_EVENTS;

  PGL_LOG("NVS: total=%lu today=%lu pending=%u jour=%u",
          static_cast<unsigned long>(totalCount_),
          static_cast<unsigned long>(todayCount_),
          size_, lastDayKey_);
  if (bytes != expected) {
    PGL_LOG_V("NVS: file queue absent ou taille invalide, FIFO vide");
  }
}

void PglCounter::persist() {
  Preferences p;
  p.begin("pglcnt", false);
  p.putULong("total", totalCount_);
  p.putULong("today", todayCount_);
  p.putUShort("day", lastDayKey_);
  p.putUShort("head", head_);
  p.putUShort("size", size_);
  p.putBytes("queue", queue_, sizeof(queue_));
  p.end();
  dirty_ = false;
  eventsSincePersist_ = 0;
}

uint16_t PglCounter::dayKeyFromEpoch(uint32_t epoch) const {
  if (epoch == 0) return 0;
  time_t t = static_cast<time_t>(epoch);
  struct tm tmNow = {};
  if (!localtime_r(&t, &tmNow)) return 0;
  const uint16_t y = static_cast<uint16_t>((tmNow.tm_year + 1900) % 100);
  const uint16_t d = static_cast<uint16_t>(tmNow.tm_yday + 1);
  return static_cast<uint16_t>((y * 400) + d);
}
