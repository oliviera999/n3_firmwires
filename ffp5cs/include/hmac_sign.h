#pragma once
// =============================================================================
// FFP5CS - HMAC-SHA256 signature pour contrat firmware <-> serveur
// =============================================================================
// v13.80 (audit général 2026-05) : module dédié au calcul d'une signature HMAC-SHA256
// du payload POST/GET avec un secret partagé (Secrets::API_SIG_SECRET).
//
// Mode dual (v13.80) :
//   - L'ESP envoie `api_key=...` dans le body POST (legacy).
//   - L'ESP ajoute trois en-têtes HTTP : `X-Sig-Timestamp`, `X-Sig-Nonce`, `X-Sig-Hmac`.
//   - Le serveur (App\Security\SignatureValidator) accepte les deux modes.
//   - Si `Secrets::API_SIG_SECRET` n'est PAS configuré, on n'envoie pas d'en-têtes
//     (équivalent legacy api_key seul, full rétrocompat).
//
// Mode obligatoire v13.90+ : ces en-têtes deviennent requis si `API_SIG_SECRET` défini.
//
// Algorithme (aligné serveur SignatureValidator) :
//   message  = timestamp + "\n" + nonce + "\n" + body
//   hmac_hex = lower_hex( HMAC_SHA256(key=secret, message) )
//
// Implémentation : mbedtls/md.h (HMAC-SHA256), pas d'allocation dynamique.
// =============================================================================

#include <cstddef>
#include <cstdint>

namespace HmacSign {

// Taille du buffer hex pour le digest (32 octets = 64 hex + null).
inline constexpr size_t HMAC_HEX_LEN = 64;
inline constexpr size_t HMAC_HEX_BUFFER_SIZE = HMAC_HEX_LEN + 1;

// Génère un nonce aléatoire (16 hex). Buffer doit être >= NONCE_HEX_BUFFER_SIZE.
inline constexpr size_t NONCE_HEX_LEN = 16;
inline constexpr size_t NONCE_HEX_BUFFER_SIZE = NONCE_HEX_LEN + 1;
void generateNonce(char* nonceOut, size_t nonceBufferSize);

// Calcule l'HMAC-SHA256 lower-hex de `message = timestamp + "\n" + nonce + "\n" + body`.
// Retourne true si succès (digest écrit dans hmacHexOut + null term).
// `secret` doit être ASCII (Secrets::API_SIG_SECRET). `secret == nullptr` ou vide → return false.
bool computeHmacHex(const char* secret,
                    const char* timestamp,
                    const char* nonce,
                    const char* body,
                    char* hmacHexOut,
                    size_t hmacBufferSize);

// Helper : retourne true si la signature HMAC est activée (Secrets::API_SIG_SECRET défini
// et différent de placeholder). Permet aux callers de skip l'ajout des en-têtes en mode legacy.
bool isEnabled();

// Helper : récupère le secret partagé (depuis Secrets::API_SIG_SECRET si défini,
// nullptr sinon).
const char* getSecret();

}  // namespace HmacSign