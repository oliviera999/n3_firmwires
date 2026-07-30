#include "camera_uploader.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>

#include "config.h"
#include "n3_log.h"
#include "n3_data.h"            // n3NetStatsRecordPost (stats réseau des uploads)
#include "n3_hmac_canonical.h"  // HMAC canonique mutualisé (ts+nonce+body, sans String)
#include "n3_upload.h"          // POST multipart streamé mutualisé (v2.66, T3b)
#include "n3_upload_source.h"

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

// v2.66 (mutualisation T3b) : le POST multipart (TLS opt-in, retries, streaming
// head+corps+tail sans malloc du JPEG) vit désormais dans shared/n3_upload
// (transposition fidèle de l'ancien doMultipartPostOnce + Multipart*Stream).
// Ce fichier ne garde que le MÉTIER caméra : contrat form-data historique
// (boundary RandomNerdTutorials, champ imageFile), en-têtes X-Sync-Session /
// X-Captured-At / X-Capture-Seq, signature HMAC par tentative, lecture SD.

// A4 — signature HMAC additive du POST multipart. Le corps (JPEG volumineux) n'est pas signable en
// streaming : on signe un condensé STABLE `timestamp\n nonce\n api_key` transporté en en-têtes
// X-Sig-Timestamp / X-Sig-Nonce / X-Sig-Hmac (mêmes en-têtes que shared/n3_data, validés côté serveur
// par SignatureValidator::isValidForBody). Rétro-compatible : si API_SIG_SECRET est vide ou l'horloge
// non fiable, aucun en-tête n'est ajouté et l'auth retombe sur la seule clé API.
static void cameraUploadAddSignatureHeaders(HTTPClient& http, const CameraUploadParams& params) {
  if (params.sigSecret == nullptr || params.sigSecret[0] == '\0') {
    return;
  }
  const time_t nowTs = time(nullptr);
  if (static_cast<unsigned long>(nowTs) < 1600000000UL) {  // horloge non synchronisée : on n'ajoute rien
    return;
  }
  char tsBuf[16];
  snprintf(tsBuf, sizeof(tsBuf), "%lu", static_cast<unsigned long>(nowTs));
  static uint32_t s_camSigCounter = 0;
  char nonceBuf[32];
  snprintf(nonceBuf, sizeof(nonceBuf), "%lu-%lu",
           static_cast<unsigned long>(nowTs), static_cast<unsigned long>(++s_camSigCounter));
  // Signe le condensé `timestamp\n nonce\n api_key` (corps multipart non signable
  // en streaming). Même calcul que la lib partagée, sans construire de message.
  char sigHex[65];
  if (n3hmac::computeHmacHex(params.sigSecret, tsBuf, nonceBuf,
                             params.apiKey ? params.apiKey : "", sigHex, sizeof(sigHex))) {
    http.addHeader("X-Sig-Timestamp", tsBuf);
    http.addHeader("X-Sig-Nonce", nonceBuf);
    http.addHeader("X-Sig-Hmac", sigHex);
  }
}

// En-têtes dynamiques posés à CHAQUE tentative (parité avec l'ancien
// doMultipartPostOnce qui recalculait la signature par tentative) : signature
// HMAC fraîche + en-têtes de session/horodatage/séquence.
static void cameraUploadBeforeSend(HTTPClient& http, void* ctx) {
  const CameraUploadParams& params = *static_cast<const CameraUploadParams*>(ctx);
  cameraUploadAddSignatureHeaders(http, params);
  if (params.syncSession && params.syncSession[0] != '\0') {
    http.addHeader("X-Sync-Session", params.syncSession);
  }
  if (params.capturedAt && params.capturedAt[0] != '\0') {
    http.addHeader("X-Captured-At", params.capturedAt);
  }
  if (params.captureSeq && params.captureSeq[0] != '\0') {
    http.addHeader("X-Capture-Seq", params.captureSeq);
  }
}

