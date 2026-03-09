#include "n3_hmac.h"
#include <HTTPClient.h>
#include "mbedtls/md.h"

bool n3HmacSha256(const char* key, const char* message, char* hexOutput, size_t hexOutputSize) {
  if (hexOutputSize < 65) return false;

  uint8_t hmacResult[32];
  mbedtls_md_context_t ctx;
  mbedtls_md_init(&ctx);

  const mbedtls_md_info_t* info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
  if (!info) { mbedtls_md_free(&ctx); return false; }

  int ret = mbedtls_md_setup(&ctx, info, 1);
  if (ret != 0) { mbedtls_md_free(&ctx); return false; }

  ret = mbedtls_md_hmac_starts(&ctx, (const unsigned char*)key, strlen(key));
  if (ret != 0) { mbedtls_md_free(&ctx); return false; }

  ret = mbedtls_md_hmac_update(&ctx, (const unsigned char*)message, strlen(message));
  if (ret != 0) { mbedtls_md_free(&ctx); return false; }

  ret = mbedtls_md_hmac_finish(&ctx, hmacResult);
  mbedtls_md_free(&ctx);
  if (ret != 0) return false;

  for (int i = 0; i < 32; i++) {
    sprintf(hexOutput + (i * 2), "%02x", hmacResult[i]);
  }
  hexOutput[64] = '\0';
  return true;
}

void n3HmacSignRequest(HTTPClient& http, const char* apiKey, const char* body) {
  char signature[65];
  if (n3HmacSha256(apiKey, body, signature, sizeof(signature))) {
    http.addHeader("X-Signature", signature);
  }
}
