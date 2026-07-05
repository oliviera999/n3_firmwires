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
#include <ctime>

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

/* Epoch pour la signature HMAC (A4) : 0 si l'horloge n'est pas fiable, ce qui désactive proprement
 * la signature (n3DataPost retombe alors sur l'auth clé API — rétro-compatible). */
unsigned long syncSignatureEpoch() {
  const unsigned long epoch = static_cast<unsigned long>(time(nullptr));
  return (epoch >= 1600000000UL) ? epoch : 0UL;
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
  pc.sigSecret = cfg.sigSecret;                 // A4 : signature HMAC additive (X-Sig-*) si secret défini
  pc.currentEpochSeconds = syncSignatureEpoch();
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

/* Clôture la session côté serveur. Retourne le code HTTP.
 * `final` (A2) : true seulement quand le backlog est réellement vidé après ce drain. Le serveur
 * n'envoie le mail récapitulatif que sur une clôture finale (ou received>=total), ce qui supprime
 * le spam d'un récap par réveil pour un gros backlog drainé en plusieurs passes. */
int sessionFinish(const CameraSyncConfig& cfg, int sessionId, uint32_t sent, uint32_t failed, uint32_t bytes, bool complete, bool final) {
  N3DataField fields[] = {
    {"api_key", cfg.apiKey ? cfg.apiKey : ""},
    {"board", String(cfg.board)},
    {"session", String(sessionId)},
    {"sent", String(sent)},
    {"failed", String(failed)},
    {"bytes", String(bytes)},
    {"status", String(complete ? "completed" : "aborted")},
    {"final", String(final ? 1 : 0)},
  };

  N3PostConfig pc = {};
  pc.url = cfg.finishUrl;
  pc.apiKey = cfg.apiKey;
  pc.fields = fields;
  pc.fieldCount = sizeof(fields) / sizeof(fields[0]);
  pc.sigSecret = cfg.sigSecret;                 // A4 : signature HMAC additive (X-Sig-*)
  pc.currentEpochSeconds = syncSignatureEpoch();

  return n3DataPost(pc);
}

/* Respecte l'intervalle minimal entre uploads galerie (rate-limit serveur par IP). */
void syncUploadRateLimitPause(uint32_t lastUploadMs) {
  if (lastUploadMs == 0) {
    return;
  }
  const uint32_t elapsed = millis() - lastUploadMs;
  if (elapsed >= SYNC_UPLOAD_MIN_INTERVAL_MS) {
    return;
  }
  const uint32_t waitMs = SYNC_UPLOAD_MIN_INTERVAL_MS - elapsed;
  Serial.printf("[SYNC] pause rate-limit %u ms\n", static_cast<unsigned int>(waitMs));
  delay(waitMs);
}

bool syncUploadIsSuccess(int httpCode) {
  return httpCode == 200 || httpCode == 202;
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

uint32_t cameraSyncPeekNextPictureNumber() {
  // Réserve « logiquement » le prochain numéro SANS incrémenter le compteur NVS. Le numéro n'est
  // committé (cameraSyncCommitWrittenCount) qu'après persistance/upload confirmé (A6/A7, audit
  // 2026-07-05) : un échec d'écriture SD ou d'upload direct ne brûle plus de numéro fantôme.
  return nvsGet(kKeyCount) + 1;
}

void cameraSyncCommitWrittenCount(uint32_t n) {
  // Avance pic_count à n (photo écrite sur SD, en file d'attente). GREATEST implicite : ne recule
  // jamais si un numéro plus grand a déjà été committé entre-temps.
  if (n > nvsGet(kKeyCount)) {
    nvsSet(kKeyCount, n);
  }
}

void cameraSyncMarkDirectUploadConfirmed(uint32_t n) {
  // Upload direct (sans SD) confirmé côté serveur : la photo n'entre PAS dans la file SD, donc on
  // avance pic_count ET le curseur ensemble pour garder pending = pic_count − cursor cohérent
  // (A7 : évite que pending gonfle indéfiniment sur l'upload direct).
  if (n > nvsGet(kKeyCount)) {
    nvsSet(kKeyCount, n);
  }
  if (n > nvsGet(kKeyCursor)) {
    nvsSet(kKeyCursor, n);
  }
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
  /* Vrai backlog (NVS) au démarrage du drain : annoncé comme `total` au serveur (A2), indépendant
     du plafond de scan SD (SYNC_MAX_BACKLOG_SCAN). */
  const uint32_t realBacklog = count - cursor;

  std::vector<SyncEntry> entries;
  entries.reserve(SYNC_MAX_BACKLOG_SCAN);  // M2 : évite les réallocations/copies de l'insertion triée
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
    /* NVS annonce un backlog mais aucun fichier présent (carte changée/effacée) : on recale. */
    nvsSet(kKeyCursor, count);
    Serial.println("[SYNC] Backlog NVS mais aucun fichier present; curseur recale.");
    return r;
  }

  /* Stratégie hybride : vidage complet si le backlog dépasse le seuil, sinon drain incrémental.
     A1 : `planned` est plafonné au réel drainable par réveil (budget temps / intervalle mini ≈16)
     pour que « vidage complet » reste ATTEIGNABLE — sinon `complete` était toujours faux et le
     serveur recevait `aborted` (mail d'alerte) à chaque réveil pour tout backlog > ~16. */
  uint32_t planned;
  if (pending > cfg.fullDrainThreshold) {
    planned = pending;
    Serial.printf("[SYNC] Backlog %u > seuil %u : rattrapage (vidage complet).\n",
                  static_cast<unsigned int>(pending), static_cast<unsigned int>(cfg.fullDrainThreshold));
  } else {
    planned = (pending < cfg.maxUploadsPerWake) ? pending : cfg.maxUploadsPerWake;
  }
  if (planned > SYNC_DRAIN_MAX_UPLOADS_PER_WAKE) {
    planned = SYNC_DRAIN_MAX_UPLOADS_PER_WAKE;  // plafond réel (budget temps)
  }
  Serial.printf("[SYNC] Drain : %u/%u photo(s) ce reveil (backlog reel=%u).\n",
                static_cast<unsigned int>(planned), static_cast<unsigned int>(pending),
                static_cast<unsigned int>(realBacklog));
  r.planned = planned;

  /* Identité de session = cible + numéro le plus haut écrit (stable sur retry du même backlog). */
  const String deviceSession = String(cfg.targetName ? cfg.targetName : "cam") + "-" + String(count);
  int sessionId = 0;
  /* A2 : on annonce le VRAI backlog (realBacklog) comme `total`, pas le lot du réveil. La jauge X/Y
     reflète le restant réel et le serveur ne clôt le récap qu'une fois le backlog vidé (final=1). */
  const int startCode = sessionStart(cfg, deviceSession, realBacklog, &sessionId);
  r.ran = true;
  r.sessionId = sessionId;
  Serial.printf("[SYNC] start HTTP=%d session=%d backlog=%u planned=%u\n",
                startCode, sessionId, static_cast<unsigned int>(realBacklog), static_cast<unsigned int>(planned));
  if (startCode != 200 || sessionId <= 0) {
    Serial.println("[SYNC][WARN] Ouverture de session echouee, drain annule.");
    return r;
  }

  const String sessionStr = String(sessionId);
  const uint32_t drainStartMs = millis();
  uint32_t lastUploadMs = 0;
  for (uint32_t i = 0; i < planned && i < entries.size(); ++i) {
    if (i > 0 && (millis() - drainStartMs) >= SYNC_DRAIN_MAX_DURATION_MS) {
      Serial.printf("[SYNC] budget temps %u ms atteint, reprise au prochain reveil.\n",
                    static_cast<unsigned int>(SYNC_DRAIN_MAX_DURATION_MS));
      break;
    }

    const SyncEntry& e = entries[i];
    syncUploadRateLimitPause(lastUploadMs);

    /* M4 : buffers pile plutôt que temporaires String concaténés (alloc/frag DRAM en chemin chaud). */
    char seqStr[12];
    snprintf(seqStr, sizeof(seqStr), "%lu", static_cast<unsigned long>(e.n));
    char filename[64];
    snprintf(filename, sizeof(filename), "esp32-cam-%s-%s.jpg",
             cfg.targetName ? cfg.targetName : "cam", seqStr);

    CameraUploadParams up = {};
    up.url = cfg.uploadUrl;
    up.apiKey = cfg.apiKey;
    up.sigSecret = cfg.sigSecret;
    up.syncSession = sessionStr.c_str();
    up.capturedAt = (e.stamp[0] != '\0') ? e.stamp : "";
    up.captureSeq = seqStr;
    up.reconnect = cfg.reconnect;

    size_t bytes = 0;
    int code = cameraUploadJpegFile(up, String(e.path), String(filename), &bytes);

    for (int retry = 0; !syncUploadIsSuccess(code) && code == 429 && retry < SYNC_RATE_LIMIT_RETRIES; ++retry) {
      Serial.printf("[SYNC][WARN] #%u HTTP=429 rate-limit, attente %u ms (retry %d/%d)\n",
                    static_cast<unsigned int>(e.n),
                    static_cast<unsigned int>(SYNC_UPLOAD_MIN_INTERVAL_MS),
                    retry + 1, SYNC_RATE_LIMIT_RETRIES);
      delay(SYNC_UPLOAD_MIN_INTERVAL_MS);
      code = cameraUploadJpegFile(up, String(e.path), filename, &bytes);
    }

    lastUploadMs = millis();

    if (syncUploadIsSuccess(code)) {
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

  /* A1 : `complete` = « aucun upload en échec ce réveil » (et non « backlog vidé »). On ne remonte
     donc `aborted` que sur une vraie perte réseau en cours de drain, pas sur un report normal au
     réveil suivant ni sur l'atteinte du budget temps. */
  r.complete = (r.failed == 0);
  /* A2 : `final` = backlog réellement vidé après ce drain -> le serveur peut clore le récap. */
  const uint32_t remaining = cameraSyncPendingCount();
  const bool final = (remaining == 0);
  const int finishCode = sessionFinish(cfg, sessionId, r.sent, r.failed, r.bytes, r.complete, final);
  Serial.printf("[SYNC] finish HTTP=%d sent=%u failed=%u bytes=%u complete=%d final=%d restant=%u\n",
                finishCode, static_cast<unsigned int>(r.sent), static_cast<unsigned int>(r.failed),
                static_cast<unsigned int>(r.bytes), r.complete ? 1 : 0, final ? 1 : 0,
                static_cast<unsigned int>(remaining));
#else
  (void)cfg;
#endif
  return r;
}
