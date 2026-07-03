#include "camera_uploader.h"

#include <WiFi.h>
#include <HTTPClient.h>
#if defined(USE_HTTPS_ENDPOINTS)
#include <WiFiClientSecure.h>
#endif

#include "config.h"
#include "camera_upload.h"

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

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
  WiFiClientSecure client;
  client.setInsecure();
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
