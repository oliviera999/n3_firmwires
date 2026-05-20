#pragma once

#include <ArduinoJson.h>

class AsyncWebServer;
class AsyncWebServerRequest;
struct AppContext;

// Fonctions libres helpers (remplace WebServerContext).
// v13.60 (audit sécurité): enableCors défaut = false (UI same-origin, pas besoin de CORS *).
// Routes qui doivent rester accessibles cross-origin (ex. /ping health check externe)
// passent explicitement true.
void sendJsonResponse(AsyncWebServerRequest* req, const JsonDocument& doc, bool enableCors = false);
void sendErrorResponse(AsyncWebServerRequest* req, int httpCode, const char* errorMessage, bool enableCors = false);
bool ensureHeapForRoute(AsyncWebServerRequest* req, uint32_t minHeap, const __FlashStringHelper* routeName);

// Auth web locale (implémentées dans web_server.cpp, utilisées par routes protégées)
bool webAuthIsAuthenticated(AsyncWebServerRequest* req);
void webAuthSendRequired(AsyncWebServerRequest* req);
// v13.52: variante WebSocket - compare un token en clair reçu via payload JSON.
bool webAuthIsTokenValid(const char* token);

namespace WebRoutes {
void registerStatusRoutes(AsyncWebServer& server, AppContext& ctx);
}
