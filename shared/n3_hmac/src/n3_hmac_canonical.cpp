// Implémentation de n3_hmac_canonical.h — mutualisée depuis ffp5cs/src/hmac_sign.cpp
// (computeHmacHex / generateNonce). Utilise mbedtls/md.h (présent dans ESP-IDF) et
// esp_random.h (esp_fill_random). N'inclut NI <Arduino.h> NI <HTTPClient.h> :
// linkable sans tirer HTTPClient, aucune Arduino String (budget DRAM).

#include "n3_hmac_canonical.h"

#include <mbedtls/md.h>
#include <esp_random.h>
#include <cstring>
#include <cstdio>

namespace n3hmac {

void generateNonce(char* nonceOut, size_t nonceBufferSize) {
  if (!nonceOut || nonceBufferSize < NONCE_HEX_BUFFER_SIZE) {
    if (nonceOut && nonceBufferSize > 0) nonceOut[0] = '\0';
    return;
  }
  uint8_t raw[NONCE_HEX_LEN / 2];
  esp_fill_random(raw, sizeof(raw));
  for (size_t i = 0; i < sizeof(raw); i++) {
    snprintf(nonceOut + (i * 2), 3, "%02x", (unsigned)raw[i]);
  }
  nonceOut[NONCE_HEX_LEN] = '\0';
}

bool computeHmacHex(const char* secret,
                    const char* timestamp,
                    const char* nonce,
                    const char* body,
                    char* hmacHexOut,
                    size_t hmacBufferSize) {
  if (!secret || secret[0] == '\0') return false;
  if (!timestamp || !nonce) return false;
  if (!hmacHexOut || hmacBufferSize < HMAC_HEX_BUFFER_SIZE) return false;
  if (!body) body = "";

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) return false;

  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);
  int ret = mbedtls_md_setup(&ctx, info, /*hmac=*/1);
  if (ret != 0) { mbedtls_md_free(&ctx); return false; }

  ret = mbedtls_md_hmac_starts(&ctx,
                               reinterpret_cast<const unsigned char*>(secret),
                               strlen(secret));
  if (ret != 0) { mbedtls_md_free(&ctx); return false; }

  // Update : timestamp + '\n' + nonce + '\n' + body  (aligné serveur SignatureValidator).
  const unsigned char sep = '\n';
  auto upd = [&](const char* s, size_t len) {
    return mbedtls_md_hmac_update(&ctx, reinterpret_cast<const unsigned char*>(s), len);
  };

  if (upd(timestamp, strlen(timestamp)) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, &sep, 1) != 0) { mbedtls_md_free(&ctx); return false; }
  if (upd(nonce, strlen(nonce)) != 0) { mbedtls_md_free(&ctx); return false; }
  if (mbedtls_md_hmac_update(&ctx, &sep, 1) != 0) { mbedtls_md_free(&ctx); return false; }
  if (upd(body, strlen(body)) != 0) { mbedtls_md_free(&ctx); return false; }

  unsigned char digest[32];
  ret = mbedtls_md_hmac_finish(&ctx, digest);
  mbedtls_md_free(&ctx);
  if (ret != 0) return false;

  for (size_t i = 0; i < sizeof(digest); i++) {
    snprintf(hmacHexOut + (i * 2), 3, "%02x", (unsigned)digest[i]);
  }
  hmacHexOut[HMAC_HEX_LEN] = '\0';
  return true;
}

}  // namespace n3hmac
