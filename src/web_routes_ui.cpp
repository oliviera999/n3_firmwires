#include "web_routes_ui.h"

#ifndef DISABLE_ASYNC_WEBSERVER

#include <ESPAsyncWebServer.h>
#include <cstring>
#include <LittleFS.h>
#include <esp_task_wdt.h>

#include "automatism.h"
#include "web_assets.h"
#include "app_context.h"

using WebRoutes::registerUiRoutes;

namespace {

bool serveIndexStreaming(AppContext& ctx, AsyncWebServerRequest* req) {
  uint32_t freeHeapBefore = ESP.getFreeHeap();
  Serial.printf("[Web] 📊 Heap libre avant index.html streaming: %u bytes\n", freeHeapBefore);

  const uint32_t streamMinHeapBytes = 60000;
  if (freeHeapBefore < streamMinHeapBytes) {
    Serial.printf("[Web] ⚠️ Mémoire insuffisante pour servir index.html (%u < %u bytes)\n",
                  freeHeapBefore,
                  streamMinHeapBytes);
    static const char LOW_MEM_PAGE[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
  <meta charset="UTF-8">
  <title>FFP5CS Dashboard - mémoire basse</title>
  <style>
    body{font-family:Arial,Helvetica,sans-serif;background:#0f172a;color:#e2e8f0;display:flex;align-items:center;justify-content:center;height:100vh;margin:0;text-align:center;padding:1.5rem;}
    .card{background:#1e293b;border-radius:12px;padding:2rem;max-width:480px;box-shadow:0 10px 35px rgba(15,23,42,0.35);}
    h1{font-size:1.4rem;margin-bottom:0.75rem;} p{margin:0.5rem 0;line-height:1.5;}
    button{margin-top:1.5rem;padding:0.75rem 1.5rem;border-radius:8px;border:none;background:#38bdf8;color:#0f172a;font-weight:600;cursor:pointer;}
    button:hover{background:#0ea5e9;color:#0f172a;}
  </style>
</head>
<body>
  <div class="card">
    <h1>Serveur en mémoire réduite</h1>
    <p>L'ESP32 manque momentanément de mémoire pour charger l'interface complète.</p>
    <p>Veuillez attendre quelques secondes puis réessayer.</p>
    <button onclick="location.reload()">Réessayer</button>
  </div>
</body>
</html>
)rawliteral";
    req->send_P(200, "text/html", LOW_MEM_PAGE);
    return true;
  }

  if (!LittleFS.exists("/index.html")) {
    Serial.println("[Web] ⚠️ index.html non trouvé dans LittleFS");
    return false;
  }

  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    Serial.println("[Web] ❌ Impossible d'ouvrir index.html");
    return false;
  }

  size_t fileSize = file.size();
  Serial.printf("[Web] 📏 index.html size: %u bytes (streaming)\n", fileSize);

  AsyncWebServerResponse* response = req->beginChunkedResponse(
      "text/html",
      [&ctx, file](uint8_t* buffer, size_t maxLen, size_t /*index*/) mutable -> size_t {
        esp_task_wdt_reset();

        if (ESP.getFreeHeap() < streamMinHeapBytes) {
          Serial.println("[Web] ⚠️ Mémoire critique pendant streaming");
          file.close();
          return 0;
        }

        if (!file.available()) {
          file.close();
          return 0;
        }

        size_t bytesRead = file.read(buffer, maxLen);
        return bytesRead;
      });

  if (!response) {
    Serial.println("[Web] ❌ Échec beginResponse pour index.html");
    return false;
  }

  response->addHeader("Cache-Control", "public, max-age=300");
  response->addHeader("X-Content-Type-Options", "nosniff");
  response->addHeader("X-Frame-Options", "DENY");
  req->send(response);

  uint32_t freeHeapAfter = ESP.getFreeHeap();
  Serial.printf("[Web] ✅ index.html streaming démarré (%u bytes, heap: %u→%u bytes)\n",
                fileSize,
                freeHeapBefore,
                freeHeapAfter);

  static uint32_t streamingRequests = 0;
  static uint32_t totalMemorySaved = 0;
  streamingRequests++;
  totalMemorySaved += fileSize;
  if (streamingRequests % 10 == 0) {
    Serial.printf("[Web] 📊 Streaming stats: %u requests, ~%u KB économisés\n",
                  streamingRequests,
                  totalMemorySaved / 1024);
  }

  return true;
}

void registerStaticAssets(AsyncWebServer& server) {
  server.serveStatic("/shared/", LittleFS, "/shared/").setCacheControl("max-age=86400");
  server.serveStatic("/pages/", LittleFS, "/pages/").setCacheControl("max-age=3600");
  server.serveStatic("/assets/", LittleFS, "/assets/").setCacheControl("max-age=604800");
}

void registerStreamingRoutes(AsyncWebServer& server, AppContext& ctx) {
  server.on("/", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    ctx.automatism.notifyLocalWebActivity();
    IPAddress remoteIP = req->client()->remoteIP();
    Serial.printf("[Web] 🌐 Requête / depuis %d.%d.%d.%d\n",
                  remoteIP[0], remoteIP[1], remoteIP[2], remoteIP[3]);

    if (serveIndexStreaming(ctx, req)) {
      return;
    }

    Serial.println("[Web] ⚠️ Fallback vers version embarquée");
    size_t len = strlen_P((PGM_P)INDEX_HTML);
    Serial.printf("[Web] 📦 Fallback size: %u bytes\n", len);

    AsyncWebServerResponse* r = req->beginResponse_P(
        200,
        "text/html",
        reinterpret_cast<const uint8_t*>(INDEX_HTML),
        len);
    if (r) {
      r->addHeader("Cache-Control", "public, max-age=300");
      r->addHeader("X-Content-Type-Options", "nosniff");
      r->addHeader("X-Frame-Options", "DENY");
      req->send(r);
      Serial.println("[Web] ✅ Fallback envoyé");
    } else {
      Serial.println("[Web] ❌ ERREUR CRITIQUE: Impossible d'envoyer fallback");
      req->send(500, "text/plain", "Erreur interne serveur");
    }
  });

  server.on("/index.html", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    Serial.println("[Web] 📁 Requête explicite /index.html");
    if (serveIndexStreaming(ctx, req)) {
      return;
    }

    Serial.println("[Web] ⚠️ Fallback vers version embarquée");
    size_t len = strlen_P((PGM_P)INDEX_HTML);
    AsyncWebServerResponse* r = req->beginResponse_P(
        200,
        "text/html",
        reinterpret_cast<const uint8_t*>(INDEX_HTML),
        len);
    if (r) {
      r->addHeader("Cache-Control", "public, max-age=300");
      r->addHeader("X-Content-Type-Options", "nosniff");
      r->addHeader("X-Frame-Options", "DENY");
      req->send(r);
    } else {
      req->send(500, "text/plain", "Erreur interne serveur");
    }
  });

  server.on("/dashboard.js", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(404, "text/plain", "Dashboard JS intégré dans la page HTML");
  });

  server.on("/dashboard.css", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send_P(200, "text/css", "/* CSS intégré dans index.html */");
  });

