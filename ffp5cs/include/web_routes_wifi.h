#pragma once

class AsyncWebServer;
struct AppContext;

// Endpoints backend de gestion WiFi (/wifi/scan, /saved, /connect, /remove).
// Extrait de web_server.cpp (audit optimisation v13.93) ; comportement identique.
namespace WebRoutes {
void registerWifiRoutes(AsyncWebServer& server, AppContext& ctx);
}
