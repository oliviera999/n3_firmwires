#pragma once

class AsyncWebServer;
struct AppContext;

// Endpoints système / maintenance : /update (info version), /api/ota, /mailtest,
// /fs/format (gardé FFP_ENABLE_DANGEROUS_ENDPOINTS), /testota.
// Extrait de web_server.cpp (audit optimisation v13.93).
namespace WebRoutes {
void registerSystemRoutes(AsyncWebServer& server, AppContext& ctx);
}
