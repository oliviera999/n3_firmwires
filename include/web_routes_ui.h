#pragma once

class AsyncWebServer;
struct AppContext;

namespace WebRoutes {
void registerUiRoutes(AsyncWebServer& server, AppContext& ctx);
}
