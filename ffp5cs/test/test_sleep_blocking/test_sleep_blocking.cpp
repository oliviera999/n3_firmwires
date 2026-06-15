#include <unity.h>

// Tests de SleepBlocking::decide (automatism/sleep_blocking.h) — décision PURE de blocage
// de la veille extraite de AutomatismSleep::handleBlockingConditions (chantier C4). Chemin
// VEILLE critique (batterie/réveil) -> couverture exhaustive : chaque raison, l'ordre de
// priorité des conditions, les bornes strictes (timeout WS, seuil décompte 300 s, expiration
// web), les transitions « fall-through » (WS timeout / web expiré) et le wrap-around millis()
// (arithmétique 32 bits). Parité ligne à ligne avec l'ancien corps inline.
#include "../../include/automatism/sleep_blocking.h"

using SleepBlocking::Reason;

namespace {
constexpr uint32_t BASE_NOW = 1000000u;   // 1000 s (évite tout underflow d'elapsed)
constexpr uint32_t WEB_TIMEOUT = 60000u;  // = TimingConfig::WEB_ACTIVITY_TIMEOUT_MS

// Inputs « aucune condition bloquante » à l'instant `now`.
SleepBlocking::Inputs baseInputs(uint32_t now = BASE_NOW) {
  SleepBlocking::Inputs in;
  in.nowMs = now;
  in.webActivityTimeoutMs = WEB_TIMEOUT;
  return in;  // tout le reste = défauts (0 / false) -> pas de blocage
}
}  // namespace

void setUp(void) {}
void tearDown(void) {}

// Aucune condition -> Allow (veille permise), état inchangé.
void test_allow_when_nothing(void) {
  SleepBlocking::State st;  // wsBlockStartMs = 0
  auto res = SleepBlocking::decide(st, baseInputs());
  TEST_ASSERT_EQUAL(Reason::Allow, res.reason);
  TEST_ASSERT_TRUE(res.canSleep());
  TEST_ASSERT_FALSE(res.wsTimedOut);
  TEST_ASSERT_FALSE(res.webExpired);
  TEST_ASSERT_FALSE(res.refreshLastWake);
  TEST_ASSERT_EQUAL_UINT32(0, st.wsBlockStartMs);
}

// 1er passage avec clients WS : initialise wsBlockStartMs=now, bloque, elapsed=0.
void test_ws_first_entry(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.wsClients = 1;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WsClients, res.reason);
  TEST_ASSERT_EQUAL_UINT32(BASE_NOW, st.wsBlockStartMs);
  TEST_ASSERT_EQUAL_UINT32(0, res.wsElapsedMs);
  TEST_ASSERT_FALSE(res.wsTimedOut);
}

// Clients WS dans le timeout : elapsed = now - wsBlockStart, état préservé.
void test_ws_within_timeout(void) {
  SleepBlocking::State st; st.wsBlockStartMs = 900000u;  // il y a 100 s
  auto in = baseInputs(); in.wsClients = 2;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WsClients, res.reason);
  TEST_ASSERT_EQUAL_UINT32(100000u, res.wsElapsedMs);
  TEST_ASSERT_EQUAL_UINT32(900000u, st.wsBlockStartMs);  // inchangé
}

// Borne du timeout : exactement 300000 ms écoulées -> NON dépassé (> strict) -> bloqué.
void test_ws_timeout_boundary_not_exceeded(void) {
  SleepBlocking::State st; st.wsBlockStartMs = BASE_NOW - 300000u;
  auto in = baseInputs(); in.wsClients = 1;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WsClients, res.reason);
  TEST_ASSERT_FALSE(res.wsTimedOut);
  TEST_ASSERT_EQUAL_UINT32(300000u, res.wsElapsedMs);
}

// Timeout dépassé (300001 ms) -> wsTimedOut, reset, fall-through jusqu'à Allow.
void test_ws_timeout_exceeded_to_allow(void) {
  SleepBlocking::State st; st.wsBlockStartMs = BASE_NOW - 300001u;
  auto in = baseInputs(); in.wsClients = 1;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::Allow, res.reason);
  TEST_ASSERT_TRUE(res.wsTimedOut);
  TEST_ASSERT_TRUE(res.canSleep());
  TEST_ASSERT_EQUAL_UINT32(0, st.wsBlockStartMs);  // remis à 0
}

// Timeout WS dépassé MAIS forceWakeUp -> log TIMEOUT puis blocage ForceWakeUp.
void test_ws_timeout_then_forcewakeup(void) {
  SleepBlocking::State st; st.wsBlockStartMs = BASE_NOW - 400000u;
  auto in = baseInputs(); in.wsClients = 1; in.forceWakeUp = true;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::ForceWakeUp, res.reason);
  TEST_ASSERT_TRUE(res.wsTimedOut);
  TEST_ASSERT_EQUAL_UINT32(0, st.wsBlockStartMs);
}

