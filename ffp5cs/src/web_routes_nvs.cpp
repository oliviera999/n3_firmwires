// web_routes_nvs.cpp — Endpoints d'inspection/édition NVS (diagnostic).
// Extrait de web_server.cpp (audit optimisation v13.93) pour alléger le god-file.
// Routes gardées par FFP_ENABLE_DANGEROUS_ENDPOINTS ; comportement identique.
#include "web_routes_nvs.h"
#include "web_routes_status.h"  // webAuth*, sendJsonResponse, sendErrorResponse, ensureHeapForRoute, getWebParam
#include "dbvars_cache.h"       // invalidateDbvarsCache

#include <ESPAsyncWebServer.h>
#include <AsyncJson.h>
#include <ArduinoJson.h>
#include <nvs.h>
#include <nvs_flash.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>

#include "config.h"
#include "automatism.h"
#include "app_context.h"

extern Automatism g_autoCtrl;

#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
// Table unique type NVS <-> libellé : source de vérité partagée par
// nvsTypeToStr() et nvsStrToType(). Ajouter un type ici suffit pour les deux sens.
static const struct { nvs_type_t type; const char* label; } kNvsTypeTable[] = {
  { NVS_TYPE_U8,   "U8"   },
  { NVS_TYPE_I8,   "I8"   },
  { NVS_TYPE_U16,  "U16"  },
  { NVS_TYPE_I16,  "I16"  },
  { NVS_TYPE_U32,  "U32"  },
  { NVS_TYPE_I32,  "I32"  },
  { NVS_TYPE_U64,  "U64"  },
  { NVS_TYPE_I64,  "I64"  },
  { NVS_TYPE_STR,  "STR"  },
  { NVS_TYPE_BLOB, "BLOB" },
};

static const char* nvsTypeToStr(nvs_type_t t) {
  for (const auto& e : kNvsTypeTable) {
    if (e.type == t) return e.label;
  }
  return "UNKNOWN";
}

static nvs_type_t nvsStrToType(const char* s) {
  for (const auto& e : kNvsTypeTable) {
    if (strcmp(s, e.label) == 0) return e.type;
  }
  return NVS_TYPE_ANY;
}

