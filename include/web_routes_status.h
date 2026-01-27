#pragma once

#include <ArduinoJson.h>

class AsyncWebServer;
class AsyncWebServerRequest;
struct AppContext;

// Fonctions libres helpers (remplace WebServerContext)
void sendJsonResponse(AsyncWebServerRequest* req, const JsonDocument& doc, bool enableCors = true);
bool ensureHeapForRoute(AsyncWebServerRequest* req, uint32_t minHeap, const __FlashStringHelper* routeName);

namespace WebRoutes {
void registerStatusRoutes(AsyncWebServer& server, AppContext& ctx);
}











