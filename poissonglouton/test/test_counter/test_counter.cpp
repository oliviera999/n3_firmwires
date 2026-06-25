// -----------------------------------------------------------------------------
// Tests natifs (hote) de PglCounter — logique de comptage / sync offline.
//
// On compile directement pgl_counter.cpp ET pgl_event_journal.cpp contre les
// stubs hote (Arduino/Preferences/SD). La NVS et la SD sont mockees en memoire
// (PglPrefsMock / SDMock) pour exercer les round-trips load/persist et le mode
// journal SD sans materiel.
//
// Couverture :
//   - helpers prives (via PglCounterTestAccess, friend sous UNIT_TEST) :
//     dayKeyFromEpoch, countAckedInBatch, sumEventDeltas, sumTodayDeltas
//   - addEvent : incrément total/today, attribution eventId monotone, rollover
//     de jour (today remis a 0)
//   - mode NVS : popBatch / popBatchByAckId (FIFO + syncedLifetimeTotal)
//   - mode journal SD : addEvent ecrit sur SD, getTotalCount monotone, sync via
//     commitJournalAck + reconciliation (total = synced + pending, jamais < )
// -----------------------------------------------------------------------------

#include <unity.h>

#include <cstdint>
#include <cstdlib>
#include <ctime>

#include <Preferences.h>
#include <SD.h>

// Backend SD storage : adosse au mock SD en memoire.
bool pglSdStorageBegin() { return SDMock::g_mounted; }
bool pglSdStorageIsMounted() { return SDMock::g_mounted; }

#include "../../src/pgl_counter.cpp"
#include "../../src/pgl_event_journal.cpp"

// Accesseur des helpers prives de PglCounter.
struct PglCounterTestAccess {
  static uint16_t dayKey(const PglCounter& c, uint32_t epoch) {
    return c.dayKeyFromEpoch(epoch);
  }
  static size_t countAcked(const PglCounter& c, uint32_t lastAckedId,
                           const PglStoredEvent* batch, size_t n) {
    return c.countAckedInBatch(lastAckedId, batch, n);
  }
  static uint32_t sumDeltas(const PglCounter& c, const PglStoredEvent* ev,
                            size_t n) {
    return c.sumEventDeltas(ev, n);
  }
  static uint32_t sumToday(const PglCounter& c, const PglStoredEvent* ev,
                           size_t n, uint16_t dayKey) {
    return c.sumTodayDeltas(ev, n, dayKey);
  }
};

// epochs valides (>= 1700000000) sur deux jours UTC distincts.
// 1700000000 = 2023-11-14 22:13:20 UTC.
static constexpr uint32_t EPOCH_DAY_A = 1700013600u;       // 2023-11-15 02:00 UTC
static constexpr uint32_t EPOCH_DAY_A_LATER = 1700049600u; // 2023-11-15 12:00 UTC (meme jour)
static constexpr uint32_t EPOCH_DAY_B = 1700100000u;       // 2023-11-16 02:00 UTC (j+1)

static PglStoredEvent mkEvent(uint16_t delta, uint32_t epoch, uint32_t eventId = 0) {
  PglStoredEvent e{};
  e.epoch = epoch;
  e.countDelta = delta;
  e.sensorMode = 1;
  e.tandemValidated = 0;
  e.batteryMilliVolt = 3700;
  e.rssi = -60;
  e.eventId = eventId;
  return e;
}

void setUp(void) {
  PglPrefsMock::reset();
  SDMock::reset();
  SDMock::g_mounted = false;  // par defaut : mode NVS-seul
  pglMockSetMillis(0);
}

void tearDown(void) {}

// ============================================================================
// Helpers purs
// ============================================================================

void test_daykey_invalid_epoch_zero() {
  PglCounter c;
  TEST_ASSERT_EQUAL_UINT16(0, PglCounterTestAccess::dayKey(c, 0));
}

void test_daykey_invalid_epoch_pre_2023() {
  PglCounter c;
  // < 1700000000 => invalide => 0
  TEST_ASSERT_EQUAL_UINT16(0, PglCounterTestAccess::dayKey(c, 1699999999u));
}

void test_daykey_valid_epoch_nonzero_and_stable() {
  PglCounter c;
  const uint16_t k1 = PglCounterTestAccess::dayKey(c, EPOCH_DAY_A);
  const uint16_t k2 = PglCounterTestAccess::dayKey(c, EPOCH_DAY_A_LATER);
  TEST_ASSERT_NOT_EQUAL(0, k1);
  TEST_ASSERT_EQUAL_UINT16(k1, k2);  // meme jour => meme cle
}

void test_daykey_changes_across_days() {
  PglCounter c;
  const uint16_t ka = PglCounterTestAccess::dayKey(c, EPOCH_DAY_A);
  const uint16_t kb = PglCounterTestAccess::dayKey(c, EPOCH_DAY_B);
  TEST_ASSERT_NOT_EQUAL(ka, kb);
}