// wsClients == 0 -> wsBlockStartMs remis à 0 (même s'il était armé).
void test_ws_zero_resets_start(void) {
  SleepBlocking::State st; st.wsBlockStartMs = 12345u;
  auto res = SleepBlocking::decide(st, baseInputs());  // wsClients = 0
  TEST_ASSERT_EQUAL(Reason::Allow, res.reason);
  TEST_ASSERT_EQUAL_UINT32(0, st.wsBlockStartMs);
}

// Activité web récente (non expirée) -> WebActivity (bloqué).
void test_web_activity_recent_blocks(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.forceWakeFromWeb = true; in.lastWebActivityMs = BASE_NOW - 10000u;  // 10 s
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WebActivity, res.reason);
  TEST_ASSERT_FALSE(res.webExpired);
}

// Activité web expirée (> timeout) -> webExpired, fall-through jusqu'à Allow.
void test_web_activity_expired(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.forceWakeFromWeb = true; in.lastWebActivityMs = BASE_NOW - 100000u;  // 100 s
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::Allow, res.reason);
  TEST_ASSERT_TRUE(res.webExpired);
}

// lastWebActivityMs == 0 : condition « > 0 » fausse -> PAS d'expiration -> bloqué.
void test_web_activity_zero_blocks(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.forceWakeFromWeb = true; in.lastWebActivityMs = 0;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WebActivity, res.reason);
  TEST_ASSERT_FALSE(res.webExpired);
}

// Borne d'expiration web : exactement timeout -> NON expiré (> strict) ; +1 ms -> expiré.
void test_web_expiry_boundary(void) {
  SleepBlocking::State st1;
  auto in1 = baseInputs(); in1.forceWakeFromWeb = true; in1.lastWebActivityMs = BASE_NOW - WEB_TIMEOUT;  // == timeout
  TEST_ASSERT_EQUAL(Reason::WebActivity, SleepBlocking::decide(st1, in1).reason);

  SleepBlocking::State st2;
  auto in2 = baseInputs(); in2.forceWakeFromWeb = true; in2.lastWebActivityMs = BASE_NOW - (WEB_TIMEOUT + 1);
  auto res2 = SleepBlocking::decide(st2, in2);
  TEST_ASSERT_EQUAL(Reason::Allow, res2.reason);
  TEST_ASSERT_TRUE(res2.webExpired);
}

// forceWakeUp persistant -> ForceWakeUp.
void test_forcewakeup_blocks(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.forceWakeUp = true;
  TEST_ASSERT_EQUAL(Reason::ForceWakeUp, SleepBlocking::decide(st, in).reason);
}

// Pompe active -> TankPump + refreshLastWake.
void test_tankpump_blocks_refresh(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.tankPumpRunning = true;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::TankPump, res.reason);
  TEST_ASSERT_TRUE(res.refreshLastWake);
}

// Nourrissage en cours -> Feeding + refreshLastWake.
void test_feeding_blocks_refresh(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.feedingInProgress = true;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::Feeding, res.reason);
  TEST_ASSERT_TRUE(res.refreshLastWake);
}

// Décompte long (> 300 s) -> CountdownLong + refreshLastWake + secondes restantes.
void test_countdown_long(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.countdownEnd = BASE_NOW + 400000u;  // 400 s
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::CountdownLong, res.reason);
  TEST_ASSERT_TRUE(res.refreshLastWake);
  TEST_ASSERT_EQUAL_UINT32(400u, res.remainingCountdownSec);
}

// Décompte court (<= 300 s) -> CountdownShort, PAS de refreshLastWake.
void test_countdown_short(void) {
  SleepBlocking::State st;
  auto in = baseInputs(); in.countdownEnd = BASE_NOW + 200000u;  // 200 s
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::CountdownShort, res.reason);
  TEST_ASSERT_FALSE(res.refreshLastWake);
  TEST_ASSERT_EQUAL_UINT32(200u, res.remainingCountdownSec);
}

// Borne décompte : exactement 300 s -> court ; 301 s -> long.
void test_countdown_boundary(void) {
  SleepBlocking::State st1;
  auto in1 = baseInputs(); in1.countdownEnd = BASE_NOW + 300000u;  // 300 s
  TEST_ASSERT_EQUAL(Reason::CountdownShort, SleepBlocking::decide(st1, in1).reason);

  SleepBlocking::State st2;
  auto in2 = baseInputs(); in2.countdownEnd = BASE_NOW + 301000u;  // 301 s
  TEST_ASSERT_EQUAL(Reason::CountdownLong, SleepBlocking::decide(st2, in2).reason);
}

