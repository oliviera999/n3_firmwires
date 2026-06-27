#include "camera_sync.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>

#include "config.h"
#include "camera_uploader.h"

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

#include "n3_data.h"

namespace {

constexpr const char* kNvsNamespace = "upcam";
constexpr const char* kKeyCount = "pic_count";   // numéro de la dernière photo écrite (partagé avec capturePhoto)
constexpr const char* kKeyCursor = "up_cursor";  // numéro de la dernière photo confirmée côté serveur

uint32_t nvsGet(const char* key) {
  Preferences prefs;
  uint32_t value = 0;
  if (prefs.begin(kNvsNamespace, true)) {
    value = prefs.getUInt(key, 0);
    prefs.end();
  }
  return value;
}

void nvsSet(const char* key, uint32_t value) {
  Preferences prefs;
  if (prefs.begin(kNvsNamespace, false)) {
    prefs.putUInt(key, value);
    prefs.end();
  }
}

/* Ouvre la session côté serveur. Retourne le code HTTP ; *outSession reçoit l'id si succès. */
int sessionStart(const CameraSyncConfig& cfg, const String& deviceSession, uint32_t total, int* outSession) {
  *outSession = 0;
  String responseBody;

  N3DataField fields[] = {
    {"api_key", cfg.apiKey ? cfg.apiKey : ""},
    {"board", String(cfg.board)},
    {"sensor", String(cfg.targetName ? cfg.targetName : "cam")},
    {"device_session", deviceSession},
    {"total", String(total)},
    {"version", String(cfg.firmwareVersion ? cfg.firmwareVersion : "")},
  };

  N3PostConfig pc = {};
  pc.url = cfg.startUrl;
  pc.apiKey = cfg.apiKey;
  pc.fields = fields;
  pc.fieldCount = sizeof(fields) / sizeof(fields[0]);
  pc.responseBodyOut = &responseBody;

  const int code = n3DataPost(pc);
  if (code == 200 && responseBody.length() > 0) {
    JsonDocument doc;
    if (deserializeJson(doc, responseBody) == DeserializationError::Ok) {
      *outSession = doc["session"].as<int>();
    }
  }
  return code;
}

/* Clôture la session côté serveur (déclenche le mail récap). Retourne le code HTTP. */
int sessionFinish(const CameraSyncConfig& cfg, int sessionId, uint32_t sent, uint32_t failed, uint32_t bytes, bool complete) {
  N3DataField fields[] = {
    {"api_key", cfg.apiKey ? cfg.apiKey : ""},
    {"board", String(cfg.board)},
    {"session", String(sessionId)},
    {"sent", String(sent)},
    {"failed", String(failed)},
    {"bytes", String(bytes)},
    {"status", String(complete ? "completed" : "aborted")},
  };

  N3PostConfig pc = {};
  pc.url = cfg.finishUrl;
  pc.apiKey = cfg.apiKey;
  pc.fields = fields;
  pc.fieldCount = sizeof(fields) / sizeof(fields[0]);

  return n3DataPost(pc);
}

}  // namespace

uint32_t cameraSyncWrittenCount() {
  return nvsGet(kKeyCount);
}

uint32_t cameraSyncNextPictureNumber() {
  Preferences prefs;
  uint32_t next = 1;
  if (prefs.begin(kNvsNamespace, false)) {
    next = prefs.getUInt(kKeyCount, 0) + 1;
    prefs.putUInt(kKeyCount, next);
    prefs.end();
  }
  return next;
}

uint32_t cameraSyncPendingCount() {
  const uint32_t count = nvsGet(kKeyCount);
  const uint32_t cursor = nvsGet(kKeyCursor);
  return (count > cursor) ? (count - cursor) : 0;
}