  server.on("/favicon.ico", HTTP_GET, [](AsyncWebServerRequest* req) {
    req->send(204);
  });

  server.on("/shared/common.js", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[Web] 📊 Heap libre avant common.js: %u bytes\n", freeHeap);

    if (freeHeap < 30000) {
      Serial.println("[Web] ⚠️ Mémoire insuffisante pour servir common.js");
      req->send(503, "text/plain", "Service temporairement indisponible - mémoire faible");
      return;
    }

    if (!LittleFS.exists("/shared/common.js")) {
      Serial.println("[Web] ❌ common.js introuvable");
      req->send(404, "text/plain", "Fichier non trouvé");
      return;
    }

    File file = LittleFS.open("/shared/common.js", "r");
    if (!file) {
      Serial.println("[Web] ❌ Impossible d'ouvrir common.js");
      req->send(500, "text/plain", "Impossible d'ouvrir le fichier");
      return;
    }

    size_t fileSize = file.size();
    Serial.printf("[Web] 📏 common.js size: %u bytes\n", fileSize);
    file.close();

    AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/shared/common.js", "application/javascript");
    if (!r) {
      Serial.println("[Web] ❌ Échec beginResponse pour common.js");
      req->send(500, "text/plain", "Erreur interne");
      return;
    }

    r->addHeader("Cache-Control", "public, max-age=86400");
    r->addHeader("X-Content-Type-Options", "nosniff");
    req->send(r);
    Serial.printf("[Web] ✅ common.js envoyé (heap libre: %u bytes)\n", ESP.getFreeHeap());
  });

  server.on("/shared/websocket.js", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    uint32_t freeHeap = ESP.getFreeHeap();
    Serial.printf("[Web] 📊 Heap libre avant websocket.js: %u bytes\n", freeHeap);

    if (freeHeap < 30000) {
      Serial.println("[Web] ⚠️ Mémoire insuffisante pour servir websocket.js");
      req->send(503, "text/plain", "Service temporairement indisponible - mémoire faible");
      return;
    }

    if (!LittleFS.exists("/shared/websocket.js")) {
      Serial.println("[Web] ❌ websocket.js introuvable");
      req->send(404, "text/plain", "Fichier non trouvé");
      return;
    }

    File file = LittleFS.open("/shared/websocket.js", "r");
    if (!file) {
      Serial.println("[Web] ❌ Impossible d'ouvrir websocket.js");
      req->send(500, "text/plain", "Impossible d'ouvrir le fichier");
      return;
    }

    size_t fileSize = file.size();
    Serial.printf("[Web] 📏 websocket.js size: %u bytes\n", fileSize);
    file.close();

    AsyncWebServerResponse* r = req->beginResponse(LittleFS, "/shared/websocket.js", "application/javascript");
    if (!r) {
      Serial.println("[Web] ❌ Échec beginResponse pour websocket.js");
      req->send(500, "text/plain", "Erreur interne");
      return;
    }

    r->addHeader("Cache-Control", "public, max-age=86400");
    r->addHeader("X-Content-Type-Options", "nosniff");
    req->send(r);
    Serial.printf("[Web] ✅ websocket.js envoyé (heap libre: %u bytes)\n", ESP.getFreeHeap());
  });
}

