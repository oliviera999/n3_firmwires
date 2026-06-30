#include "camera_sync.h"

#include <WiFi.h>
#include <Preferences.h>
#include <ArduinoJson.h>
#include <algorithm>
#include <vector>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

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

/* Une entrée du backlog SD : numéro, horodatage de capture, chemin réel sur la carte. */
struct SyncEntry {
  uint32_t n;
  char stamp[24];  // "Y-m-d_H-i-s" ou "" si inconnu
  char path[48];   // chemin SD réel (tel qu'énuméré)
};

/* Parse un nom de fichier SD vers une SyncEntry.
 * Reconnaît le format N-first "<chiffres>_<stamp>.jpg" et le legacy "picture<N>.jpg" (stamp vide).
 * Retourne false si le nom ne correspond à aucune photo de la file. */
bool parseEntry(const char* rawName, SyncEntry& out) {
  if (!rawName || rawName[0] == '\0') return false;
  const char* name = (rawName[0] == '/') ? rawName + 1 : rawName;
  snprintf(out.path, sizeof(out.path), "/%s", name);
  out.stamp[0] = '\0';

  // Legacy : picture<N>.jpg (cartes déjà en service avant le format horodaté).
  unsigned long legacyN = 0;
  if (sscanf(name, "picture%lu.jpg", &legacyN) == 1) {
    out.n = static_cast<uint32_t>(legacyN);
    return true;
  }

  // N-first : <chiffres>_<stamp>.jpg
  const char* us = strchr(name, '_');
  if (!us || us == name) return false;
  for (const char* p = name; p < us; ++p) {
    if (!isdigit(static_cast<unsigned char>(*p))) return false;
  }
  const char* dot = strstr(us + 1, ".jpg");
  if (!dot) return false;
  out.n = static_cast<uint32_t>(strtoul(name, nullptr, 10));
  size_t len = static_cast<size_t>(dot - (us + 1));
  if (len >= sizeof(out.stamp)) len = sizeof(out.stamp) - 1;
  memcpy(out.stamp, us + 1, len);
  out.stamp[len] = '\0';
  if (strcmp(out.stamp, "0") == 0) out.stamp[0] = '\0';  // sentinelle "horloge inconnue"
  return true;
}

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

String cameraSyncBuildSdPath(uint32_t n, const char* stamp) {
  char num[16];
  snprintf(num, sizeof(num), "%010lu", static_cast<unsigned long>(n));
  String path = "/";
  path += num;
  path += "_";
  path += (stamp && stamp[0] != '\0') ? stamp : "0";
  path += ".jpg";
  return path;
}

CameraSyncResult cameraSyncDrain(const CameraSyncConfig& cfg) {
  CameraSyncResult r = {};
#if USE_SD
  const uint32_t count = nvsGet(kKeyCount);
  const uint32_t cursor = nvsGet(kKeyCursor);
  if (count <= cursor) {
    Serial.println("[SYNC] Aucun backlog a transferer.");
    return r;  // ran = false
  }

  /* Énumération du répertoire SD : on collecte les photos de numéro > curseur, bornées aux plus
     anciennes (SYNC_MAX_BACKLOG_SCAN) et triées par numéro croissant. Gère le format N-first
     "<N>_<stamp>.jpg" ET le legacy "picture<N>.jpg" (cartes déjà en service). */
  std::vector<SyncEntry> entries;
  {
    fs::FS& fs = SD_MMC;
    File root = fs.open("/");
    if (root) {
      for (File f = root.openNextFile(); f; f = root.openNextFile()) {
        if (f.isDirectory()) {
          f.close();
          continue;
        }
        SyncEntry e = {};
        const bool ok = parseEntry(f.name(), e);
        f.close();
        if (!ok || e.n <= cursor) {
          continue;
        }
        // Insertion triée bornée : on conserve les SYNC_MAX_BACKLOG_SCAN plus petits numéros.
        auto it = std::lower_bound(entries.begin(), entries.end(), e,
                                   [](const SyncEntry& a, const SyncEntry& b) { return a.n < b.n; });
        entries.insert(it, e);
        if (entries.size() > static_cast<size_t>(SYNC_MAX_BACKLOG_SCAN)) {
          entries.pop_back();
        }
      }
      root.close();
    }
  }

  const uint32_t pending = static_cast<uint32_t>(entries.size());
  r.pending = pending;
  if (pending == 0) {
    /* Ne jamais avancer le curseur sans ACK serveur : une erreur SD transitoire peut rendre
       l'enumeration vide alors que les photos sont toujours recuperables au prochain reveil. */
    Serial.printf("[SYNC][WARN] Backlog NVS (%u photo(s)) mais aucune entree SD enumerable; curseur conserve a %u.\n",
                  static_cast<unsigned int>(count - cursor), static_cast<unsigned int>(cursor));
    return r;
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
  for (uint32_t i = 0; i < planned && i < entries.size(); ++i) {
    const SyncEntry& e = entries[i];
    const String seqStr = String(e.n);
    CameraUploadParams up = {};
    up.url = cfg.uploadUrl;
    up.apiKey = cfg.apiKey;
    up.syncSession = sessionStr.c_str();
    up.capturedAt = (e.stamp[0] != '\0') ? e.stamp : "";
    up.captureSeq = seqStr.c_str();
    up.reconnect = cfg.reconnect;

    const String filename = "esp32-cam-" + String(cfg.targetName ? cfg.targetName : "cam") + "-" + seqStr + ".jpg";
    size_t bytes = 0;
    const int code = cameraUploadJpegFile(up, String(e.path), filename, &bytes);
    if (code == 200 || code == 202) {
      r.sent++;
      r.bytes += bytes;
      nvsSet(kKeyCursor, e.n);  // confirmé (couvre aussi les éventuels trous < e.n)
      Serial.printf("[SYNC] #%u (%s) envoyee HTTP=%d (%u bytes)\n",
                    static_cast<unsigned int>(e.n), e.path, code, static_cast<unsigned int>(bytes));
    } else {
      r.failed++;
      Serial.printf("[SYNC][WARN] #%u echec HTTP=%d : arret du drain (reseau ?)\n",
                    static_cast<unsigned int>(e.n), code);
      break;  // réseau probablement perdu : reprise au prochain réveil
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