static N3UploadConfig buildUploadConfig(const CameraUploadParams& params) {
  N3UploadConfig cfg = {};
  cfg.url = params.url;
  cfg.apiKey = params.apiKey;
  cfg.onBeforeSend = &cameraUploadBeforeSend;
  cfg.onBeforeSendCtx = const_cast<CameraUploadParams*>(&params);
  cfg.connectRetries = UPLOAD_CONNECT_RETRIES;
  cfg.retryDelayMs = UPLOAD_RETRY_DELAY_MS;
  cfg.responseTimeoutMs = HTTP_RESPONSE_TIMEOUT_MS;
  cfg.reconnect = params.reconnect;
  cfg.onStats = &n3NetStatsRecordPost;  // les uploads photo entrent enfin dans les stats réseau
  return cfg;
}

static N3UploadMultipartPart buildPart(const char* filename) {
  N3UploadMultipartPart part = {};
  part.boundary = "RandomNerdTutorials";
  part.fieldName = "imageFile";
  part.filename = filename;
  part.contentType = "image/jpeg";
  return part;
}

int cameraUploadJpegBuffer(const CameraUploadParams& params, const uint8_t* image, size_t imageLen, const String& filename) {
  if (!image || imageLen == 0) {
    return -1;
  }
  const N3UploadConfig cfg = buildUploadConfig(params);
  const N3UploadMultipartPart part = buildPart(filename.c_str());
  N3UploadBufferSource source(image, imageLen);
  return n3UploadMultipart(cfg, part, source);
}

#if USE_SD
namespace {
// Source n3_upload sur fichier SD : lecture chunkée (UPLOAD_CHUNK_SIZE), rewind
// par seek(0) — équivalent de l'ancien MultipartFileStream + file.seek(0) par retry.
class SdFileSource : public N3UploadSource {
 public:
  SdFileSource(File& file, size_t len) : _file(file), _len(len) {}
  size_t size() const override { return _len; }
  void rewind() override { _file.seek(0); }
  size_t readChunk(uint8_t* dst, size_t maxLen) override {
    if (!dst || maxLen == 0) return 0;
    size_t chunk = maxLen;
    if (chunk > static_cast<size_t>(UPLOAD_CHUNK_SIZE)) chunk = UPLOAD_CHUNK_SIZE;
    return _file.read(dst, chunk);
  }

 private:
  File& _file;
  size_t _len;
};
}  // namespace
#endif

int cameraUploadJpegFile(const CameraUploadParams& params, const String& sdPath, const String& filename, size_t* outBytes) {
  if (outBytes) {
    *outBytes = 0;
  }
#if USE_SD
  fs::FS& fs = SD_MMC;
  File file = fs.open(sdPath.c_str(), FILE_READ);
  if (!file) {
    N3_LOGE("[UPLOAD][SD] Ouverture %s impossible", sdPath.c_str());
    return CAMERA_UPLOAD_LOCAL_ERROR;
  }
  const size_t len = file.size();
  if (len == 0) {
    file.close();
    N3_LOGE("[UPLOAD][SD] Fichier %s vide (0 octet)", sdPath.c_str());
    return CAMERA_UPLOAD_LOCAL_ERROR;
  }

  const uint32_t heapFree = ESP.getFreeHeap();
  N3_LOGI("[UPLOAD][SD] fichier=%u bytes heap_libre=%u",
          static_cast<unsigned int>(len), static_cast<unsigned int>(heapFree));
  if (len > heapFree) {
    N3_LOGI("[UPLOAD][SD] upload streaming (pas de malloc JPEG complet)");
  }

  const N3UploadConfig cfg = buildUploadConfig(params);
  const N3UploadMultipartPart part = buildPart(filename.c_str());
  SdFileSource source(file, len);
  const int httpCode = n3UploadMultipart(cfg, part, source);
  file.close();
  if (httpCode > 0 && outBytes) {
    *outBytes = len;
  }
  return httpCode;
#else
  (void)params;
  (void)sdPath;
  (void)filename;
  return -1;
#endif
}