void test_sum_event_deltas() {
  PglCounter c;
  PglStoredEvent ev[3] = {mkEvent(2, EPOCH_DAY_A), mkEvent(3, EPOCH_DAY_A),
                          mkEvent(5, EPOCH_DAY_B)};
  TEST_ASSERT_EQUAL_UINT32(10, PglCounterTestAccess::sumDeltas(c, ev, 3));
  TEST_ASSERT_EQUAL_UINT32(0, PglCounterTestAccess::sumDeltas(c, nullptr, 3));
  TEST_ASSERT_EQUAL_UINT32(0, PglCounterTestAccess::sumDeltas(c, ev, 0));
}

void test_sum_today_deltas_filters_by_day() {
  PglCounter c;
  const uint16_t keyA = PglCounterTestAccess::dayKey(c, EPOCH_DAY_A);
  PglStoredEvent ev[3] = {mkEvent(2, EPOCH_DAY_A), mkEvent(3, EPOCH_DAY_A_LATER),
                          mkEvent(5, EPOCH_DAY_B)};
  // seuls les deux evenements du jour A comptent (2 + 3)
  TEST_ASSERT_EQUAL_UINT32(5, PglCounterTestAccess::sumToday(c, ev, 3, keyA));
  // dayKey == 0 => 0 (epoch invalide / jour inconnu)
  TEST_ASSERT_EQUAL_UINT32(0, PglCounterTestAccess::sumToday(c, ev, 3, 0));
}

// --- countAckedInBatch ------------------------------------------------------

void test_count_acked_empty_batch() {
  PglCounter c;
  TEST_ASSERT_EQUAL_UINT32(0, PglCounterTestAccess::countAcked(c, 5, nullptr, 0));
}

void test_count_acked_eventid_zero_always_acked() {
  // eventId == 0 (jamais assigne) est traite comme deja acquitte (purge).
  PglCounter c;
  PglStoredEvent batch[3] = {mkEvent(1, EPOCH_DAY_A, 0), mkEvent(1, EPOCH_DAY_A, 0),
                             mkEvent(1, EPOCH_DAY_A, 0)};
  TEST_ASSERT_EQUAL_UINT32(3, PglCounterTestAccess::countAcked(c, 0, batch, 3));
}

void test_count_acked_partial() {
  PglCounter c;
  // ids 10,11,12 ; ack jusqu'a 11 => 2 acquittes (prefixe contigu)
  PglStoredEvent batch[3] = {mkEvent(1, EPOCH_DAY_A, 10), mkEvent(1, EPOCH_DAY_A, 11),
                             mkEvent(1, EPOCH_DAY_A, 12)};
  TEST_ASSERT_EQUAL_UINT32(2, PglCounterTestAccess::countAcked(c, 11, batch, 3));
}

void test_count_acked_full() {
  PglCounter c;
  PglStoredEvent batch[3] = {mkEvent(1, EPOCH_DAY_A, 10), mkEvent(1, EPOCH_DAY_A, 11),
                             mkEvent(1, EPOCH_DAY_A, 12)};
  TEST_ASSERT_EQUAL_UINT32(3, PglCounterTestAccess::countAcked(c, 99, batch, 3));
}

void test_count_acked_not_found() {
  PglCounter c;
  // ack id=5 introuvable, premier id=10 > 5 => 0 acquitte (rien a purger)
  PglStoredEvent batch[3] = {mkEvent(1, EPOCH_DAY_A, 10), mkEvent(1, EPOCH_DAY_A, 11),
                             mkEvent(1, EPOCH_DAY_A, 12)};
  TEST_ASSERT_EQUAL_UINT32(0, PglCounterTestAccess::countAcked(c, 5, batch, 3));
}

// ============================================================================
// addEvent — mode NVS-seul
// ============================================================================

void test_add_event_increments_totals() {
  PglCounter c;
  c.begin(nullptr);
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(2, EPOCH_DAY_A));
  TEST_ASSERT_EQUAL_UINT32(3, c.getTotalCount());
  TEST_ASSERT_EQUAL_UINT32(3, c.getTodayCount());
  TEST_ASSERT_EQUAL_UINT16(2, c.getNvsPendingCount());
}

void test_add_event_assigns_monotonic_event_id() {
  PglCounter c;
  c.begin(nullptr);
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  PglStoredEvent peek[3] = {};
  const size_t n = c.peekBatch(peek, 3);
  TEST_ASSERT_EQUAL_UINT32(3, n);
  TEST_ASSERT_NOT_EQUAL(0, peek[0].eventId);
  TEST_ASSERT_TRUE(peek[1].eventId > peek[0].eventId);
  TEST_ASSERT_TRUE(peek[2].eventId > peek[1].eventId);
}