// Décompte passé ou nul -> non pendant -> Allow.
void test_countdown_past_or_zero_is_allow(void) {
  SleepBlocking::State st1;
  auto in1 = baseInputs(); in1.countdownEnd = BASE_NOW - 1000u;  // déjà passé
  TEST_ASSERT_EQUAL(Reason::Allow, SleepBlocking::decide(st1, in1).reason);

  SleepBlocking::State st2;
  auto in2 = baseInputs(); in2.countdownEnd = 0;  // pas de décompte
  TEST_ASSERT_EQUAL(Reason::Allow, SleepBlocking::decide(st2, in2).reason);
}

// Priorité : clients WS (dans timeout) priment sur tout le reste.
void test_priority_ws_over_all(void) {
  SleepBlocking::State st;
  auto in = baseInputs();
  in.wsClients = 1; in.forceWakeUp = true; in.tankPumpRunning = true; in.feedingInProgress = true;
  TEST_ASSERT_EQUAL(Reason::WsClients, SleepBlocking::decide(st, in).reason);
}

// Priorité : activité web (récente) prime sur forceWakeUp.
void test_priority_web_over_forcewake(void) {
  SleepBlocking::State st;
  auto in = baseInputs();
  in.forceWakeFromWeb = true; in.lastWebActivityMs = BASE_NOW - 1000u; in.forceWakeUp = true;
  TEST_ASSERT_EQUAL(Reason::WebActivity, SleepBlocking::decide(st, in).reason);
}

// Priorité : forceWakeUp prime sur pompe ; pompe sur nourrissage ; nourrissage sur décompte.
void test_priority_chain(void) {
  SleepBlocking::State st;
  auto inFw = baseInputs(); inFw.forceWakeUp = true; inFw.tankPumpRunning = true;
  TEST_ASSERT_EQUAL(Reason::ForceWakeUp, SleepBlocking::decide(st, inFw).reason);

  auto inPump = baseInputs(); inPump.tankPumpRunning = true; inPump.feedingInProgress = true;
  TEST_ASSERT_EQUAL(Reason::TankPump, SleepBlocking::decide(st, inPump).reason);

  auto inFeed = baseInputs(); inFeed.feedingInProgress = true; inFeed.countdownEnd = BASE_NOW + 400000u;
  TEST_ASSERT_EQUAL(Reason::Feeding, SleepBlocking::decide(st, inFeed).reason);
}

// Wrap-around millis() sur le décompte : now au max uint32, countdownEnd wrappé -> 400 s correctement.
void test_wraparound_countdown(void) {
  const uint32_t now = 0xFFFFFFFFu;
  SleepBlocking::State st;
  auto in = baseInputs(now);
  in.countdownEnd = now + 400000u;  // wrappe (= 399999)
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::CountdownLong, res.reason);
  TEST_ASSERT_EQUAL_UINT32(400u, res.remainingCountdownSec);
}

// Wrap-around millis() sur l'elapsed WS : démarrage juste avant le wrap, now juste après.
void test_wraparound_ws_elapsed(void) {
  const uint32_t now = 0x00000100u;          // 256
  SleepBlocking::State st; st.wsBlockStartMs = 0xFFFFFF00u;  // 256 ms avant le wrap
  auto in = baseInputs(now); in.wsClients = 1;
  auto res = SleepBlocking::decide(st, in);
  TEST_ASSERT_EQUAL(Reason::WsClients, res.reason);
  TEST_ASSERT_FALSE(res.wsTimedOut);
  TEST_ASSERT_EQUAL_UINT32(512u, res.wsElapsedMs);  // 256 (avant wrap) + 256 (après) = 512
}

int main(void) {
  UNITY_BEGIN();
  RUN_TEST(test_allow_when_nothing);
  RUN_TEST(test_ws_first_entry);
  RUN_TEST(test_ws_within_timeout);
  RUN_TEST(test_ws_timeout_boundary_not_exceeded);
  RUN_TEST(test_ws_timeout_exceeded_to_allow);
  RUN_TEST(test_ws_timeout_then_forcewakeup);
  RUN_TEST(test_ws_zero_resets_start);
  RUN_TEST(test_web_activity_recent_blocks);
  RUN_TEST(test_web_activity_expired);
  RUN_TEST(test_web_activity_zero_blocks);
  RUN_TEST(test_web_expiry_boundary);
  RUN_TEST(test_forcewakeup_blocks);
  RUN_TEST(test_tankpump_blocks_refresh);
  RUN_TEST(test_feeding_blocks_refresh);
  RUN_TEST(test_countdown_long);
  RUN_TEST(test_countdown_short);
  RUN_TEST(test_countdown_boundary);
  RUN_TEST(test_countdown_past_or_zero_is_allow);
  RUN_TEST(test_priority_ws_over_all);
  RUN_TEST(test_priority_web_over_forcewake);
  RUN_TEST(test_priority_chain);
  RUN_TEST(test_wraparound_countdown);
  RUN_TEST(test_wraparound_ws_elapsed);
  return UNITY_END();
}
