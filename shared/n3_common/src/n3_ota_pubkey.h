#pragma once

// =============================================================================
// Clé publique ECDSA P-256 pour vérification de signature OTA
// Certificat CA pour validation TLS HTTPS
//
// Généré le : 2026-03-15
// Source    : scripts/generate_ota_keys.ps1
//
// CE FICHIER EST COMMITABLE (clé publique uniquement).
// La clé privée (scripts/ota_keys/ota_signing_key.pem) ne doit JAMAIS être
// commitée — elle est ignorée par .gitignore.
// =============================================================================

// -----------------------------------------------------------------------------
// Certificat CA pour validation TLS du serveur iot.olution.info
//
// Actuellement en mode setInsecure() : canal chiffré, certificat serveur
// non vérifié. La signature ECDSA garantit l'authenticité du firmware.
//
// Pour activer la validation complète du certificat serveur (Phase 2) :
//   1. openssl s_client -connect iot.olution.info:443 -showcerts 2>/dev/null \
//        | openssl x509 -noout -text | grep "Issuer:"
//   2. Télécharger le certificat CA racine correspondant
//   3. Remplacer le commentaire ci-dessous par :
//      #define OTA_CA_CERT R"CA(-----BEGIN CERTIFICATE-----\n...\n-----END CERTIFICATE-----\n)CA"
// -----------------------------------------------------------------------------
// OTA_CA_CERT non défini → setInsecure() (transitoire)

// -----------------------------------------------------------------------------
// Clé publique ECDSA P-256 (format SPKI/PEM)
// Vérification : mbedtls_pk_parse_public_key + mbedtls_pk_verify
// -----------------------------------------------------------------------------
#define OTA_SIGNING_PUBLIC_KEY_PEM R"OTAKEY(
-----BEGIN PUBLIC KEY-----
MIGbMBAGByqGSM49AgEGBSuBBAAjA4GGAAQA3w/Kj4IU2YWY9bd3OAc7/hEZLSPq
G9Jm+zMdeCzksIiwbsQC/lL9gw9tUrNmC4PW5x3g8gZFyzjLpkpsvBArqawB20Fk
i/2X5gJ8b7zSHdao+lcvoMCiT7N+GyB1M47ExRsRLOgeK7ScR70NwzNOIol45wpV
EMEe4sZr4ipzen9R9t0=
-----END PUBLIC KEY-----
)OTAKEY"