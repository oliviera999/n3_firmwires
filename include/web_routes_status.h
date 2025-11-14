#pragma once

class AsyncWebServer;
struct WebServerContext;

namespace WebRoutes {
void registerStatusRoutes(AsyncWebServer& server, WebServerContext& ctx);
}











