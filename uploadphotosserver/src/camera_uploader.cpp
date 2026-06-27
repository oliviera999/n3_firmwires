#include "camera_uploader.h"

#include <WiFi.h>
#include <HTTPClient.h>
#if defined(USE_HTTPS_ENDPOINTS)
#include <WiFiClientSecure.h>
#endif
#include <esp_heap_caps.h>

#include "config.h"
#include "camera_upload.h"  // MultipartCameraStream

#if USE_SD
#include "FS.h"
#include "SD_MMC.h"
#endif

/* Coeur commun : construit le multipart et poste, avec retries + reconnexion WiFi optionnelle. */
static int doMultipartPost(const CameraUploadParams& params, const uint8_t* image, size_t imageLen, const String& filename) {
  const String head = "--RandomNerdTutorials\r\nContent-Disposition: form-data; name=\"imageFile\"; filename=\"" +
                      filename + "\"\r\nContent-Type: image/jpeg\r\n\r\n";
  const String tail = "\r\n--RandomNerdTutorials--\r\n";
  const uint32_t totalLen = imageLen + head.length() + tail.length();

  int httpCode = -1;
  bool sentOk = false;

  for (int attempt = 1; attempt <= UPLOAD_CONNECT_RETRIES && !sentOk; ++attempt) {
    if (WiFi.status() != WL_CONNECTED) {
      if (params.reconnect) {
        Serial.println("[UPLOAD][WIFI][WARN] Deconnecte, tentative de reconnexion");
        params.reconnect();
      }
      if (WiFi.status() != WL_CONNECTED) {
        delay(UPLOAD_RETRY_DELAY_MS);
        continue;
      }
    }

#if defined(USE_HTTPS_ENDPOINTS)
    WiFiClientSecure client;
    client.setInsecure();  // chiffrement sans epinglage CA (suivi documente)
#else
    WiFiClient client;
#endif
    HTTPClient http;
    if (!http.begin(client, params.url)) {
      Serial.println("[UPLOAD][ERROR] HTTP begin a echoue");
      delay(UPLOAD_RETRY_DELAY_MS);
      continue;
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

    MultipartCameraStream multipart(head, image, imageLen, tail);
    httpCode = http.sendRequest("POST", &multipart, totalLen);
    if (httpCode > 0) {
      sentOk = true;
    } else {
      Serial.printf("[UPLOAD][ERROR] sendRequest HTTP=%d (%s)\n", httpCode, http.errorToString(httpCode).c_str());
      delay(UPLOAD_RETRY_DELAY_MS);
    }
    http.end();
  }

  return httpCode;
}

int cameraUploadJpegBuffer(const CameraUploadParams& params, const uint8_t* image, size_t imageLen, const String& filename) {
  if (!image || imageLen == 0) {
    return -1;
  }
  return doMultipartPost(params, image, imageLen, filename);
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

  /* Lecture en RAM : PSRAM en priorite (framebuffer deja libere a ce stade), repli tas interne. */
  uint8_t* buf = static_cast<uint8_t*>(heap_caps_malloc(len, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (!buf) {
    buf = static_cast<uint8_t*>(malloc(len));
  }
  if (!buf) {
    Serial.printf("[UPLOAD][SD][ERROR] malloc %u bytes echoue\n", static_cast<unsigned int>(len));
    file.close();
    return -1;
  }

  const size_t readBytes = file.read(buf, len);
  file.close();
  if (readBytes != len) {
    Serial.printf("[UPLOAD][SD][ERROR] Lecture incomplete %u/%u\n", static_cast<unsigned int>(readBytes), static_cast<unsigned int>(len));
    free(buf);
    return -1;
  }

  const int code = doMultipartPost(params, buf, len, filename);
  free(buf);
  if (code > 0 && outBytes) {
    *outBytes = len;
  }
  return code;
#else
  (void)params;
  (void)sdPath;
  (void)filename;
  return -1;
#endif
}
