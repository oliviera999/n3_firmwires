#include <unity.h>
#include "test_mocks.h"
#include "timer_manager.h"

// Compteurs pour vérifier les callbacks
static int callback1_count = 0;
static int callback2_count = 0;

void test_callback1() {
  callback1_count++;
}

void test_callback2() {
  callback2_count++;
}

void setUp(void) {
  // Reset avant chaque test
  setup_mock_time(0, 0);
  callback1_count = 0;
  callback2_count = 0;
  
  // Réinitialiser TimerManager (nécessite accès aux variables statiques)
  // Note: En production, il faudrait une méthode reset() ou utiliser setUp/tearDown
}

void tearDown(void) {
  // Nettoyage après chaque test
}

// ============================================================================
// TESTS D'INITIALISATION
// ============================================================================

void test_timer_manager_init() {
  TimerManager::init();
  TEST_ASSERT_EQUAL(0, TimerManager::getActiveTimerCount());
  TEST_ASSERT_EQUAL(0, TimerManager::getTotalExecutionTime());
}

void test_timer_manager_add_single_timer() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  int id = TimerManager::addTimer("test1", 100, test_callback1);
  
  TEST_ASSERT_GREATER_OR_EQUAL(0, id);
  TEST_ASSERT_EQUAL(1, TimerManager::getActiveTimerCount());
  
  TimerManager::Timer* stats = TimerManager::getTimerStats(id);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_EQUAL_STRING("test1", stats->name);
  TEST_ASSERT_EQUAL(100, stats->interval);
  TEST_ASSERT_TRUE(stats->enabled);
}

void test_timer_manager_add_multiple_timers() {
  TimerManager::init();
  
  int id1 = TimerManager::addTimer("timer1", 100, test_callback1);
  int id2 = TimerManager::addTimer("timer2", 200, test_callback2);
  
  TEST_ASSERT_NOT_EQUAL(id1, id2);
  TEST_ASSERT_EQUAL(2, TimerManager::getActiveTimerCount());
}

void test_timer_manager_max_capacity() {
  TimerManager::init();
  
  // Ajouter jusqu'à la capacité max
  for (size_t i = 0; i < TimerManager::MAX_TIMERS; i++) {
    char name[16];
    snprintf(name, sizeof(name), "timer%zu", i);
    int id = TimerManager::addTimer(name, 100, test_callback1);
    TEST_ASSERT_GREATER_OR_EQUAL(0, id);
  }
  
  // Le suivant doit échouer
  int id = TimerManager::addTimer("overflow", 100, test_callback1);
  TEST_ASSERT_EQUAL(-1, id);
  TEST_ASSERT_EQUAL(TimerManager::MAX_TIMERS, TimerManager::getActiveTimerCount());
}

// ============================================================================
// TESTS D'EXÉCUTION
// ============================================================================

void test_timer_manager_process_triggers_callback() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  int id = TimerManager::addTimer("test", 100, test_callback1);
  TEST_ASSERT_EQUAL(0, callback1_count);
  
  // Avancer le temps de 50ms (pas assez)
  advance_time(50);
  TimerManager::process();
  TEST_ASSERT_EQUAL(0, callback1_count);
  
  // Avancer de 60ms supplémentaires (total 110ms > 100ms)
  advance_time(60);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
}

void test_timer_manager_process_multiple_triggers() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  TimerManager::addTimer("timer1", 100, test_callback1);
  TimerManager::addTimer("timer2", 200, test_callback2);
  
  // Après 110ms: timer1 doit se déclencher
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
  TEST_ASSERT_EQUAL(0, callback2_count);
  
  // Après 100ms supplémentaires (total 210ms): timer1 et timer2 doivent se déclencher
  advance_time(100);
  TimerManager::process();
  TEST_ASSERT_EQUAL(2, callback1_count);
  TEST_ASSERT_EQUAL(1, callback2_count);
}

void test_timer_manager_process_only_enabled() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  int id = TimerManager::addTimer("test", 100, test_callback1);
  TimerManager::enableTimer(id, false);
  
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(0, callback1_count);
  
  // Réactiver
  TimerManager::enableTimer(id, true);
  advance_time(100);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
}

// ============================================================================
// TESTS DE GESTION
// ============================================================================

