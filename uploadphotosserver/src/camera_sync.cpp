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
#include "n3_log.h"
#include "n3_store_forward.h"  // n3SfDrain (boucle de drain mutualisee, v2.66)

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

bool syncUploadIsSuccess(int httpCode) {
  return httpCode == 200 || httpCode == 202;
}

// ---------------------------------------------------------------------------
// v2.66 (mutualisation T3b) : la boucle de drain (pacing rate-limit, budget
// temps, retries 429, commit-sur-succès, arrêt-sur-échec-réseau) vit désormais
// dans shared/n3_store_forward (n3SfDrain — transposition fidèle de l'ancienne
// boucle, invariants verrouillés par test_store_forward). Restent ici : le
// backend SD+curseur NVS, l'envoi métier (nommage, session, logs par photo)
// et les wrappers temps.
// ---------------------------------------------------------------------------

/* Backend n3_store_forward : énumération = `entries` (scan SD trié), commit =
 * avance du curseur NVS (couvre aussi les trous < n, parité historique). */
class SdCursorBackend : public N3SfBackend {
 public:
  explicit SdCursorBackend(const std::vector<SyncEntry>& entries) : _entries(entries) {}
  uint32_t count() override { return static_cast<uint32_t>(_entries.size()); }
  bool peek(uint32_t index, N3SfItem& out) override {
    if (index >= _entries.size()) return false;
    out = {};
    out.handle = _entries[index].n;
    out.ref = _entries[index].path;
    return true;
  }
  void commit(const N3SfItem& item) override { nvsSet(kKeyCursor, item.handle); }

 private:
  const std::vector<SyncEntry>& _entries;
};

struct DrainSendCtx {
  const CameraSyncConfig* cfg;
  const std::vector<SyncEntry>* entries;
  const char* sessionStr;
  uint32_t bytes;
};

const SyncEntry* findEntryByPath(const std::vector<SyncEntry>& entries, const char* path) {
  if (!path || path[0] == '\0') return nullptr;
  for (const SyncEntry& e : entries) {
    if (strcmp(e.path, path) == 0) return &e;
  }
  return nullptr;
}

/* Envoi d'UNE photo du backlog (métier upload) -> verdict pour n3SfDrain. */
N3SfSend drainSendOne(const N3SfItem& item, void* rawCtx) {
  DrainSendCtx& ctx = *static_cast<DrainSendCtx*>(rawCtx);
  // `handle` est le n° de curseur NVS mais n'est pas une identité unique : deux
  // captures peuvent partager ce numéro après une coupure entre écriture SD et
  // commit NVS. `ref` porte le chemin exact énuméré au scan.
  const SyncEntry* e = findEntryByPath(*ctx.entries, item.ref);
  if (!e) {
    return N3SfSend::HardFail;  // entrée introuvable : sauter, ne pas bloquer la file
  }

  /* M4 : buffers pile plutôt que temporaires String concaténés (chemin chaud). */
  char seqStr[12];
  snprintf(seqStr, sizeof(seqStr), "%lu", static_cast<unsigned long>(e->n));
  char filename[64];
  snprintf(filename, sizeof(filename), "esp32-cam-%s-%s.jpg",
           ctx.cfg->targetName ? ctx.cfg->targetName : "cam", seqStr);

  CameraUploadParams up = {};
  up.url = ctx.cfg->uploadUrl;
  up.apiKey = ctx.cfg->apiKey;
  up.sigSecret = ctx.cfg->sigSecret;
  up.syncSession = ctx.sessionStr;
  up.capturedAt = (e->stamp[0] != '\0') ? e->stamp : "";
  up.captureSeq = seqStr;
  up.reconnect = ctx.cfg->reconnect;

  size_t bytes = 0;
  const int code = cameraUploadJpegFile(up, String(e->path), String(filename), &bytes);

  if (syncUploadIsSuccess(code)) {
    ctx.bytes += bytes;
    N3_LOGI("[SYNC] #%u (%s) envoyee HTTP=%d (%u bytes)",
            static_cast<unsigned int>(e->n), e->path, code,
            static_cast<unsigned int>(bytes));
    return N3SfSend::Ok;
  }
  if (code == CAMERA_UPLOAD_LOCAL_ERROR) {
    // Fichier SD absent/vide/illisible : poison-pill — sauter pour ne pas bloquer
    // indéfiniment tout le backlog derrière (NetworkError reprendrait le même fichier).
    N3_LOGW("[SYNC] #%u (%s) fichier local inutilisable : skip HardFail",
            static_cast<unsigned int>(e->n), e->path);
    return N3SfSend::HardFail;
  }
  if (code == 429) {
    N3_LOGW("[SYNC] #%u HTTP=429 rate-limit", static_cast<unsigned int>(e->n));
    return N3SfSend::RateLimited;  // pause + retries bornés gérés par n3SfDrain
  }
  N3_LOGW("[SYNC] #%u echec HTTP=%d : arret du drain (reseau ?)",
          static_cast<unsigned int>(e->n), code);
  return N3SfSend::NetworkError;   // arrêt du drain, reprise au prochain réveil
}

uint32_t syncNowMs() { return static_cast<uint32_t>(millis()); }

/* Attente injectée dans n3SfDrain : pacing rate-limit ET pauses 429 (le log
 * "pause rate-limit" couvre désormais les deux, contenu identique). */
