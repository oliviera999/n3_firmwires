#include "pgl_counter.h"

#include <Arduino.h>
#include <time.h>

#include "config.h"
#include "pgl_log.h"

void PglCounter::begin(PglEventJournal* journal) {
  journal_ = journal;
  load();
  reconcileTotals(0);
  if (journal_ && journal_->canRead()) {
    PGL_LOG("Compteur: journal SD pending~%lu NVS=%u total=%lu ecriture=%s",
            static_cast<unsigned long>(journal_->pendingCount()),
            size_,
            static_cast<unsigned long>(totalCount_),
            journal_->isSdOk() ? "OK" : "HS");
  } else {
    PGL_LOG("Compteur: mode NVS-seul (SD absente ou journal vide, total=%lu pending=%u)",
            static_cast<unsigned long>(totalCount_),
            size_);
  }
}

uint32_t PglCounter::nextEventId() {
  const uint32_t id = nextEventId_++;
  totalsDirty_ = true;
  return id;
}

uint32_t PglCounter::sumEventDeltas(const PglStoredEvent* events, size_t count) const {
  if (!events) return 0;
  uint32_t sum = 0;
  for (size_t i = 0; i < count; ++i) {
    sum += events[i].countDelta;
  }
  return sum;
}

uint32_t PglCounter::sumTodayDeltas(const PglStoredEvent* events, size_t count,
                                    uint16_t dayKey) const {
  if (!events || dayKey == 0) return 0;
  uint32_t sum = 0;
  for (size_t i = 0; i < count; ++i) {
    if (dayKeyFromEpoch(events[i].epoch) == dayKey) {
      sum += events[i].countDelta;
    }
  }
  return sum;
}

size_t PglCounter::countAckedInBatch(uint32_t lastAckedId,
                                     const PglStoredEvent* batch,
                                     size_t batchCount) const {
  if (!batch || batchCount == 0) return 0;
  size_t acked = 0;
  for (size_t i = 0; i < batchCount; ++i) {
    if (batch[i].eventId == 0 || batch[i].eventId <= lastAckedId) {
      acked = i + 1;
    } else {
      break;
    }
  }
  return acked;
}

void PglCounter::persistTotals() {
  Preferences p;
  p.begin("pglcnt", false);
  p.putULong("total", totalCount_);
  p.putULong("today", todayCount_);
  p.putUShort("day", lastDayKey_);
  p.putULong("nxtid", nextEventId_);
  p.putULong("synctot", syncedLifetimeTotal_);
  p.putULong("synctdy", syncedTodayTotal_);
  p.end();
  totalsDirty_ = false;
}

void PglCounter::reconcileTotals(uint32_t nowEpoch) {
  if (journal_ && journal_->canRead()) {
    if (syncedLifetimeTotal_ == 0 && journal_->getAckOffset() > 0) {
      syncedLifetimeTotal_ =
          journal_->sumDeltasInRange(0, journal_->getAckOffset());
      if (lastDayKey_ != 0) {
        syncedTodayTotal_ = journal_->sumDeltasInRange(
            0, journal_->getAckOffset(), lastDayKey_);
      }
      PGL_LOG("Compteur: migration syncsum=%lu synctdy=%lu",
              static_cast<unsigned long>(syncedLifetimeTotal_),
              static_cast<unsigned long>(syncedTodayTotal_));
    }

    const uint32_t pendingSum = journal_->sumDeltasInRange(
        journal_->getAckOffset(), journal_->getWriteOffset());
    const uint32_t queuedSum = sumEventDeltas(queue_, size_);
    const uint32_t reconciledTotal = syncedLifetimeTotal_ + pendingSum + queuedSum;
    if (reconciledTotal > totalCount_) {
      PGL_LOG("Compteur: reconcile total %lu -> %lu (sync=%lu pending=%lu)",
              static_cast<unsigned long>(totalCount_),
              static_cast<unsigned long>(reconciledTotal),
              static_cast<unsigned long>(syncedLifetimeTotal_),
              static_cast<unsigned long>(pendingSum + queuedSum));
      totalCount_ = reconciledTotal;
    }

    uint16_t dayKey = lastDayKey_;
    if (dayKey == 0 && nowEpoch > 0) {
      dayKey = dayKeyFromEpoch(nowEpoch);
    }
    if (dayKey != 0) {
      const uint32_t pendingToday = journal_->sumDeltasInRange(
          journal_->getAckOffset(), journal_->getWriteOffset(), dayKey);
      const uint32_t queuedToday = sumTodayDeltas(queue_, size_, dayKey);
      const uint32_t reconciledToday = syncedTodayTotal_ + pendingToday + queuedToday;
      if (reconciledToday > todayCount_) {
        PGL_LOG("Compteur: reconcile today %lu -> %lu",
                static_cast<unsigned long>(todayCount_),
                static_cast<unsigned long>(reconciledToday));
        todayCount_ = reconciledToday;
        lastDayKey_ = dayKey;
      }
    }
    persistTotals();
    return;
  }

  const uint32_t queuedSum = sumEventDeltas(queue_, size_);
  const uint32_t reconciledTotal = syncedLifetimeTotal_ + queuedSum;
  if (reconciledTotal > totalCount_) {
    PGL_LOG("Compteur: reconcile NVS total %lu -> %lu",
            static_cast<unsigned long>(totalCount_),
            static_cast<unsigned long>(reconciledTotal));
    totalCount_ = reconciledTotal;
    persistTotals();
  }
}