void test_timer_manager_enable_disable_by_id() {
  TimerManager::init();
  
  int id = TimerManager::addTimer("test", 100, test_callback1);
  TEST_ASSERT_TRUE(TimerManager::getTimerStats(id)->enabled);
  
  TimerManager::enableTimer(id, false);
  TEST_ASSERT_FALSE(TimerManager::getTimerStats(id)->enabled);
  
  TimerManager::enableTimer(id, true);
  TEST_ASSERT_TRUE(TimerManager::getTimerStats(id)->enabled);
}

void test_timer_manager_enable_disable_by_name() {
  TimerManager::init();
  
  TimerManager::addTimer("timer1", 100, test_callback1);
  TimerManager::enableTimer("timer1", false);
  
  // Vérifier qu'il est désactivé (nécessite getTimerStats par nom ou itération)
  // Pour l'instant, on teste juste qu'il ne se déclenche pas
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(0, callback1_count);
}

void test_timer_manager_update_interval() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  int id = TimerManager::addTimer("test", 100, test_callback1);
  TimerManager::updateInterval(id, 200);
  
  TimerManager::Timer* stats = TimerManager::getTimerStats(id);
  TEST_ASSERT_EQUAL(200, stats->interval);
  
  // Après 150ms, ne doit pas se déclencher (nouveau interval = 200ms)
  advance_time(150);
  TimerManager::process();
  TEST_ASSERT_EQUAL(0, callback1_count);
  
  // Après 60ms supplémentaires (total 210ms), doit se déclencher
  advance_time(60);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
}

void test_timer_manager_suspend_resume_all() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  TimerManager::addTimer("timer1", 100, test_callback1);
  TimerManager::addTimer("timer2", 100, test_callback2);
  
  TimerManager::suspendAll();
  TEST_ASSERT_EQUAL(0, TimerManager::getActiveTimerCount());
  
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(0, callback1_count);
  TEST_ASSERT_EQUAL(0, callback2_count);
  
  TimerManager::resumeAll();
  TEST_ASSERT_EQUAL(2, TimerManager::getActiveTimerCount());
  
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
  TEST_ASSERT_EQUAL(1, callback2_count);
}

// ============================================================================
// TESTS DE STATISTIQUES
// ============================================================================

void test_timer_manager_stats_tracking() {
  TimerManager::init();
  setup_mock_time(1000, 0);
  
  int id = TimerManager::addTimer("test", 100, test_callback1);
  
  // Déclencher plusieurs fois
  for (int i = 0; i < 5; i++) {
    advance_time(100);
    TimerManager::process();
  }
  
  TimerManager::Timer* stats = TimerManager::getTimerStats(id);
  TEST_ASSERT_NOT_NULL(stats);
  TEST_ASSERT_EQUAL(5, stats->callCount);
  TEST_ASSERT_GREATER(0, stats->totalTime);
}

void test_timer_manager_duplicate_name_returns_existing() {
  TimerManager::init();
  
  int id1 = TimerManager::addTimer("test", 100, test_callback1);
  int id2 = TimerManager::addTimer("test", 200, test_callback2);
  
  // Devrait retourner le même ID
  TEST_ASSERT_EQUAL(id1, id2);
  // Le callback devrait rester le premier
  advance_time(110);
  TimerManager::process();
  TEST_ASSERT_EQUAL(1, callback1_count);
  TEST_ASSERT_EQUAL(0, callback2_count);
}

// ============================================================================
// RUN TESTS
// ============================================================================

int main(void) {
  UNITY_BEGIN();
  
  RUN_TEST(test_timer_manager_init);
  RUN_TEST(test_timer_manager_add_single_timer);
  RUN_TEST(test_timer_manager_add_multiple_timers);
  RUN_TEST(test_timer_manager_max_capacity);
  RUN_TEST(test_timer_manager_process_triggers_callback);
  RUN_TEST(test_timer_manager_process_multiple_triggers);
  RUN_TEST(test_timer_manager_process_only_enabled);
  RUN_TEST(test_timer_manager_enable_disable_by_id);
  RUN_TEST(test_timer_manager_enable_disable_by_name);
  RUN_TEST(test_timer_manager_update_interval);
  RUN_TEST(test_timer_manager_suspend_resume_all);
  RUN_TEST(test_timer_manager_stats_tracking);
  RUN_TEST(test_timer_manager_duplicate_name_returns_existing);
  
  return UNITY_END();
}
