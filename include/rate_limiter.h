#pragma once

#include <Arduino.h>
#include <map>
#ifndef DISABLE_ASYNC_WEBSERVER
#include <ESPAsyncWebServer.h>
#endif

/**
 * Rate Limiter pour endpoints critiques
 * Limite le nombre de requêtes par IP/endpoint
 * Implémentation simple sans allocation dynamique excessive
 */
class RateLimiter {
public:
  struct RateLimit {
    uint16_t maxRequests;  // Nombre max de requêtes
    uint32_t windowMs;     // Fenêtre de temps en millisecondes
  };
  
  RateLimiter();
  
  /**
   * Vérifier si une requête est autorisée
   * @param clientIP IP du client
   * @param endpoint Endpoint demandé
   * @return true si autorisé, false si rate limited
   */
  bool isAllowed(const String& clientIP, const String& endpoint);
  
  /**
   * Configurer une limite pour un endpoint
   * @param endpoint Endpoint à limiter
   * @param maxRequests Nombre max de requêtes
   * @param windowMs Fenêtre de temps en ms
   */
  void setLimit(const String& endpoint, uint16_t maxRequests, uint32_t windowMs);
  
  /**
   * Nettoyer les entrées expirées (appelé périodiquement)
   */
  void cleanup();
  
  /**
   * Réinitialiser les compteurs pour une IP
   * @param clientIP IP à réinitialiser
   */
  void resetClient(const String& clientIP);
  
  /**
   * Obtenir les statistiques
   */
  struct Stats {
    uint32_t totalRequests;
    uint32_t blockedRequests;
    uint32_t activeClients;
  };
  Stats getStats() const;
  
private:
  struct ClientRecord {
    uint32_t firstRequest;  // Timestamp première requête de la fenêtre
    uint16_t count;         // Nombre de requêtes dans la fenêtre
  };
  
  // Map endpoint -> limite configurée
  std::map<String, RateLimit> _limits;
  
  // Map (clientIP + endpoint) -> record
  std::map<String, ClientRecord> _clients;
  
  // Statistiques
  uint32_t _totalRequests;
  uint32_t _blockedRequests;
  
  // Dernière cleanup
  uint32_t _lastCleanup;
  
  // Générer clé unique pour client+endpoint
  String makeKey(const String& clientIP, const String& endpoint) const;
  
  // Obtenir ou créer un record client
  ClientRecord& getOrCreateRecord(const String& key);
};

// Instance globale
extern RateLimiter rateLimiter;

