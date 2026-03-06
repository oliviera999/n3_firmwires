#include <unity.h>
#include "test_mocks.h"
#include "rate_limiter.h"
#include <string>

void setUp(void) {
  reset_mock_time();
  setup_mock_time(1000, 0);
  
  // Réinitialiser RateLimiter (créer nouvelle instance pour chaque test)
  // Note: RateLimiter utilise une instance globale, donc il faudrait une méthode reset()
}

void tearDown(void) {
  // Nettoyage après chaque test
}

// ============================================================================
// TESTS DE BASE
// ============================================================================

void test_rate_limiter_allows_first_request() {
  RateLimiter limiter;
  
  String ip = "192.168.1.100";
  String endpoint = "/api/status";
  
  bool allowed = limiter.isAllowed(ip, endpoint);
  TEST_ASSERT_TRUE(allowed);
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(1, stats.totalRequests);
  TEST_ASSERT_EQUAL(0, stats.blockedRequests);
}

void test_rate_limiter_allows_requests_within_limit() {
  RateLimiter limiter;
  limiter.setLimit("/test", 5, 1000); // 5 requêtes / 1000ms
  
  String ip = "192.168.1.100";
  
  // 5 requêtes doivent être autorisées
  for (int i = 0; i < 5; i++) {
    bool allowed = limiter.isAllowed(ip, "/test");
    TEST_ASSERT_TRUE(allowed);
    advance_time(10); // Avancer de 10ms entre chaque requête
  }
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(5, stats.totalRequests);
  TEST_ASSERT_EQUAL(0, stats.blockedRequests);
}

void test_rate_limiter_blocks_requests_over_limit() {
  RateLimiter limiter;
  limiter.setLimit("/test", 3, 1000); // 3 requêtes / 1000ms
  
  String ip = "192.168.1.100";
  
  // 3 requêtes autorisées
  for (int i = 0; i < 3; i++) {
    TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  }
  
  // La 4ème doit être bloquée
  bool allowed = limiter.isAllowed(ip, "/test");
  TEST_ASSERT_FALSE(allowed);
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(4, stats.totalRequests);
  TEST_ASSERT_EQUAL(1, stats.blockedRequests);
}

// ============================================================================
// TESTS DE FENÊTRE TEMPORELLE
// ============================================================================

void test_rate_limiter_resets_after_window() {
  RateLimiter limiter;
  limiter.setLimit("/test", 2, 500); // 2 requêtes / 500ms
  
  String ip = "192.168.1.100";
  
  // 2 requêtes autorisées
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  advance_time(10);
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  
  // 3ème bloquée
  advance_time(10);
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/test"));
  
  // Après 500ms, nouvelle fenêtre
  advance_time(500);
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
}

void test_rate_limiter_window_sliding() {
  RateLimiter limiter;
  limiter.setLimit("/test", 3, 1000); // 3 requêtes / 1000ms
  
  String ip = "192.168.1.100";
  
  // Requête à t=0
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  
  // Requête à t=300ms
  advance_time(300);
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  
  // Requête à t=600ms
  advance_time(300);
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  
  // Requête à t=700ms (trop tôt, bloquée)
  advance_time(100);
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/test"));
  
  // Requête à t=1100ms (100ms après la première requête)
  advance_time(400); // total = 1100ms depuis le début
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test")); // Devrait être autorisée car fenêtre glissante
}

// ============================================================================
// TESTS MULTI-CLIENTS
// ============================================================================

void test_rate_limiter_separate_clients() {
  RateLimiter limiter;
  limiter.setLimit("/test", 2, 1000);
  
  String ip1 = "192.168.1.100";
  String ip2 = "192.168.1.101";
  
  // IP1: 2 requêtes
  TEST_ASSERT_TRUE(limiter.isAllowed(ip1, "/test"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip1, "/test"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip1, "/test")); // Bloquée
  
  // IP2: doit être indépendant
  TEST_ASSERT_TRUE(limiter.isAllowed(ip2, "/test"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip2, "/test"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip2, "/test")); // Bloquée
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(6, stats.totalRequests);
  TEST_ASSERT_EQUAL(2, stats.blockedRequests);
}

