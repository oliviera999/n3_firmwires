#pragma once
// =============================================================================
// n3_hmac — HMAC canonique "body-signing" (timestamp + nonce + body)
// =============================================================================
// Mutualisé depuis ffp5cs/include/hmac_sign.h (computeHmacHex / generateNonce).
// Calcule HMAC-SHA256(timestamp + "\n" + nonce + "\n" + body) et pose le contrat
// des en-têtes X-Sig-Timestamp / X-Sig-Nonce / X-Sig-Hmac (validé serveur via
// SignatureValidator::isValidForBody).
//
// Différence avec n3_hmac.h (n3HmacSha256, message plat -> X-Signature) : ici le
// message est canonique (ts \n nonce \n body) et la construction est **mbedtls
// incrémentale, SANS Arduino String** (budget DRAM) — c'est la variante à
// privilégier dans les chemins chauds. Cette unité de traduction n'inclut NI
// <Arduino.h> NI <HTTPClient.h> (contrairement à n3_hmac.cpp) : linkable sans
// tirer HTTPClient.
//
// Le nonce est OPAQUE au contrat serveur (réinjecté verbatim dans X-Sig-Nonce) :
// generateNonce fournit un nonce aléatoire hex (anti-rejeu), mais chaque appelant
// peut aussi fournir son propre nonce.
//
// Le secret (API_SIG_SECRET) reste fourni par l'appelant : la gestion des Secrets
// (getSecret/isEnabled/placeholder) est spécifique à chaque firmware et n'est PAS
// mutualisée ici.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace n3hmac {

inline constexpr size_t HMAC_HEX_LEN         = 64;  // 32 octets -> 64 hex
inline constexpr size_t HMAC_HEX_BUFFER_SIZE = 65;  // + NUL
inline constexpr size_t NONCE_HEX_LEN        = 16;  // 8 octets -> 16 hex
inline constexpr size_t NONCE_HEX_BUFFER_SIZE = 17; // + NUL

// Calcule hmacHexOut = lower_hex(HMAC-SHA256(secret, timestamp + "\n" + nonce +
// "\n" + body)). body == nullptr est toléré (traité comme ""). Renvoie false sur
// paramètre invalide (secret nul/vide, timestamp/nonce nuls, buffer < 65) ou
// erreur mbedtls. La sortie est toujours terminée par NUL en cas de succès.
bool computeHmacHex(const char* secret,
                    const char* timestamp,
                    const char* nonce,
                    const char* body,
                    char* hmacHexOut,
                    size_t hmacBufferSize);

// Génère un nonce aléatoire de NONCE_HEX_LEN caractères hexadécimaux (+ NUL).
// Si le buffer est trop petit (< NONCE_HEX_BUFFER_SIZE), écrit une chaîne vide
// (si possible) et sort.
void generateNonce(char* nonceOut, size_t nonceBufferSize);

}  // namespace n3hmac
