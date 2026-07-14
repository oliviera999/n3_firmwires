// Implémentation de n3_upload.h — transposition fidèle de
// uploadphotosserver/src/camera_uploader.cpp (doMultipartPostOnce + boucle de
// retry de cameraUploadJpegBuffer/File) : mêmes gardes, mêmes logs [UPLOAD],
// même contrat multipart. Améliorations sans changement d'octets envoyés :
// head/tail en buffers pile (plus de String en chemin chaud) et hook onStats.

#include "n3_upload.h"

#include <WiFi.h>
#include <HTTPClient.h>
#if defined(USE_HTTPS_ENDPOINTS)
#include <WiFiClientSecure.h>
#endif

#include <climits>
#include <cstdio>

#include "n3_multipart_reader.h"

// Épinglage CA opt-in, INERTE PAR DÉFAUT — même mécanisme et même en-tête
// (n3_data_ca_cert.h) que shared/n3_data : si le firmware fournit cet en-tête
// (N3_DATA_CA_CERT_PEM) dans son include path, le certificat serveur est validé
// via setCACert() ; absent, on retombe EXACTEMENT sur setInsecure().
#if defined(USE_HTTPS_ENDPOINTS)
#  if defined(__has_include)
#    if __has_include("n3_data_ca_cert.h")
#      include "n3_data_ca_cert.h"
#      define N3_UPLOAD_HAS_CA_CERT 1
#    endif
#  endif
static void n3UploadPrepareTlsClient(WiFiClientSecure& client) {
#  if defined(N3_UPLOAD_HAS_CA_CERT)
  client.setCACert(N3_DATA_CA_CERT_PEM);
#  else
  client.setInsecure();
#  endif
}
#endif

namespace {

// Adaptateur Arduino Stream -> N3MultipartReader (HTTPClient::sendRequest
// consomme un Stream*). Mêmes no-op que les Multipart*Stream d'origine.
class MultipartBodyStream : public Stream {
 public:
  explicit MultipartBodyStream(N3MultipartReader& reader) : _r(reader) {}

  int available() override {
    const size_t remaining = _r.totalSize() - _r.position();
    return remaining > static_cast<size_t>(INT_MAX) ? INT_MAX : static_cast<int>(remaining);
  }
  int read() override {
    uint8_t b = 0;
    return _r.readBytes(reinterpret_cast<char*>(&b), 1) == 1 ? b : -1;
  }
  int peek() override { return -1; }
  void flush() override {}
  size_t write(uint8_t) override { return 0; }
  size_t write(const uint8_t*, size_t) override { return 0; }
  size_t readBytes(char* buffer, size_t length) override {
    return _r.readBytes(buffer, length);
  }

 private:
  N3MultipartReader& _r;
};

/* Une tentative POST multipart (parité doMultipartPostOnce). */
int postOnce(const N3UploadConfig& cfg, const char* contentTypeHeader,
             Stream& body, uint32_t totalLen) {
  if (WiFi.status() != WL_CONNECTED) {
    if (cfg.reconnect) {
      Serial.println("[UPLOAD][WIFI][WARN] Deconnecte, tentative de reconnexion");
      cfg.reconnect();
    }
    if (WiFi.status() != WL_CONNECTED) {
      return -1;
    }
  }

#if defined(USE_HTTPS_ENDPOINTS)
  // Le handshake TLS réserve ~16-45 Ko de DRAM : heap loguée juste avant pour
  // diagnostiquer la pression mémoire (module CAM sans PSRAM).
  Serial.printf("[UPLOAD][TLS] heap libre avant handshake=%lu bytes\n",
                static_cast<unsigned long>(ESP.getFreeHeap()));
  WiFiClientSecure client;
  n3UploadPrepareTlsClient(client);
#else
  WiFiClient client;
#endif
  HTTPClient http;
  if (!http.begin(client, cfg.url)) {
    Serial.println("[UPLOAD][ERROR] HTTP begin a echoue");
    return -1;
  }
  client.setTimeout(cfg.responseTimeoutMs);
  http.setTimeout(cfg.responseTimeoutMs);
  http.addHeader("Content-Type", contentTypeHeader);
  if (cfg.apiKey) {
    http.addHeader("X-Api-Key", cfg.apiKey);
  }
  for (size_t i = 0; i < cfg.headerCount; ++i) {
    const N3UploadHeader& h = cfg.headers[i];
    if (h.name && h.value && h.value[0] != '\0') {
      http.addHeader(h.name, h.value);
    }
  }
  if (cfg.onBeforeSend) {
    cfg.onBeforeSend(http, cfg.onBeforeSendCtx);  // en-têtes frais par tentative
  }

  const unsigned long postStartMs = millis();
  const int httpCode = http.sendRequest("POST", &body, totalLen);
  const unsigned long durationMs = millis() - postStartMs;
  if (httpCode <= 0) {
    Serial.printf("[UPLOAD][ERROR] sendRequest HTTP=%d (%s)\n", httpCode,
                  http.errorToString(httpCode).c_str());
  }
  http.end();

  if (cfg.onStats) {
    const int rssi = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -127;
    cfg.onStats(httpCode, durationMs, rssi);
  }
  return httpCode;
}

}  // namespace

int n3UploadMultipart(const N3UploadConfig& cfg, const N3UploadMultipartPart& part,
                      N3UploadSource& body) {
  if (!cfg.url || !part.boundary || !part.fieldName || !part.contentType) {
    return -1;
  }

  // head/tail multipart en buffers pile (contenu octet-identique à l'original).
  char head[256];
  const int headLen = snprintf(head, sizeof(head),
                               "--%s\r\nContent-Disposition: form-data; name=\"%s\"; "
                               "filename=\"%s\"\r\nContent-Type: %s\r\n\r\n",
                               part.boundary, part.fieldName,
                               part.filename ? part.filename : "", part.contentType);
  char tail[48];
  const int tailLen = snprintf(tail, sizeof(tail), "\r\n--%s--\r\n", part.boundary);
  if (headLen <= 0 || headLen >= static_cast<int>(sizeof(head)) ||
      tailLen <= 0 || tailLen >= static_cast<int>(sizeof(tail))) {
    Serial.println("[UPLOAD][ERROR] en-tete multipart trop long");
    return -1;  // troncature = contrat multipart cassé : refuser plutôt qu'envoyer
  }
  char contentTypeHeader[96];
  const int ctLen = snprintf(contentTypeHeader, sizeof(contentTypeHeader),
                             "multipart/form-data; boundary=%s", part.boundary);
  if (ctLen <= 0 || ctLen >= static_cast<int>(sizeof(contentTypeHeader))) {
    return -1;
  }

  N3MultipartReader reader(head, body, tail);
  const uint32_t totalLen = static_cast<uint32_t>(reader.totalSize());

  const int attempts = (cfg.connectRetries >= 1) ? cfg.connectRetries : 1;
  int httpCode = -1;
  for (int attempt = 1; attempt <= attempts && httpCode <= 0; ++attempt) {
    reader.rewind();  // source rembobinée : équivalent du Stream neuf / seek(0)
    MultipartBodyStream stream(reader);
    httpCode = postOnce(cfg, contentTypeHeader, stream, totalLen);
    if (httpCode <= 0 && attempt < attempts) {
      delay(cfg.retryDelayMs);
    }
  }
  return httpCode;
}