void test_rate_limiter_separate_endpoints() {
  RateLimiter limiter;
  limiter.setLimit("/endpoint1", 2, 1000);
  limiter.setLimit("/endpoint2", 5, 1000);
  
  String ip = "192.168.1.100";
  
  // Limite endpoint1
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint1"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint1"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/endpoint1"));
  
  // Endpoint2 indépendant
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint2"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint2"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint2"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint2"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/endpoint2"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/endpoint2"));
}

// ============================================================================
// TESTS DE GESTION
// ============================================================================

void test_rate_limiter_no_limit_unlimited() {
  RateLimiter limiter;
  // Pas de limite configurée pour "/unlimited"
  
  String ip = "192.168.1.100";
  
  // Devrait toujours être autorisé
  for (int i = 0; i < 100; i++) {
    TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/unlimited"));
  }
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(100, stats.totalRequests);
  TEST_ASSERT_EQUAL(0, stats.blockedRequests);
}

void test_rate_limiter_reset_client() {
  RateLimiter limiter;
  limiter.setLimit("/test", 2, 1000);
  
  String ip = "192.168.1.100";
  
  // Bloquer le client
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/test"));
  
  // Réinitialiser
  limiter.resetClient(ip);
  
  // Devrait pouvoir faire de nouvelles requêtes
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  TEST_ASSERT_TRUE(limiter.isAllowed(ip, "/test"));
  TEST_ASSERT_FALSE(limiter.isAllowed(ip, "/test"));
}

void test_rate_limiter_stats_active_clients() {
  RateLimiter limiter;
  limiter.setLimit("/test", 10, 1000);
  
  // Créer plusieurs clients
  for (int i = 0; i < 5; i++) {
    String ip = "192.168.1." + String(100 + i);
    limiter.isAllowed(ip, "/test");
  }
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(5, stats.activeClients);
  TEST_ASSERT_EQUAL(5, stats.totalRequests);
}

// ============================================================================
// TESTS DE CLEANUP
// ============================================================================

void test_rate_limiter_cleanup_removes_old_entries() {
  RateLimiter limiter;
  limiter.setLimit("/test", 10, 1000);
  
  String ip = "192.168.1.100";
  limiter.isAllowed(ip, "/test");
  
  RateLimiter::Stats stats = limiter.getStats();
  TEST_ASSERT_EQUAL(1, stats.activeClients);
  
  // Avancer le temps au-delà de la fenêtre de cleanup (60s)
  advance_time(61000);
  
  // Cleanup devrait supprimer les entrées expirées
  limiter.cleanup();
  
  stats = limiter.getStats();
  TEST_ASSERT_EQUAL(0, stats.activeClients);
}

// ============================================================================
// RUN TESTS
// ============================================================================

int main(void) {
  UNITY_BEGIN();
  
  RUN_TEST(test_rate_limiter_allows_first_request);
  RUN_TEST(test_rate_limiter_allows_requests_within_limit);
  RUN_TEST(test_rate_limiter_blocks_requests_over_limit);
  RUN_TEST(test_rate_limiter_resets_after_window);
  RUN_TEST(test_rate_limiter_window_sliding);
  RUN_TEST(test_rate_limiter_separate_clients);
  RUN_TEST(test_rate_limiter_separate_endpoints);
  RUN_TEST(test_rate_limiter_no_limit_unlimited);
  RUN_TEST(test_rate_limiter_reset_client);
  RUN_TEST(test_rate_limiter_stats_active_clients);
  RUN_TEST(test_rate_limiter_cleanup_removes_old_entries);
  
  return UNITY_END();
}

