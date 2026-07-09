#include "camera_uploader.h"

#include <WiFi.h>
#include <HTTPClient.h>
#include <time.h>
#if defined(USE_HTTPS_ENDPOINTS)
#include <WiFiClientSecure.h>
#endif

#include "config.h"
#include "camera_upload.h"
#include "n3_hmac.h"
#include "n3_hmac_canonical.h"  // HMAC canonique mutualisé (ts+nonce+body, sans String)

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

// A5 (audit 2026-07-05) — épinglage CA opt-in pour l'upload TLS, INERTE PAR DÉFAUT.
// Même mécanisme et même en-tête (`n3_data_ca_cert.h`) que la lib n3_data : si le firmware fournit
// cet en-tête (définissant N3_DATA_CA_CERT_PEM) dans l'include path, on valide le certificat serveur
// via setCACert() (protection MITM). Absent (cas actuel) : on retombe EXACTEMENT sur setInsecure().
#if defined(USE_HTTPS_ENDPOINTS)
#  if defined(__has_include)
#    if __has_include("n3_data_ca_cert.h")
#      include "n3_data_ca_cert.h"
#      define CAMERA_UPLOAD_HAS_CA_CERT 1
#    endif
#  endif
static void cameraUploadPrepareTlsClient(WiFiClientSecure& client) {
#  if defined(CAMERA_UPLOAD_HAS_CA_CERT)
  client.setCACert(N3_DATA_CA_CERT_PEM);
#  else
  client.setInsecure();
#  endif
}
#endif

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

static String buildMultipartHead(const String& filename) {
  return "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" +
         filename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
}

static String buildMultipartTail() {
  return "\r\n--RandomNerdTutorials--\r\n";
}

/* Une tentative POST multipart ; le Stream doit etre neuf (position 0) par retry. */
static int doMultipartPostOnce(const CameraUploadParams& params, Stream& body, uint32_t totalLen) {
  if (WiFi.status() != WL_CONNECTED) {
    if (params.reconnect) {
      Serial.println("[UPLOAD][WIFI][WARN] Deconnecte, tentative de reconnexion");
      params.reconnect();
    }
    if (WiFi.status() != WL_CONNECTED) {
      return -1;
    }
  }

#if defined(USE_HTTPS_ENDPOINTS)
  // M3 (audit 2026-07-05) : le handshake TLS réserve ~16-45 Ko de DRAM. On loge la heap libre juste
  // avant, pour diagnostiquer la pression mémoire sur module sans PSRAM (repli framebuffer CIF/DRAM).
  Serial.printf("[UPLOAD][TLS] heap libre avant handshake=%lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  WiFiClientSecure client;
  cameraUploadPrepareTlsClient(client);
#else
  WiFiClient client;
#endif
  HTTPClient http;
  if (!http.begin(client, params.url)) {
    Serial.println("[UPLOAD][ERROR] HTTP begin a echoue");
    return -1;
  }
  client.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  http.setTimeout(HTTP_RESPONSE_TIMEOUT_MS);
  http.addHeader("Content-Type", "multipart/form-data; boundary=RandomNerdTutorials");
  if (params.apiKey) {
    http.addHeader("X-Api-Key", params.apiKey);
  }
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

  const int httpCode = http.sendRequest("POST", &body, totalLen);
  if (httpCode <= 0) {
    Serial.printf("[UPLOAD][ERROR] sendRequest HTTP=%d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
  }
  http.end();
  return httpCode;
}

int cameraUploadJpegBuffer(const CameraUploadParams& params, const uint8_t* image, size_t imageLen, const String& filename) {
  if (!image || imageLen == 0) {
    return -1;
  }
  const String head = buildMultipartHead(filename);
  const String tail = buildMultipartTail();
  const uint32_t totalLen = static_cast<uint32_t>(imageLen + head.length() + tail.length());

  int httpCode = -1;
  for (int attempt = 1; attempt <= UPLOAD_CONNECT_RETRIES && httpCode <= 0; ++attempt) {
    MultipartCameraStream multipart(head, image, imageLen, tail);
    httpCode = doMultipartPostOnce(params, multipart, totalLen);
    if (httpCode <= 0) {
      delay(UPLOAD_RETRY_DELAY_MS);
    }
  }
  return httpCode;
}

int cameraUploadJpegFile(const CameraUploadParams& params, const String& sdPath, const String& filename, size_t* outBytes) {
  if (outBytes) {
    *outBytes = 0;
  }
#if USE_SD
  fs::FS& fs = SD_MMC;
  File file = fs.open(sdPath.c_str(), FILE_READ);
  if (!file) {
    Serial.printf("[UPLOAD][SD][ERROR] Ouverture %s impossible\n", sdPath.c_str());
    return -1;
  }
  const size_t len = file.size();
  if (len == 0) {
    file.close();
    return -1;
  }

  const uint32_t heapFree = ESP.getFreeHeap();
  Serial.printf("[UPLOAD][SD] fichier=%u bytes heap_libre=%u\n",
                static_cast<unsigned int>(len), static_cast<unsigned int>(heapFree));
  if (len > heapFree) {
    Serial.println("[UPLOAD][SD][INFO] upload streaming (pas de malloc JPEG complet)");
  }

  const String head = buildMultipartHead(filename);
  const String tail = buildMultipartTail();
  const uint32_t totalLen = static_cast<uint32_t>(len + head.length() + tail.length());

  int httpCode = -1;
  for (int attempt = 1; attempt <= UPLOAD_CONNECT_RETRIES && httpCode <= 0; ++attempt) {
    file.seek(0);
    MultipartFileStream multipart(head, file, len, tail);
    httpCode = doMultipartPostOnce(params, multipart, totalLen);
    if (httpCode <= 0) {
      delay(UPLOAD_RETRY_DELAY_MS);
    }
  }
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
