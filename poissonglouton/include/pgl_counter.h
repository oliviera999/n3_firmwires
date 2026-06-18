#pragma once



#include <Preferences.h>



#include "pgl_event_journal.h"

#include "pgl_types.h"



class PglCounter {

 public:

  static constexpr size_t MAX_EVENTS = 128;



  /**

   * Initialise le compteur.

   * journal peut être nullptr : dans ce cas, on reste en mode NVS-seul.

   * Si journal est fourni et sdOk, c'est le journal SD qui sert de source

   * de vérité pour les événements en attente.

   */

  void begin(PglEventJournal* journal = nullptr);

  void addEvent(const PglStoredEvent& event);



  // --- Lecture batch (mode SD ou mode NVS selon disponibilité) ---

  size_t peekBatch(PglStoredEvent* out, size_t maxItems) const;

  void popBatch(size_t count);

  void popBatchByAckId(uint32_t lastAckedId, const PglStoredEvent* batch, size_t batchCount);



  // --- Batch journal SD : lecture directe (contourne la NVS FIFO) ---

  size_t peekJournalBatch(PglStoredEvent* out, size_t maxItems,

                          uint32_t& outFirstId, uint32_t& outLastId) const;

  void commitJournalAck(uint32_t lastAckedId, const PglStoredEvent* ackedEvents,

                        size_t ackedCount);



  uint32_t getTotalCount() const;

  uint32_t getTodayCount() const;

  uint16_t getPendingCount() const;

  uint32_t getJournalPendingCount() const;

  uint16_t getNvsPendingCount() const;

  bool hasJournalPending() const;



  /** true si le journal accepte de nouvelles écritures SD. */

  bool isSdMode() const;



  void resetDailyIfNeeded(uint32_t nowEpoch);



  // Persistance lazy NVS (stats agrégées + FIFO de secours)

  void flush();

  void flushIfDue();

  /** Remplace les epochs invalides (< 1700000000) dans la FIFO NVS. */
  void fixInvalidEpochs(uint32_t nowEpoch);

 private:

  void load();

  void persist();

  void persistTotals();

  void reconcileTotals(uint32_t nowEpoch);

  uint16_t dayKeyFromEpoch(uint32_t epoch) const;

  uint32_t nextEventId();

  uint32_t sumEventDeltas(const PglStoredEvent* events, size_t count) const;

  uint32_t sumTodayDeltas(const PglStoredEvent* events, size_t count,

                          uint16_t dayKey) const;

  size_t countAckedInBatch(uint32_t lastAckedId, const PglStoredEvent* batch,

                           size_t batchCount) const;



  uint32_t totalCount_ = 0;

  uint32_t todayCount_ = 0;

  uint32_t syncedLifetimeTotal_ = 0;  // somme des countDelta deja acquittes serveur

  uint32_t syncedTodayTotal_ = 0;     // idem pour le jour courant (lastDayKey_)

  uint16_t lastDayKey_ = 0;

  uint32_t nextEventId_ = 1;  // compteur monotone persisté en NVS



  // FIFO NVS (mode dégradé / SD absente)

  PglStoredEvent queue_[MAX_EVENTS] = {};

  uint16_t head_ = 0;

  uint16_t size_ = 0;



  bool dirty_ = false;

  bool totalsDirty_ = false;

  uint8_t eventsSincePersist_ = 0;

  uint32_t firstDirtyMs_ = 0;



  PglEventJournal* journal_ = nullptr;

};


