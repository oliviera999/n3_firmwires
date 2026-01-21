#include "rate_limiter.h"
#include <string.h>
#include <sstream>

// Instance globale
RateLimiter rateLimiter;

RateLimiter::RateLimiter() : _totalRequests(0), _blockedRequests(0), _lastCleanup(0) {
  // Configuration par défaut des limites
  // Endpoints critiques: limite stricte
  setLimit("/action", 10, 10000);         // 10 requêtes / 10s
  setLimit("/dbvars/update", 5, 30000);   // 5 requêtes / 30s
  setLimit("/wifi/connect", 3, 60000);    // 3 requêtes / 60s
  setLimit("/nvs/set", 10, 30000);        // 10 requêtes / 30s
  setLimit("/nvs/erase", 5, 30000);       // 5 requêtes / 30s
  
  // Endpoints fréquents: limite plus souple
  setLimit("/json", 60, 10000);           // 60 requêtes / 10s
  setLimit("/dbvars", 30, 10000);         // 30 requêtes / 10s
  setLimit("/wifi/scan", 2, 30000);       // 2 scans / 30s (opération lente)
}

bool RateLimiter::isAllowed(const char* clientIP, const char* endpoint) {
  _totalRequests++;
  
  // Vérifier si l'endpoint a une limite configurée
  std::string endpointStr(endpoint);
  auto limitIt = _limits.find(endpointStr);
  if (limitIt == _limits.end()) {
    // Pas de limite configurée, autoriser
    return true;
  }
  
  const RateLimit& limit = limitIt->second;
  std::string key = makeKey(clientIP, endpoint);
  ClientRecord& record = getOrCreateRecord(key);
  
  uint32_t now = millis();
  
  // Vérifier si on est dans une nouvelle fenêtre
  if (now - record.firstRequest > limit.windowMs) {
    // Nouvelle fenêtre, réinitialiser
    record.firstRequest = now;
    record.count = 1;
    return true;
  }
  
  // Dans la fenêtre actuelle
  if (record.count >= limit.maxRequests) {
    // Limite atteinte
    _blockedRequests++;
    Serial.printf("[RateLimit] Blocked %s from %s (%u/%u requests)\n", 
                  endpoint, clientIP, record.count, limit.maxRequests);
    return false;
  }
  
  // Incrémenter le compteur
  record.count++;
  return true;
}

void RateLimiter::setLimit(const char* endpoint, uint16_t maxRequests, uint32_t windowMs) {
  RateLimit limit;
  limit.maxRequests = maxRequests;
  limit.windowMs = windowMs;
  _limits[std::string(endpoint)] = limit;
  
  Serial.printf("[RateLimit] Set limit for %s: %u requests / %u ms\n", 
                endpoint, maxRequests, windowMs);
}

void RateLimiter::cleanup() {
  uint32_t now = millis();
  
  // Nettoyer toutes les 60 secondes
  if (now - _lastCleanup < 60000) {
    return;
  }
  
  _lastCleanup = now;
  
  // Supprimer les entrées expirées (fenêtre max = 60s)
  auto it = _clients.begin();
  uint32_t removed = 0;
  
  while (it != _clients.end()) {
    if (now - it->second.firstRequest > 60000) {
      it = _clients.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  
  if (removed > 0) {
    Serial.printf("[RateLimit] Cleanup: %u expired entries removed\n", removed);
  }
}

void RateLimiter::resetClient(const char* clientIP) {
  auto it = _clients.begin();
  uint32_t removed = 0;
  std::string prefix = std::string(clientIP) + ":";
  
  while (it != _clients.end()) {
    if (it->first.find(prefix) == 0) {
      it = _clients.erase(it);
      removed++;
    } else {
      ++it;
    }
  }
  
  if (removed > 0) {
    Serial.printf("[RateLimit] Reset client %s: %u entries removed\n", 
                  clientIP, removed);
  }
}

RateLimiter::Stats RateLimiter::getStats() const {
  Stats stats;
  stats.totalRequests = _totalRequests;
  stats.blockedRequests = _blockedRequests;
  stats.activeClients = _clients.size();
  return stats;
}

std::string RateLimiter::makeKey(const char* clientIP, const char* endpoint) const {
  return std::string(clientIP) + ":" + std::string(endpoint);
}

RateLimiter::ClientRecord& RateLimiter::getOrCreateRecord(const std::string& key) {
  auto it = _clients.find(key);
  if (it != _clients.end()) {
    return it->second;
  }
  
  // Créer nouveau record
  ClientRecord record;
  record.firstRequest = millis();
  record.count = 0;
  _clients[key] = record;
  return _clients[key];
}