void PglCounter::addEvent(const PglStoredEvent& eventIn) {
  const uint16_t eventDayKey = dayKeyFromEpoch(eventIn.epoch);
  if (lastDayKey_ != 0 && eventDayKey != 0 && eventDayKey != lastDayKey_) {
    todayCount_ = 0;
    syncedTodayTotal_ = 0;
    lastDayKey_ = eventDayKey;
  } else if (lastDayKey_ == 0 && eventDayKey != 0) {
    lastDayKey_ = eventDayKey;
  }

  totalCount_ += eventIn.countDelta;
  todayCount_ += eventIn.countDelta;

  PglStoredEvent event = eventIn;
  if (event.eventId == 0) {
    event.eventId = nextEventId();
  } else {
    totalsDirty_ = true;
  }

  if (journal_ && journal_->isSdOk()) {
    if (!journal_->append(event)) {
      PGL_LOG("Journal SD: echec append — repli NVS FIFO");
    } else {
      totalsDirty_ = true;
      eventsSincePersist_++;
      if (eventsSincePersist_ >= PGL_PERSIST_EVERY_EVENTS) {
        persistTotals();
        eventsSincePersist_ = 0;
      }
      return;
    }
  }

  if (size_ < MAX_EVENTS) {
    queue_[(head_ + size_) % MAX_EVENTS] = event;
    size_++;
  } else {
    PGL_LOG("Compteur: FIFO NVS pleine (%u) — ecrasement du plus ancien",
            static_cast<unsigned int>(MAX_EVENTS));
    queue_[head_] = event;
    head_ = (head_ + 1) % MAX_EVENTS;
  }

  if (!dirty_) {
    dirty_ = true;
    firstDirtyMs_ = millis();
  }
  eventsSincePersist_++;
  totalsDirty_ = true;
  if (eventsSincePersist_ >= PGL_PERSIST_EVERY_EVENTS || size_ >= MAX_EVENTS) {
    persist();
  } else if (totalsDirty_) {
    persistTotals();
    eventsSincePersist_ = 0;
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

  uint32_t lifetimeSum = 0;
  uint32_t todaySum = 0;
  for (size_t i = 0; i < n; ++i) {
    const PglStoredEvent& ev = queue_[(head_ + i) % MAX_EVENTS];
    lifetimeSum += ev.countDelta;
    if (lastDayKey_ != 0 && dayKeyFromEpoch(ev.epoch) == lastDayKey_) {
      todaySum += ev.countDelta;
    }
  }
  syncedLifetimeTotal_ += lifetimeSum;
  syncedTodayTotal_ += todaySum;

  head_ = static_cast<uint16_t>((head_ + n) % MAX_EVENTS);
  size_ = static_cast<uint16_t>(size_ - n);
  persist();
}

void PglCounter::popBatchByAckId(uint32_t lastAckedId, const PglStoredEvent* batch,
                                 size_t batchCount) {
  if (!batch || batchCount == 0 || size_ == 0) return;

  size_t toPop = countAckedInBatch(lastAckedId, batch, batchCount);
  if (toPop == 0 && lastAckedId > 0) {
    PGL_LOG("Compteur: ack id=%lu introuvable dans le lot NVS — conservation",
            static_cast<unsigned long>(lastAckedId));
    return;
  }
  if (toPop == 0) {
    toPop = batchCount;
  }
  popBatch(toPop);
}

size_t PglCounter::peekJournalBatch(PglStoredEvent* out, size_t maxItems,
                                    uint32_t& outFirstId,
                                    uint32_t& outLastId) const {
  if (!journal_ || !journal_->canRead()) return 0;
  return journal_->readPending(out, maxItems, outFirstId, outLastId);
}

void PglCounter::commitJournalAck(uint32_t lastAckedId,
                                  const PglStoredEvent* ackedEvents,
                                  size_t ackedCount) {
  if (!journal_ || !journal_->canRead()) return;

  const size_t ackedUpTo = countAckedInBatch(lastAckedId, ackedEvents, ackedCount);
  if (ackedUpTo == 0) {
    PGL_LOG("Compteur: commitJournalAck ignore — aucun evt <= id=%lu",
            static_cast<unsigned long>(lastAckedId));
    return;
  }

  const uint32_t batchSum = sumEventDeltas(ackedEvents, ackedUpTo);
  const uint32_t batchToday =
      sumTodayDeltas(ackedEvents, ackedUpTo, lastDayKey_);

  syncedLifetimeTotal_ += batchSum;
  syncedTodayTotal_ += batchToday;

  if (!journal_->commitAck(lastAckedId)) {
    syncedLifetimeTotal_ -= batchSum;
    syncedTodayTotal_ -= batchToday;
    PGL_LOG("Compteur: commitJournalAck annule — curseur journal inchange");
    return;
  }

  reconcileTotals(0);
  persist();
}

uint32_t PglCounter::getTotalCount() const { return totalCount_; }
uint32_t PglCounter::getTodayCount() const { return todayCount_; }

uint32_t PglCounter::getJournalPendingCount() const {
  if (!journal_ || !journal_->canRead()) return 0;
  return journal_->pendingCount();
}

uint16_t PglCounter::getNvsPendingCount() const { return size_; }

bool PglCounter::hasJournalPending() const {
  return getJournalPendingCount() > 0;
}

uint16_t PglCounter::getPendingCount() const {
  const uint32_t journalPending = getJournalPendingCount();
  const uint32_t totalPending = journalPending + size_;
  return static_cast<uint16_t>(totalPending > 0xFFFF ? 0xFFFF : totalPending);
}

bool PglCounter::isSdMode() const {
  return journal_ != nullptr && journal_->isSdOk();
}

void PglCounter::flush() {
  if (dirty_) {
    persist();
  } else if (totalsDirty_) {
    persistTotals();
  }
}

void PglCounter::flushIfDue() {
  if (dirty_ && (millis() - firstDirtyMs_) >= PGL_PERSIST_MAX_DELAY_MS) {
    persist();
    return;
  }
  if (totalsDirty_ && eventsSincePersist_ >= PGL_PERSIST_EVERY_EVENTS) {
    persistTotals();
    eventsSincePersist_ = 0;
  }
}

void PglCounter::fixInvalidEpochs(uint32_t nowEpoch) {
  if (nowEpoch < 1700000000) return;

  // 1) FIFO NVS de secours.
  if (size_ > 0) {
    bool changed = false;
    for (uint16_t i = 0; i < size_; ++i) {
      PglStoredEvent& ev = queue_[(head_ + i) % MAX_EVENTS];
      if (ev.epoch < 1700000000) {
        ev.epoch = nowEpoch;
        changed = true;
      }
    }
    if (changed) {
      PGL_LOG("Compteur: horodatage NVS corrige (%u evt) apres sync NTP", size_);
      dirty_ = true;
      persist();
    }
  }

  // 2) Journal SD : records pending ecrits avant la synchro NTP.
  if (journal_) {
    journal_->fixInvalidEpochs(nowEpoch);
  }
}

void PglCounter::resetDailyIfNeeded(uint32_t nowEpoch) {
  const uint16_t nowKey = dayKeyFromEpoch(nowEpoch);
  if (nowKey == 0) return;
  if (lastDayKey_ == 0) {
    lastDayKey_ = nowKey;
    reconcileTotals(nowEpoch);
    persistTotals();
    return;
  }
  if (nowKey != lastDayKey_) {
    todayCount_ = 0;
    syncedTodayTotal_ = 0;
    lastDayKey_ = nowKey;
    reconcileTotals(nowEpoch);
    persistTotals();
    lastReconcileMs_ = millis();
    return;
  }
  // Meme jour : la reconciliation lit tout le journal SD (cher). On la limite
  // a PGL_RECONCILE_INTERVAL_MS quand des evenements sont en attente sur SD,
  // au lieu de scanner a chaque tour de loop(). En mode NVS-seul, le scan est
  // bon marche (pas d'I/O SD), on laisse passer a chaque fois.
  if (journal_ && journal_->canRead()) {
    const uint32_t now = millis();
    if ((now - lastReconcileMs_) < PGL_RECONCILE_INTERVAL_MS) return;
    lastReconcileMs_ = now;
  }
  reconcileTotals(nowEpoch);
}

void PglCounter::load() {
  Preferences p;
  p.begin("pglcnt", true);
  totalCount_ = p.getULong("total", 0);
  todayCount_ = p.getULong("today", 0);
  lastDayKey_ = p.getUShort("day", 0);
  nextEventId_ = p.getULong("nxtid", 1);
  syncedLifetimeTotal_ = p.getULong("synctot", 0);
  syncedTodayTotal_ = p.getULong("synctdy", 0);
  head_ = p.getUShort("head", 0);
  size_ = p.getUShort("size", 0);

  const size_t bytes = p.getBytesLength("queue");
  const size_t expected = sizeof(queue_);
  if (bytes == expected) {
    p.getBytes("queue", queue_, expected);
  } else {
    memset(queue_, 0, expected);
    head_ = 0;
    size_ = 0;
  }
  p.end();

  if (head_ >= MAX_EVENTS) head_ = 0;
  if (size_ > MAX_EVENTS) size_ = MAX_EVENTS;
  if (nextEventId_ == 0) nextEventId_ = 1;

  PGL_LOG("NVS: total=%lu today=%lu sync=%lu pending=%u jour=%u nextId=%lu",
          static_cast<unsigned long>(totalCount_),
          static_cast<unsigned long>(todayCount_),
          static_cast<unsigned long>(syncedLifetimeTotal_),
          size_, lastDayKey_,
          static_cast<unsigned long>(nextEventId_));
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
  p.putULong("nxtid", nextEventId_);
  p.putULong("synctot", syncedLifetimeTotal_);
  p.putULong("synctdy", syncedTodayTotal_);
  p.putUShort("head", head_);
  p.putUShort("size", size_);
  p.putBytes("queue", queue_, sizeof(queue_));
  p.end();
  dirty_ = false;
  totalsDirty_ = false;
  eventsSincePersist_ = 0;
}

uint16_t PglCounter::dayKeyFromEpoch(uint32_t epoch) const {
  if (epoch == 0 || epoch < 1700000000) return 0;
  time_t t = static_cast<time_t>(epoch);
  struct tm tmNow = {};
  if (!localtime_r(&t, &tmNow)) return 0;
  const uint16_t y = static_cast<uint16_t>((tmNow.tm_year + 1900) % 100);
  const uint16_t d = static_cast<uint16_t>(tmNow.tm_yday + 1);
  return static_cast<uint16_t>((y * 400) + d);
}