void test_add_event_day_rollover_resets_today() {
  PglCounter c;
  c.begin(nullptr);
  c.addEvent(mkEvent(4, EPOCH_DAY_A));
  TEST_ASSERT_EQUAL_UINT32(4, c.getTodayCount());
  TEST_ASSERT_EQUAL_UINT32(4, c.getTotalCount());

  // evenement d'un autre jour => today repart de ce delta, total cumule
  c.addEvent(mkEvent(3, EPOCH_DAY_B));
  TEST_ASSERT_EQUAL_UINT32(3, c.getTodayCount());
  TEST_ASSERT_EQUAL_UINT32(7, c.getTotalCount());
}

// ============================================================================
// popBatch / popBatchByAckId — FIFO NVS + syncedLifetimeTotal
// ============================================================================

void test_pop_batch_advances_fifo() {
  PglCounter c;
  c.begin(nullptr);
  for (int i = 0; i < 5; ++i) c.addEvent(mkEvent(1, EPOCH_DAY_A));
  TEST_ASSERT_EQUAL_UINT16(5, c.getNvsPendingCount());

  c.popBatch(2);
  TEST_ASSERT_EQUAL_UINT16(3, c.getNvsPendingCount());

  // l'ordre FIFO est preserve : les 3 restants sont les 3 derniers ajoutes
  PglStoredEvent peek[8] = {};
  const size_t n = c.peekBatch(peek, 8);
  TEST_ASSERT_EQUAL_UINT32(3, n);
}

void test_pop_batch_total_never_decreases() {
  PglCounter c;
  c.begin(nullptr);
  for (int i = 0; i < 5; ++i) c.addEvent(mkEvent(1, EPOCH_DAY_A));
  const uint32_t before = c.getTotalCount();
  c.popBatch(5);  // tout acquitte => syncedLifetimeTotal += 5, FIFO vide
  TEST_ASSERT_EQUAL_UINT16(0, c.getNvsPendingCount());
  // total ne baisse jamais (reconciliation monotone)
  TEST_ASSERT_EQUAL_UINT32(before, c.getTotalCount());
}

void test_pop_batch_by_ack_id_partial() {
  PglCounter c;
  c.begin(nullptr);
  for (int i = 0; i < 4; ++i) c.addEvent(mkEvent(1, EPOCH_DAY_A));

  PglStoredEvent batch[4] = {};
  const size_t n = c.peekBatch(batch, 4);
  TEST_ASSERT_EQUAL_UINT32(4, n);

  // acquitte jusqu'au 2e id du lot => 2 popped
  const uint32_t ackId = batch[1].eventId;
  c.popBatchByAckId(ackId, batch, n);
  TEST_ASSERT_EQUAL_UINT16(2, c.getNvsPendingCount());
}

void test_pop_batch_by_ack_id_unknown_keeps_queue() {
  PglCounter c;
  c.begin(nullptr);
  for (int i = 0; i < 3; ++i) c.addEvent(mkEvent(1, EPOCH_DAY_A));  // ids 1,2,3

  // Purge le premier (id=1) pour que la tete de file soit l'id 2.
  c.popBatch(1);
  TEST_ASSERT_EQUAL_UINT16(2, c.getNvsPendingCount());

  PglStoredEvent batch[2] = {};
  const size_t n = c.peekBatch(batch, 2);  // ids 2,3
  TEST_ASSERT_EQUAL_UINT32(2, n);
  TEST_ASSERT_TRUE(batch[0].eventId > 1);

  // ack id=1 : > 0 mais < premier id en file (2) => introuvable => conservation
  c.popBatchByAckId(1, batch, n);
  TEST_ASSERT_EQUAL_UINT16(2, c.getNvsPendingCount());
}

// ============================================================================
// Persistance NVS — round-trip load/persist
// ============================================================================

void test_persistence_roundtrip_across_instances() {
  {
    PglCounter c;
    c.begin(nullptr);
    c.addEvent(mkEvent(2, EPOCH_DAY_A));
    c.addEvent(mkEvent(3, EPOCH_DAY_A));
    c.flush();  // force la persistance NVS
    TEST_ASSERT_EQUAL_UINT32(5, c.getTotalCount());
  }
  // Nouvelle instance => relit la NVS mockee (qui survit comme la vraie flash).
  PglCounter c2;
  c2.begin(nullptr);
  TEST_ASSERT_EQUAL_UINT32(5, c2.getTotalCount());
  TEST_ASSERT_EQUAL_UINT16(2, c2.getNvsPendingCount());
}

// ============================================================================
// Mode journal SD
// ============================================================================

