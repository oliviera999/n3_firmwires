#pragma once

#include <ArduinoJson.h>

class AsyncWebServer;
class AsyncWebServerRequest;
struct AppContext;

// Fonctions libres helpers (remplace WebServerContext)
void sendJsonResponse(AsyncWebServerRequest* req, const JsonDocument& doc, bool enableCors = true);
void sendErrorResponse(AsyncWebServerRequest* req, int httpCode, const char* errorMessage, bool enableCors = true);
bool ensureHeapForRoute(AsyncWebServerRequest* req, uint32_t minHeap, const __FlashStringHelper* routeName);

// Auth web locale (implémentées dans web_server.cpp, utilisées par routes protégées)
bool webAuthIsAuthenticated(AsyncWebServerRequest* req);
void webAuthSendRequired(AsyncWebServerRequest* req);

namespace WebRoutes {
void registerStatusRoutes(AsyncWebServer& server, AppContext& ctx);
}
