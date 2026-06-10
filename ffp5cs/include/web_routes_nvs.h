#pragma once

class AsyncWebServer;
struct AppContext;

// Endpoints d'inspection/édition NVS (/nvs.json, /nvs, /nvs/set, /nvs/erase,
// /nvs/erase_ns), gardés par FFP_ENABLE_DANGEROUS_ENDPOINTS. Extrait de
// web_server.cpp (audit optimisation v13.93). No-op si le flag est absent.
namespace WebRoutes {
void registerNvsRoutes(AsyncWebServer& server, AppContext& ctx);
}
