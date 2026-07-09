// v13.80 (audit) - Implementation HMAC-SHA256 pour contrat firmware <-> serveur.
// Voir hmac_sign.h pour la documentation. Utilise mbedtls/md.h (présent dans ESP-IDF).

#include "hmac_sign.h"
#include <cstring>
#include "config.h"  // Secrets::API_SIG_SECRET si défini, Arduino String évité
#include "n3_hmac_canonical.h"  // calcul mutualisé (shared/n3_hmac) — même algo, sans String

namespace HmacSign {

const char* getSecret() {
#if defined(SECRETS_INCLUDE_API_SIG_SECRET) || defined(API_SIG_SECRET_DEFINED)
    // v13.80 : Secrets::API_SIG_SECRET est défini dans secrets_config.h si
    // l'utilisateur a configuré la signature HMAC (cf. secrets_config.h.example).
    return Secrets::API_SIG_SECRET;
#else
    return nullptr;
#endif
}

bool isEnabled() {
    const char* s = getSecret();
    if (s == nullptr) return false;
    if (s[0] == '\0') return false;
    // Refuser le placeholder pour éviter d'envoyer une signature triviale.
    if (strcmp(s, "CHANGEZ_MOI") == 0) return false;
    return true;
}

// generateNonce / computeHmacHex délèguent désormais à la brique canonique
// mutualisée (shared/n3_hmac/n3_hmac_canonical) : même algorithme (mbedtls
// incrémental, ts + '\n' + nonce + '\n' + body), sans Arduino String. Les
// constantes HmacSign::* (buffers) sont alignées sur n3hmac::* (64/65/16/17).
// getSecret()/isEnabled() restent ici (couplage Secrets::API_SIG_SECRET).

void generateNonce(char* nonceOut, size_t nonceBufferSize) {
    n3hmac::generateNonce(nonceOut, nonceBufferSize);
}

bool computeHmacHex(const char* secret,
                    const char* timestamp,
                    const char* nonce,
                    const char* body,
                    char* hmacHexOut,
                    size_t hmacBufferSize) {
    return n3hmac::computeHmacHex(secret, timestamp, nonce, body,
                                  hmacHexOut, hmacBufferSize);
}

}  // namespace HmacSign