void syncSleepMs(uint32_t ms) {
  N3_LOGI("[SYNC] pause rate-limit %u ms", static_cast<unsigned int>(ms));
  delay(ms);
}

}  // namespace

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
    N3_LOGI("[SYNC] Aucun backlog a transferer.");
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
    // Un scan vide peut être TRANSITOIRE (SD lente au réveil, mount partiel, glitch
    // bus). Avancer up_cursor jusqu'à pic_count acquitterait localement des photos
    // que le serveur n'a jamais reçues — perte définitive du backlog. On conserve
    // le curseur ; un prochain réveil avec scan sain reprendra le drain.
    N3_LOGW("[SYNC] Backlog NVS=%u mais scan SD vide; curseur conserve=%u.",
            static_cast<unsigned int>(realBacklog), static_cast<unsigned int>(cursor));
    return r;
  }

  /* Identité de session = cible + numéro le plus haut écrit (stable sur retry du même backlog). */
  const String deviceSession = String(cfg.targetName ? cfg.targetName : "cam") + "-" + String(count);
  int sessionId = 0;
  /* A2 : on annonce le VRAI backlog (realBacklog) comme `total`, pas le lot du réveil. La jauge X/Y
     reflète le restant réel et le serveur ne clôt le récap qu'une fois le backlog vidé (final=1). */
  const int startCode = sessionStart(cfg, deviceSession, realBacklog, &sessionId);
  r.ran = true;
  r.sessionId = sessionId;
  N3_LOGI("[SYNC] start HTTP=%d session=%d backlog=%u pending=%u",
          startCode, sessionId, static_cast<unsigned int>(realBacklog),
          static_cast<unsigned int>(pending));
  if (startCode != 200 || sessionId <= 0) {
    N3_LOGW("[SYNC] Ouverture de session echouee, drain annule.");
    return r;
  }

  const String sessionStr = String(sessionId);

  /* v2.66 (T3b) : boucle de drain mutualisée (n3SfDrain, shared/n3_store_forward).
     Mêmes règles qu'avant, transposées : stratégie hybride A1 (vidage complet
     au-delà de fullDrainThreshold, sinon incrémental maxUploadsPerWake, plafonné
     SYNC_DRAIN_MAX_UPLOADS_PER_WAKE), pacing SYNC_UPLOAD_MIN_INTERVAL_MS au
     complément, budget temps SYNC_DRAIN_MAX_DURATION_MS (report ≠ échec),
     retries 429 bornés, curseur NVS avancé APRÈS acquittement uniquement.
     NB : le log « Drain : X/Y » est désormais émis après le drain (planned est
     calculé par la lib) — contenu identique, données serveur inchangées. */
  SdCursorBackend backend(entries);
  DrainSendCtx sendCtx = {};
  sendCtx.cfg = &cfg;
  sendCtx.entries = &entries;
  sendCtx.sessionStr = sessionStr.c_str();
  sendCtx.bytes = 0;

  N3SfDrainConfig sf = {};
  sf.maxItemsPerWake = cfg.maxUploadsPerWake;
  sf.fullDrainThreshold = cfg.fullDrainThreshold;
  sf.hardCapPerWake = SYNC_DRAIN_MAX_UPLOADS_PER_WAKE;
  sf.minIntervalMs = SYNC_UPLOAD_MIN_INTERVAL_MS;
  sf.maxDurationMs = SYNC_DRAIN_MAX_DURATION_MS;
  sf.rateLimitRetries = SYNC_RATE_LIMIT_RETRIES;
  sf.nowMs = &syncNowMs;
  sf.sleepMs = &syncSleepMs;

  const N3SfDrainResult dr = n3SfDrain(backend, sf, &drainSendOne, &sendCtx);
  r.planned = dr.planned;
  r.sent = dr.sent;
  r.failed = dr.failed;
  r.bytes = sendCtx.bytes;
  N3_LOGI("[SYNC] Drain : %u/%u photo(s) ce reveil (backlog reel=%u).",
          static_cast<unsigned int>(dr.planned), static_cast<unsigned int>(pending),
          static_cast<unsigned int>(realBacklog));

  /* A1 : `complete` = « aucun upload en échec ce réveil » (et non « backlog vidé »). On ne remonte
     donc `aborted` que sur une vraie perte réseau en cours de drain, pas sur un report normal au
     réveil suivant ni sur l'atteinte du budget temps. */
  r.complete = dr.complete;
  /* A2 : `final` = backlog réellement vidé après ce drain -> le serveur peut clore le récap. */
  const uint32_t remaining = cameraSyncPendingCount();
  const bool final = (remaining == 0);
  const int finishCode = sessionFinish(cfg, sessionId, r.sent, r.failed, r.bytes, r.complete, final);
  N3_LOGI("[SYNC] finish HTTP=%d sent=%u failed=%u bytes=%u complete=%d final=%d restant=%u",
          finishCode, static_cast<unsigned int>(r.sent), static_cast<unsigned int>(r.failed),
          static_cast<unsigned int>(r.bytes), r.complete ? 1 : 0, final ? 1 : 0,
          static_cast<unsigned int>(remaining));
#else
  (void)cfg;
#endif
  return r;
}
