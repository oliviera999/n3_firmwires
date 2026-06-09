// Stub mbedtls/md.h pour tests natifs (hôte) — PAS de vraie crypto.
// Produit un digest déterministe (octets 0x00..0x1f) pour valider le
// formatage hex et le contrat de taille de n3HmacSha256, sans dépendre de
// mbedtls (indisponible en natif). La justesse HMAC est validée sur cible.
#pragma once
#include <stddef.h>

typedef struct { int dummy; } mbedtls_md_context_t;
typedef struct { int dummy; } mbedtls_md_info_t;
typedef enum { MBEDTLS_MD_SHA256 = 6 } mbedtls_md_type_t;

static inline void mbedtls_md_init(mbedtls_md_context_t*) {}
static inline void mbedtls_md_free(mbedtls_md_context_t*) {}
static inline const mbedtls_md_info_t* mbedtls_md_info_from_type(mbedtls_md_type_t) {
  static mbedtls_md_info_t info;
  return &info;
}
static inline int mbedtls_md_setup(mbedtls_md_context_t*, const mbedtls_md_info_t*, int) { return 0; }
static inline int mbedtls_md_hmac_starts(mbedtls_md_context_t*, const unsigned char*, size_t) { return 0; }
static inline int mbedtls_md_hmac_update(mbedtls_md_context_t*, const unsigned char*, size_t) { return 0; }
static inline int mbedtls_md_hmac_finish(mbedtls_md_context_t*, unsigned char* output) {
  for (int i = 0; i < 32; ++i) output[i] = (unsigned char)i;  // digest déterministe 00..1f
  return 0;
}