void test_sd_mode_add_event_goes_to_journal_not_nvs() {
  SDMock::g_mounted = true;
  PglEventJournal journal;
  journal.begin();

  PglCounter c;
  c.begin(&journal);
  TEST_ASSERT_TRUE(c.isSdMode());

  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  // En mode SD, la FIFO NVS reste vide : les events partent sur le journal.
  TEST_ASSERT_EQUAL_UINT16(0, c.getNvsPendingCount());
  TEST_ASSERT_EQUAL_UINT32(2, c.getJournalPendingCount());
  TEST_ASSERT_EQUAL_UINT32(2, c.getTotalCount());
}

void test_sd_mode_commit_ack_advances_and_total_monotonic() {
  SDMock::g_mounted = true;
  PglEventJournal journal;
  journal.begin();

  PglCounter c;
  c.begin(&journal);
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  c.addEvent(mkEvent(1, EPOCH_DAY_A));
  const uint32_t totalBefore = c.getTotalCount();
  TEST_ASSERT_EQUAL_UINT32(3, totalBefore);

  // Lit un lot du journal puis acquitte jusqu'au 2e id => 2 confirmes.
  PglStoredEvent out[8] = {};
  uint32_t firstId = 0, lastId = 0;
  const size_t n = c.peekJournalBatch(out, 8, firstId, lastId);
  TEST_ASSERT_EQUAL_UINT32(3, n);

  c.commitJournalAck(out[1].eventId, out, n);
  // 1 record reste en attente, total inchange (monotone).
  TEST_ASSERT_EQUAL_UINT32(1, c.getJournalPendingCount());
  TEST_ASSERT_EQUAL_UINT32(totalBefore, c.getTotalCount());
}

void test_sd_mode_reconcile_recovers_total_from_journal() {
  SDMock::g_mounted = true;
  // Premiere "session" : on ecrit 3 events sur le journal SD mais on simule un
  // crash AVANT la persistance des totaux (on n'appelle pas flush()).
  {
    PglEventJournal journal;
    journal.begin();
    PglCounter c;
    c.begin(&journal);
    c.addEvent(mkEvent(2, EPOCH_DAY_A));
    c.addEvent(mkEvent(2, EPOCH_DAY_A));
    c.addEvent(mkEvent(2, EPOCH_DAY_A));
    TEST_ASSERT_EQUAL_UINT32(6, c.getTotalCount());
  }
  // Efface les totaux NVS pour simuler une NVS en retard sur le journal SD
  // (le journal, lui, persiste ses offsets). reconcileTotals() au begin() doit
  // rattraper : total = synced(0) + pending journal (6).
  PglPrefsMock::store()["pglcnt"].clear();

  PglEventJournal journal2;
  journal2.begin();
  PglCounter c2;
  c2.begin(&journal2);
  TEST_ASSERT_EQUAL_UINT32(3, c2.getJournalPendingCount());
  TEST_ASSERT_EQUAL_UINT32(6, c2.getTotalCount());  // reconcilie depuis le journal
}

int main(void) {
  // dayKeyFromEpoch utilise localtime_r : on fige le fuseau a UTC pour des
  // cles de jour deterministes, independantes de l'environnement CI.
  setenv("TZ", "UTC", 1);
  tzset();

  UNITY_BEGIN();

  // Helpers purs
  RUN_TEST(test_daykey_invalid_epoch_zero);
  RUN_TEST(test_daykey_invalid_epoch_pre_2023);
  RUN_TEST(test_daykey_valid_epoch_nonzero_and_stable);
  RUN_TEST(test_daykey_changes_across_days);
  RUN_TEST(test_sum_event_deltas);
  RUN_TEST(test_sum_today_deltas_filters_by_day);
  RUN_TEST(test_count_acked_empty_batch);
  RUN_TEST(test_count_acked_eventid_zero_always_acked);
  RUN_TEST(test_count_acked_partial);
  RUN_TEST(test_count_acked_full);
  RUN_TEST(test_count_acked_not_found);

  // addEvent
  RUN_TEST(test_add_event_increments_totals);
  RUN_TEST(test_add_event_assigns_monotonic_event_id);
  RUN_TEST(test_add_event_day_rollover_resets_today);

  // popBatch / popBatchByAckId
  RUN_TEST(test_pop_batch_advances_fifo);
  RUN_TEST(test_pop_batch_total_never_decreases);
  RUN_TEST(test_pop_batch_by_ack_id_partial);
  RUN_TEST(test_pop_batch_by_ack_id_unknown_keeps_queue);

  // Persistance NVS
  RUN_TEST(test_persistence_roundtrip_across_instances);

  // Mode journal SD
  RUN_TEST(test_sd_mode_add_event_goes_to_journal_not_nvs);
  RUN_TEST(test_sd_mode_commit_ack_advances_and_total_monotonic);
  RUN_TEST(test_sd_mode_reconcile_recovers_total_from_journal);

  return UNITY_END();
}
