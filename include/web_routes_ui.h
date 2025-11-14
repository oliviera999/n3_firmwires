#pragma once

class AsyncWebServer;
struct WebServerContext;

namespace WebRoutes {
void registerUiRoutes(AsyncWebServer& server, WebServerContext& ctx);
}