CameraSyncResult cameraSyncDrain(const CameraSyncConfig& cfg) {
  CameraSyncResult r = {};
#if USE_SD
  const uint32_t count = nvsGet(kKeyCount);
  const uint32_t cursor = nvsGet(kKeyCursor);
  const uint32_t pending = (count > cursor) ? (count - cursor) : 0;
  r.pending = pending;
  if (pending == 0) {
    Serial.println("[SYNC] Aucun backlog a transferer.");
    return r;  // ran = false
  }

  /* Stratégie hybride : vidage complet si le backlog dépasse le seuil, sinon drain incrémental. */
  uint32_t planned;
  if (pending > cfg.fullDrainThreshold) {
    planned = pending;
    Serial.printf("[SYNC] Backlog %u > seuil %u : vidage complet.\n",
                  static_cast<unsigned int>(pending), static_cast<unsigned int>(cfg.fullDrainThreshold));
  } else {
    planned = (pending < cfg.maxUploadsPerWake) ? pending : cfg.maxUploadsPerWake;
    Serial.printf("[SYNC] Drain incremental : %u/%u photo(s) ce reveil.\n",
                  static_cast<unsigned int>(planned), static_cast<unsigned int>(pending));
  }
  r.planned = planned;

  /* Identité de session = cible + numéro le plus haut écrit (stable sur retry du même backlog). */
  const String deviceSession = String(cfg.targetName ? cfg.targetName : "cam") + "-" + String(count);
  int sessionId = 0;
  const int startCode = sessionStart(cfg, deviceSession, planned, &sessionId);
  r.ran = true;
  r.sessionId = sessionId;
  Serial.printf("[SYNC] start HTTP=%d session=%d pending=%u planned=%u\n",
                startCode, sessionId, static_cast<unsigned int>(pending), static_cast<unsigned int>(planned));
  if (startCode != 200 || sessionId <= 0) {
    Serial.println("[SYNC][WARN] Ouverture de session echouee, drain annule.");
    return r;
  }

  const String sessionStr = String(sessionId);
  CameraUploadParams up = {};
  up.url = cfg.uploadUrl;
  up.apiKey = cfg.apiKey;
  up.syncSession = sessionStr.c_str();
  up.reconnect = cfg.reconnect;

  fs::FS& fs = SD_MMC;
  uint32_t processed = 0;
  uint32_t n = cursor;
  while (processed < planned && n < count) {
    n++;
    const String path = "/picture" + String(n) + ".jpg";
    if (!fs.exists(path.c_str())) {
      /* Trou (fichier supprimé / carte changée) : on avance le curseur, rien à envoyer. */
      Serial.printf("[SYNC] picture%u absente, curseur avance.\n", static_cast<unsigned int>(n));
      nvsSet(kKeyCursor, n);
      continue;
    }

    const String filename = "esp32-cam-" + String(cfg.targetName ? cfg.targetName : "cam") + "-" + String(n) + ".jpg";
    size_t bytes = 0;
    const int code = cameraUploadJpegFile(up, path, filename, &bytes);
    processed++;
    if (code == 200 || code == 202) {
      r.sent++;
      r.bytes += bytes;
      nvsSet(kKeyCursor, n);  // confirmé côté serveur
      Serial.printf("[SYNC] picture%u envoyee HTTP=%d (%u bytes)\n",
                    static_cast<unsigned int>(n), code, static_cast<unsigned int>(bytes));
    } else {
      r.failed++;
      Serial.printf("[SYNC][WARN] picture%u echec HTTP=%d : arret du drain (reseau ?)\n",
                    static_cast<unsigned int>(n), code);
      break;  // réseau probablement perdu : on s'arrête, reprise au prochain réveil
    }
  }

  r.complete = (r.failed == 0 && r.sent == planned);
  const int finishCode = sessionFinish(cfg, sessionId, r.sent, r.failed, r.bytes, r.complete);
  Serial.printf("[SYNC] finish HTTP=%d sent=%u failed=%u bytes=%u complete=%d restant=%u\n",
                finishCode, static_cast<unsigned int>(r.sent), static_cast<unsigned int>(r.failed),
                static_cast<unsigned int>(r.bytes), r.complete ? 1 : 0,
                static_cast<unsigned int>(cameraSyncPendingCount()));
#else
  (void)cfg;
#endif
  return r;
}
