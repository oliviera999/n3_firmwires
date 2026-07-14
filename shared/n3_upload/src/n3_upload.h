#pragma once
// =============================================================================
// n3_upload — POST HTTP multipart streamé d'un gros binaire
// =============================================================================
// Mutualisé depuis uploadphotosserver (camera_uploader.cpp : doMultipartPostOnce
// + boucle de retry). Poste `head + corps + tail` en multipart/form-data SANS
// jamais allouer le corps complet (N3MultipartReader sur N3UploadSource), avec
// retries bornés, reconnexion WiFi optionnelle entre tentatives, TLS opt-in
// (USE_HTTPS_ENDPOINTS + épinglage CA via n3_data_ca_cert.h, même mécanique que
// n3_data), et raccord statistiques réseau par callback (→ n3NetStatsRecordPost)
// SANS dépendance dure sur n3_data.
//
// Les en-têtes DYNAMIQUES par tentative (ex. signature HMAC X-Sig-* avec
// timestamp/nonce frais) sont posés par le callback onBeforeSend, invoqué à
// CHAQUE tentative après les en-têtes standards — parité avec l'original qui
// recalculait la signature par tentative.
// =============================================================================

#include <Arduino.h>

#include "n3_upload_source.h"

class HTTPClient;

/** En-tête statique additionnel (identique à chaque tentative). */
struct N3UploadHeader {
  const char* name;
  const char* value;
};

/** Paramètres du form-data (l'ex-spécifique caméra devient paramétrable). */
struct N3UploadMultipartPart {
  const char* boundary;     // ex. "RandomNerdTutorials"
  const char* fieldName;    // ex. "imageFile"
  const char* filename;     // ex. "esp32-cam-msp1-42.jpg"
  const char* contentType;  // ex. "image/jpeg"
};

struct N3UploadConfig {
  const char* url;                 // URL complète d'upload
  const char* apiKey;              // en-tête X-Api-Key ; nullptr = pas d'auth
  const N3UploadHeader* headers;   // en-têtes statiques additionnels (peut être nullptr)
  size_t headerCount;
  /** En-têtes dynamiques par tentative (signature HMAC…) ; nullptr = aucun. */
  void (*onBeforeSend)(HTTPClient& http, void* ctx);
  void* onBeforeSendCtx;
  uint8_t connectRetries;          // nombre TOTAL de tentatives (>= 1)
  uint16_t retryDelayMs;           // pause entre tentatives échouées (réseau)
  uint32_t responseTimeoutMs;      // timeout client + réponse HTTP
  bool (*reconnect)();             // reconnexion WiFi entre tentatives ; nullptr = aucune
  /** Statistiques réseau (une par tentative) ; câbler sur n3NetStatsRecordPost. */
  void (*onStats)(int httpCode, unsigned long durationMs, int rssi);
};

/**
 * POST multipart streamé. La source est rembobinée (rewind) avant chaque
 * tentative. Retourne le code HTTP (>0) ou une valeur négative (erreur réseau
 * après épuisement des tentatives).
 */
int n3UploadMultipart(const N3UploadConfig& cfg, const N3UploadMultipartPart& part,
                      N3UploadSource& body);
