// web_routes_system.cpp — Endpoints système / maintenance (info MAJ, OTA, mailtest,
// format FS, test OTA). Extrait de web_server.cpp (audit optimisation v13.93).
#include "web_routes_system.h"

#ifndef DISABLE_ASYNC_WEBSERVER
#include "web_routes_status.h"  // webAuth*, sendJsonResponse, ensureHeapForRoute, getWebParam

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <LittleFS.h>
#include "ffp5cs_fs.h"

#include "config.h"
#include "automatism.h"
#include "mailer.h"
#include "config_manager.h"
#include "app_tasks.h"          // AppTasks::netRequestOtaCheck()
#include "app_context.h"

extern Automatism g_autoCtrl;
extern Mailer mailer;
extern ConfigManager config;

namespace WebRoutes {

void registerSystemRoutes(AsyncWebServer& server, AppContext& ctx) {
  (void)ctx;
  server.on("/update", HTTP_GET, [](AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    req->send(NetworkConfig::HTTP_OK, "text/html",
      "<html><head><meta charset='utf-8'><title>FFP5CS OTA</title>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<style>body{font-family:sans-serif;padding:16px;max-width:480px;}"
      "button{padding:10px 20px;margin:8px 0;cursor:pointer;border-radius:6px;border:1px solid #0d6efd;background:#0d6efd;color:#fff;} "
      "button:disabled{opacity:0.6;cursor:not-allowed;} #msg{margin-top:12px;padding:8px;border-radius:4px;white-space:pre-wrap;}</style></head><body>"
      "<h1>FFP5CS - Mise à jour OTA</h1>"
      "<p>Vérifier et lancer une mise à jour firmware (WiFi requis).</p>"
      "<button id='btn' onclick='runOta()'>Vérifier et mettre à jour</button>"
      "<div id='msg'></div>"
      "<p><a href='/'>Retour au dashboard</a></p>"
      "<script>"
      "async function runOta(){"
      "var btn=document.getElementById('btn');var msg=document.getElementById('msg');"
      "btn.disabled=true;msg.textContent='Vérification...';"
      "try{"
      "var r=await fetch('/api/ota',{method:'POST'});"
      "var j=await r.json();"
      "msg.textContent=j.message||JSON.stringify(j);"
      "msg.style.background=j.ok?'#d4edda':'#f8d7da';"
      "}catch(e){msg.textContent='Erreur: '+e.message;msg.style.background='#f8d7da';}"
      "btn.disabled=false;}"
      "</script></body></html>");
  });

  // POST /api/ota - Demande de vérification OTA à la tâche dédiée (prioritaire)
  server.on("/api/ota", HTTP_POST, [](AsyncWebServerRequest* req) {
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    g_autoCtrl.notifyLocalWebActivity();

    if (WiFi.status() != WL_CONNECTED) {
      StaticJsonDocument<256> doc;
      doc["triggered"] = false;
      doc["message"] = "WiFi non connecté";
      doc["ok"] = false;
      char buf[256];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }
    if (ESP.getFreeHeap() < HeapConfig::MIN_HEAP_OTA) {
      StaticJsonDocument<256> doc;
      doc["triggered"] = false;
      doc["message"] = "Heap insuffisant pour OTA";
      doc["ok"] = false;
      char buf[256];
      serializeJson(doc, buf, sizeof(buf));
      req->send(NetworkConfig::HTTP_OK, "application/json", buf);
      return;
    }

    AppTasks::netRequestOtaCheck();
    StaticJsonDocument<256> doc;
    doc["triggered"] = true;
    doc["message"] = "Vérification OTA envoyée à la tâche dédiée (prioritaire)";
    doc["currentVersion"] = ProjectConfig::VERSION;
    doc["ok"] = true;
    char buf[256];
    serializeJson(doc, buf, sizeof(buf));
    req->send(NetworkConfig::HTTP_OK, "application/json", buf);
  });

  // /mailtest endpoint: envoie un e-mail de test (v13.52: protégé - envoi mail arbitraire)
  server.on("/mailtest", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Action utilisateur critique
    g_autoCtrl.notifyLocalWebActivity();
    char subjBuf[128];
    if (req->hasParam("subject")) {
      const AsyncWebParameter* p = req->getParam("subject");
      if (p) {
        Utils::safeStrncpy(subjBuf, p->value().c_str(), sizeof(subjBuf));
      } else {
        Utils::safeStrncpy(subjBuf, "Test FFP5CS", sizeof(subjBuf));
      }
    } else {
      Utils::safeStrncpy(subjBuf, "Test FFP5CS", sizeof(subjBuf));
    }
    char bodyBuf[256];
    if (req->hasParam("body")) {
      const AsyncWebParameter* p = req->getParam("body");
      if (p) {
        Utils::safeStrncpy(bodyBuf, p->value().c_str(), sizeof(bodyBuf));
      } else {
        Utils::safeStrncpy(bodyBuf, "Ceci est un e-mail de test envoyé depuis l'ESP32.", sizeof(bodyBuf));
      }
    } else {
      Utils::safeStrncpy(bodyBuf, "Ceci est un e-mail de test envoyé depuis l'ESP32.", sizeof(bodyBuf));
    }
    char destBuf[128];
    if (req->hasParam("to")) {
      const AsyncWebParameter* p = req->getParam("to");
      if (p) {
        Utils::safeStrncpy(destBuf, p->value().c_str(), sizeof(destBuf));
      } else {
        const char* configured = g_autoCtrl.getEmailAddress();
        if (configured && strlen(configured) > 0) {
          Utils::safeStrncpy(destBuf, configured, sizeof(destBuf));
        } else {
          Utils::safeStrncpy(destBuf, EmailConfig::DEFAULT_RECIPIENT, sizeof(destBuf));
        }
      }
    } else {
      const char* configured = g_autoCtrl.getEmailAddress();
      if (configured && strlen(configured) > 0) {
        Utils::safeStrncpy(destBuf, configured, sizeof(destBuf));
      } else {
        Utils::safeStrncpy(destBuf, EmailConfig::DEFAULT_RECIPIENT, sizeof(destBuf));
      }
    }
    bool ok = mailer.sendAlert(subjBuf, bodyBuf, destBuf);
    const char* resp = ok ? "OK" : "FAIL";
    req->send(NetworkConfig::HTTP_OK, "text/plain", resp);
  });

#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  // Maintenance: format LittleFS (use with care) (v13.52: auth obligatoire)
  server.on("/fs/format", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Action maintenance critique
    g_autoCtrl.notifyLocalWebActivity();
    if (!req->hasParam("confirm")) {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing confirm=1");
      return;
    }
    const AsyncWebParameter* pConfirm = req->getParam("confirm");
    if (!pConfirm || pConfirm->value() != "1") {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "confirm must be 1");
      return;
    }
    bool ok = FFP5CS_FS.format();
    req->send(ok ? 200 : 500, "text/plain", ok ? "LittleFS formatted" : "Format failed");
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

  // /testota endpoint: active manuellement le flag OTA pour les tests (v13.52: auth obligatoire)
  server.on("/testota", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // v11.40: Pas de notifyLocalWebActivity() - endpoint de test
    config.setOtaUpdateFlag(true);
    req->send(NetworkConfig::HTTP_OK, "text/plain", "Flag OTA activé - redémarrez pour tester l'email");
  });
}

}  // namespace WebRoutes

#else
// Stub si DISABLE_ASYNC_WEBSERVER est défini (ex. wroom-prod) — ESPAsyncWebServer ignoré.
namespace WebRoutes {
void registerSystemRoutes(AsyncWebServer& server, AppContext& ctx) { (void)server; (void)ctx; }
}  // namespace WebRoutes
#endif