void registerCompressedAssets(AsyncWebServer& server) {
  auto sendWithCompression = [](AsyncWebServerRequest* req, const char* path, const char* contentType) {
    bool acceptsGzip = req->hasHeader("Accept-Encoding") &&
                      req->getHeader("Accept-Encoding")->value().indexOf("gzip") >= 0;

    if (acceptsGzip) {
      char gz[128];
      snprintf(gz, sizeof(gz), "%s.gz", path);
      if (LittleFS.exists(gz)) {
        File file = LittleFS.open(gz, "r");
        if (file) {
          size_t fileSize = file.size();
          file.close();
          Serial.printf("[Web] 📏 Gzip file %s size: %u bytes\n", gz, fileSize);
          AsyncWebServerResponse* r = req->beginResponse(LittleFS, gz, contentType);
          if (r) {
            r->addHeader("Content-Encoding", "gzip");
            r->addHeader("Cache-Control", "public, max-age=604800");
            r->addHeader("X-Content-Type-Options", "nosniff");
            req->send(r);
            return;
          }
        }
      }
    }

    if (LittleFS.exists(path)) {
      File file = LittleFS.open(path, "r");
      if (file) {
        size_t fileSize = file.size();
        file.close();
        Serial.printf("[Web] 📏 File %s size: %u bytes\n", path, fileSize);
        AsyncWebServerResponse* r = req->beginResponse(LittleFS, path, contentType);
        if (r) {
          r->addHeader("Cache-Control", "public, max-age=604800");
          r->addHeader("X-Content-Type-Options", "nosniff");
          req->send(r);
          return;
        }
      }
    }

    req->send(404);
  };

  server.on("/chart.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req) {
    sendWithCompression(req, "/chart.js", "application/javascript");
  });

  server.on("/bootstrap.min.css", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req) {
    sendWithCompression(req, "/bootstrap.min.css", "text/css");
  });

  server.on("/bootstrap.bundle.min.js", HTTP_GET, [sendWithCompression](AsyncWebServerRequest* req) {
    sendWithCompression(req, "/bootstrap.bundle.min.js", "application/javascript");
  });
}

}  // namespace

namespace WebRoutes {

void registerUiRoutes(AsyncWebServer& server, AppContext& ctx) {
  registerStaticAssets(server);
  registerStreamingRoutes(server, ctx);
  server.on("/optimized", HTTP_GET, [](AsyncWebServerRequest* req) { req->redirect("/"); });
  server.on("/classic", HTTP_GET, [&ctx](AsyncWebServerRequest* req) {
    size_t len = strlen_P((PGM_P)INDEX_HTML);
    AsyncWebServerResponse* r = req->beginResponse_P(
        200,
        "text/html",
        reinterpret_cast<const uint8_t*>(INDEX_HTML),
        len);
    if (r) {
      r->addHeader("Cache-Control", "public, max-age=300");
      r->addHeader("X-Content-Type-Options", "nosniff");
      r->addHeader("X-Frame-Options", "DENY");
      req->send(r);
    } else {
      req->send(500);
    }
  });

  registerCompressedAssets(server);
}

}  // namespace WebRoutes

#else
// Stub si DISABLE_ASYNC_WEBSERVER est défini
namespace WebRoutes {
void registerUiRoutes(AsyncWebServer& server, AppContext& ctx) { (void)server; (void)ctx; }
}  // namespace WebRoutes
#endif