static void printNvsEntryToJson(nvs_handle_t h, const nvs_entry_info_t& e2, Print* res) {
  res->print("\"type\":\"");
  res->print(nvsTypeToStr(e2.type));
  res->print("\"");
  esp_err_t err = ESP_ERR_INVALID_ARG;
  switch (e2.type) {
    case NVS_TYPE_U8: {
      uint8_t v = 0;
      err = nvs_get_u8(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I8: {
      int8_t v = 0;
      err = nvs_get_i8(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U16: {
      uint16_t v = 0;
      err = nvs_get_u16(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I16: {
      int16_t v = 0;
      err = nvs_get_i16(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U32: {
      uint32_t v = 0;
      err = nvs_get_u32(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_I32: {
      int32_t v = 0;
      err = nvs_get_i32(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print(v); }
    } break;
    case NVS_TYPE_U64: {
      uint64_t v = 0;
      err = nvs_get_u64(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print((uint64_t)v); }
    } break;
    case NVS_TYPE_I64: {
      int64_t v = 0;
      err = nvs_get_i64(h, e2.key, &v);
      if (err == ESP_OK) { res->print(",\"value\":"); res->print((int64_t)v); }
    } break;
    case NVS_TYPE_STR: {
      size_t len = 0;
      err = nvs_get_str(h, e2.key, nullptr, &len);
      if (err == ESP_OK) {
        if (len > 0 && len < BufferConfig::JSON_DOCUMENT_SIZE) {
          static char nvsStrBuf[BufferConfig::JSON_DOCUMENT_SIZE];
          size_t bufLen = sizeof(nvsStrBuf);
          if (nvs_get_str(h, e2.key, nvsStrBuf, &bufLen) == ESP_OK) {
            res->print(",\"value\":\"");
            for (size_t i = 0; i < bufLen && nvsStrBuf[i]; ++i) {
              char c = nvsStrBuf[i];
              if (c == '"' || c == '\\') { res->print('\\'); res->print(c); }
              else if (c == '\n') { res->print("\\n"); }
              else if (c == '\r') { res->print("\\r"); }
              else { res->print(c); }
            }
            res->print("\"");
          } else {
            res->print(",\"value\":\"<err>\"");
          }
        } else if (len >= BufferConfig::JSON_DOCUMENT_SIZE) {
          res->print(",\"value\":\"<too_long>\"");
        } else {
          res->print(",\"value\":\"\"");
        }
      }
    } break;
    case NVS_TYPE_BLOB: {
      size_t len = 0;
      err = nvs_get_blob(h, e2.key, nullptr, &len);
      if (err == ESP_OK) {
        res->print(",\"length\":");
        res->print((uint32_t)len);
        res->print(",\"value\":\"<blob>\"");
      }
    } break;
    default: break;
  }
}
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS

namespace WebRoutes {

void registerNvsRoutes(AsyncWebServer& server, AppContext& ctx) {
  (void)server;
  (void)ctx;
#ifdef FFP_ENABLE_DANGEROUS_ENDPOINTS
  server.on("/nvs.json", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Outil de debug interactif
    g_autoCtrl.notifyLocalWebActivity();
    if (!ensureHeapForRoute(req, HeapConfig::MIN_HEAP_RESPONSE_STREAM, F("/nvs.json"))) {
      return;
    }
    AsyncResponseStream* res = req->beginResponseStream("application/json");
    if (!res) {
      req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Memory error");
      return;
    }
    res->print('{');
    res->print("\"namespaces\":{");

    bool firstNs = true;
#if defined(ESP_IDF_VERSION_MAJOR) && (ESP_IDF_VERSION_MAJOR >= 5)
    nvs_iterator_t it = nullptr;
    if (nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY, &it) == ESP_OK) {
      do {
        nvs_entry_info_t info;
        nvs_entry_info(it, &info);

        if (!firstNs) res->print(',');
        firstNs = false;
        res->print('"'); res->print(info.namespace_name); res->print("\":{");

        bool firstKey = true;
        nvs_iterator_t it2 = nullptr;
        if (nvs_entry_find(NVS_DEFAULT_PART_NAME, info.namespace_name,
                           NVS_TYPE_ANY, &it2) == ESP_OK) {
          do {
            nvs_entry_info_t e2; nvs_entry_info(it2, &e2);
            if (strcmp(e2.namespace_name, info.namespace_name) != 0) continue;
            if (!firstKey) res->print(',');
            firstKey = false;
            res->print('"'); res->print(e2.key); res->print("\":{");

            nvs_handle_t h;
            if (nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
              printNvsEntryToJson(h, e2, res);
              nvs_close(h);
            }
            res->print('}');
          } while (nvs_entry_next(&it2) == ESP_OK);
          nvs_release_iterator(it2);
        }

        res->print('}');
      } while (nvs_entry_next(&it) == ESP_OK);
      nvs_release_iterator(it);
    }
#else
    // IDF 4.x: nvs_entry_find(3 args) retourne l'itérateur, nvs_entry_next(it) retourne le suivant
    nvs_iterator_t it = nvs_entry_find(NVS_DEFAULT_PART_NAME, nullptr, NVS_TYPE_ANY);
    while (it != nullptr) {
      nvs_entry_info_t info;
      nvs_entry_info(it, &info);

      if (!firstNs) res->print(',');
      firstNs = false;
      res->print('"'); res->print(info.namespace_name); res->print("\":{");

      bool firstKey = true;
      nvs_iterator_t it2 = nvs_entry_find(NVS_DEFAULT_PART_NAME, info.namespace_name, NVS_TYPE_ANY);
      while (it2 != nullptr) {
        nvs_entry_info_t e2; nvs_entry_info(it2, &e2);
        if (strcmp(e2.namespace_name, info.namespace_name) != 0) { it2 = nvs_entry_next(it2); continue; }
        if (!firstKey) res->print(',');
        firstKey = false;
        res->print('"'); res->print(e2.key); res->print("\":{");

        nvs_handle_t h;
        if (nvs_open(info.namespace_name, NVS_READONLY, &h) == ESP_OK) {
          printNvsEntryToJson(h, e2, res);
          nvs_close(h);
        }
        res->print('}');
        it2 = nvs_entry_next(it2);
      }

      res->print('}');
      it = nvs_entry_next(it);
    }
#endif

    res->print('}'); // namespaces
    res->print('}'); // root
    req->send(res);
  });

  server.on("/nvs", HTTP_GET, [](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Page NVS Inspector interactive
    g_autoCtrl.notifyLocalWebActivity();
    const char* html =
      "<html><head><meta charset='utf-8'>"
      "<meta name='viewport' content='width=device-width, initial-scale=1'>"
      "<title>NVS Inspector</title>"
      "<style>body{font-family:sans-serif;margin:12px;}table{border-collapse:collapse;width:100%;}"
      "th,td{border:1px solid #ddd;padding:6px;}"
      "th{background:#f3f3f3;}"
      "input[type=number]{width:120px;}"
      "code{background:#f7f7f7;padding:2px 4px;border-radius:3px;}</style>"
      "</head><body>"
      "<h2>NVS - Variables persistantes</h2>"
      "<p><a href='/'>&larr; Retour</a> | <button onclick=load()>Recharger</button></p>"
      "<div id='content'>Chargement...</div>"
      "<script>"
      "function esc(s){return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;');}"
      "async function load(){"
      "const r=await fetch('/nvs.json');"
      "const j=await r.json();"
      "let h='';"
      "for(const ns in j.namespaces){"
      "h+=`<h3>Namespace <code>${esc(ns)}</code></h3>`;"
      "h+=`<table><tr><th>Clé</th><th>Type</th><th>Valeur</th><th>Actions</th></tr>`;"
      "const obj=j.namespaces[ns];"
      "for(const k in obj){"
      "const it=obj[k];"
      "let input='';"
      "const t=it.type;"
      "const id=ns+'::'+k;"
      "if(t==='STR'){"
      "input=`<input id='v_${id}' type='text' "
      "value='${esc(it.value||'')}'/>`;"
      "}"
      "else if(t==='BLOB'){"
      "input=`<em>blob (${it.length||0} bytes)</em>`;"
      "}"
      "else{"
      "input=`<input id='v_${id}' type='number' "
      "value='${esc(it.value)}'/>`;"
      "}"
      "h+=`<tr><td><code>${esc(k)}</code></td><td>${t}</td><td>${input}</td>`+"
      "`<td>`+`<button onclick=save('${ns}','${k}','${t}')>Enregistrer</button> `+"
      "`<button onclick=delKey('${ns}','${k}')>Effacer</button>`+`</td></tr>`;}"
      "h+='</table>';"
      "h+=`<p><button onclick=clearNs('${ns}')>"
      "Effacer le namespace</button></p>`;"
      "}"
      "document.getElementById('content').innerHTML=h;}"
      "async function save(ns,k,t){"
      "const id='v_'+ns+'::'+k;"
      "const el=document.getElementById(id);"
      "let v=el?el.value:'';"
      "if(t!=='STR' && t!=='BLOB'){"
      "if(v==='')v='0';"
      "}"
      "const p=new URLSearchParams();"
      "p.set('ns',ns);p.set('key',k);"
      "p.set('type',t);p.set('value',v);"
      "const r=await fetch('/nvs/set',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "async function delKey(ns,k){"
      "if(!confirm('Effacer '+ns+'::'+k+' ?'))return;"
      "const p=new URLSearchParams();"
      "p.set('ns',ns);p.set('key',k);"
      "const r=await fetch('/nvs/erase',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "async function clearNs(ns){"
      "if(!confirm('Effacer tout le namespace '+ns+' ?'))return;"
      "const p=new URLSearchParams();p.set('ns',ns);"
      "const r=await fetch('/nvs/erase_ns',{"
      "method:'POST',"
      "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
      "body:p});"
      "alert('Statut: '+(r.ok?'OK':'ERREUR'));"
      "if(r.ok) load();"
      "}"
      "load();"
      "</script>"
      "</body></html>";
    req->send(NetworkConfig::HTTP_OK, "text/html", html);
  });

  // v11.178: Note audit - NVS Inspector utilise intentionnellement l'API NVS directe
  // (et non NVSManager) car il nécessite un accès bas niveau pour debug/inspection
  // avec support de tous les types NVS (U8, I8, U16, I16, U32, I32, U64, I64, STR, BLOB)
  server.on("/nvs/set", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64], typeBuf[16], valueBuf[256];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf)) ||
        !getWebParam(req, "key", keyBuf, sizeof(keyBuf)) ||
        !getWebParam(req, "type", typeBuf, sizeof(typeBuf))) {
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns/key/type");
      return;
    }
    getWebParam(req, "value", valueBuf, sizeof(valueBuf));
    
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }

    nvs_type_t t = nvsStrToType(typeBuf);
    if (t == NVS_TYPE_ANY) {
      nvs_close(h);
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Invalid type");
      return;
    }

    auto sendBadValue = [req, &h](const char* msg) {
      nvs_close(h);
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", msg);
    };

    bool valueChanged = false;
    errno = 0;
    char* endptr = nullptr;

    switch (t) {
      case NVS_TYPE_U8: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < 0 || parsed > 255) {
          sendBadValue("Invalid value for U8 (0-255)");
          return;
        }
        uint8_t v = (uint8_t)parsed;
        uint8_t current = 0;
        esp_err_t getErr = nvs_get_u8(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u8(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I8: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < -128 || parsed > 127) {
          sendBadValue("Invalid value for I8 (-128 to 127)");
          return;
        }
        int8_t v = (int8_t)parsed;
        int8_t current = 0;
        esp_err_t getErr = nvs_get_i8(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i8(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U16: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < 0 || parsed > 65535) {
          sendBadValue("Invalid value for U16 (0-65535)");
          return;
        }
        uint16_t v = (uint16_t)parsed;
        uint16_t current = 0;
        esp_err_t getErr = nvs_get_u16(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u16(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I16: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE || parsed < -32768 || parsed > 32767) {
          sendBadValue("Invalid value for I16 (-32768 to 32767)");
          return;
        }
        int16_t v = (int16_t)parsed;
        int16_t current = 0;
        esp_err_t getErr = nvs_get_i16(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i16(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U32: {
        unsigned long parsed = strtoul(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for U32");
          return;
        }
        uint32_t v = (uint32_t)parsed;
        uint32_t current = 0;
        esp_err_t getErr = nvs_get_u32(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u32(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I32: {
        long parsed = strtol(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for I32");
          return;
        }
        int32_t v = (int32_t)parsed;
        int32_t current = 0;
        esp_err_t getErr = nvs_get_i32(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i32(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_U64: {
        unsigned long long parsed = strtoull(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for U64");
          return;
        }
        uint64_t v = (uint64_t)parsed;
        uint64_t current = 0;
        esp_err_t getErr = nvs_get_u64(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_u64(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_I64: {
        long long parsed = strtoll(valueBuf, &endptr, 10);
        if (endptr == valueBuf || *endptr != '\0' || errno == ERANGE) {
          sendBadValue("Invalid value for I64");
          return;
        }
        int64_t v = (int64_t)parsed;
        int64_t current = 0;
        esp_err_t getErr = nvs_get_i64(h, keyBuf, &current);
        if (getErr != ESP_OK && getErr != ESP_ERR_NVS_NOT_FOUND) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "NVS read failed"); return; }
        if (getErr == ESP_ERR_NVS_NOT_FOUND || current != v) {
          err = nvs_set_i64(h, keyBuf, v);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_STR: {
        size_t len = 0;
        esp_err_t getErr = nvs_get_str(h, keyBuf, nullptr, &len);
        bool doSet = (getErr == ESP_ERR_NVS_NOT_FOUND);
        if (getErr == ESP_OK && len > 0) {
          if (len <= 512) {
            char currentStr[512];
            size_t readLen = len;
            getErr = nvs_get_str(h, keyBuf, currentStr, &readLen);
            if (getErr == ESP_OK && strcmp(currentStr, valueBuf) != 0) doSet = true;
          } else {
            doSet = true;
          }
        }
        if (doSet) {
          err = nvs_set_str(h, keyBuf, valueBuf);
          if (err == ESP_OK) valueChanged = true;
        }
      } break;
      case NVS_TYPE_BLOB: {
        req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "BLOB set not supported");
        nvs_close(h);
        return;
      }
      default: break;
    }
    if (err != ESP_OK) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Write failed"); return; }
    if (valueChanged && nvs_commit(h) != ESP_OK) { nvs_close(h); req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Commit failed"); return; }
    nvs_close(h);

    // Rafraîchir l'état runtime si nécessaire (namespaces actuels cfg/sys + legacy bouffe/ota/rtc/remoteVars)
    const bool nsCfg = (strcmp(nsBuf, NVS_NAMESPACES::CONFIG) == 0);
    const bool nsSys = (strcmp(nsBuf, NVS_NAMESPACES::SYSTEM) == 0);
    const bool legacyBouffe = (strcmp(nsBuf, "bouffe") == 0);
    const bool legacyOta = (strcmp(nsBuf, "ota") == 0);
    const bool legacyRtc = (strcmp(nsBuf, "rtc") == 0);
    const bool legacyRemoteVars = (strcmp(nsBuf, "remoteVars") == 0);
    const bool keyBouffe = (strcmp(keyBuf, NVSKeys::Config::BOUFFE_MATIN) == 0 || strcmp(keyBuf, NVSKeys::Config::BOUFFE_MIDI) == 0 ||
                            strcmp(keyBuf, NVSKeys::Config::BOUFFE_SOIR) == 0 || strcmp(keyBuf, NVSKeys::Config::BOUFFE_JOUR) == 0 ||
                            strcmp(keyBuf, NVSKeys::Config::BF_PMP_LOCK) == 0);
    if ((nsCfg && keyBouffe) || legacyBouffe || (nsSys && strcmp(keyBuf, NVSKeys::System::OTA_UPDATE_FLAG) == 0) || legacyOta) {
      config.loadBouffeFlags();
    }
    if (legacyRtc || (nsSys && strcmp(keyBuf, NVSKeys::System::RTC_EPOCH) == 0)) {
      power.loadTimeFromFlash();
    }
    if ((nsCfg && strcmp(keyBuf, NVSKeys::Config::REMOTE_JSON) == 0) || (legacyRemoteVars && strcmp(keyBuf, "json") == 0)) {
      invalidateDbvarsCache();
      if (strlen(valueBuf) > 0) {
        StaticJsonDocument<256> tmp;
        if (!deserializeJson(tmp, valueBuf)) {
          g_autoCtrl.applyConfigFromJson(tmp);
        }
      }
    }

    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });

  server.on("/nvs/erase", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32], keyBuf[64];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf)) || !getWebParam(req, "key", keyBuf, sizeof(keyBuf))) { 
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns/key"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_key(h, keyBuf); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Erase failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });

  server.on("/nvs/erase_ns", HTTP_POST, [&ctx](AsyncWebServerRequest* req){
    if (!webAuthIsAuthenticated(req)) { webAuthSendRequired(req); return; }
    // GARDER notifyLocalWebActivity() - Modification NVS critique
    g_autoCtrl.notifyLocalWebActivity();
    char nsBuf[32];
    if (!getWebParam(req, "ns", nsBuf, sizeof(nsBuf))) { 
      req->send(NetworkConfig::HTTP_BAD_REQUEST, "text/plain", "Missing ns"); 
      return; 
    }
    nvs_handle_t h; esp_err_t err = nvs_open(nsBuf, NVS_READWRITE, &h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "nvs_open failed"); return; }
    err = nvs_erase_all(h); if (err == ESP_OK) err = nvs_commit(h);
    nvs_close(h);
    if (err != ESP_OK) { req->send(NetworkConfig::HTTP_INTERNAL_ERROR, "text/plain", "Erase namespace failed"); return; }
    
    StaticJsonDocument<BufferConfig::JSON_DOCUMENT_SIZE> doc;
    doc["status"] = "OK";
    sendJsonResponse(req, doc);
  });
#endif // FFP_ENABLE_DANGEROUS_ENDPOINTS
}

}  // namespace WebRoutes
