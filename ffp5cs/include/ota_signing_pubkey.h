#pragma once

// =============================================================================
// FFP5CS — Clé publique ECDSA pour vérification de signature OTA
// =============================================================================
// Vérifie l'authenticité du binaire OTA (sha256 signé) AVANT de marquer la
// partition de boot. Le binaire transitant en HTTP (cf. ota_config.h), seule la
// signature garantit qu'il provient bien du serveur n3_serveur.
//
// CE FICHIER EST COMMITABLE (clé PUBLIQUE uniquement).
//
// ⚠️ SOURCE UNIQUE : cette clé DOIT rester identique à
//    shared/n3_common/src/n3_ota_pubkey.h (OTA_SIGNING_PUBLIC_KEY_PEM), généré
//    par scripts/generate_ota_keys.ps1. La clé privée n'est jamais commitée.
//    En cas de rotation de clé, mettre à jour les DEUX fichiers.
//
// Vérification : mbedtls_pk_parse_public_key + mbedtls_pk_verify (MBEDTLS_MD_SHA256).
// =============================================================================

static const char OTA_SIGNING_PUBLIC_KEY_PEM[] =
    "-----BEGIN PUBLIC KEY-----\n"
    "MIGbMBAGByqGSM49AgEGBSuBBAAjA4GGAAQA3w/Kj4IU2YWY9bd3OAc7/hEZLSPq\n"
    "G9Jm+zMdeCzksIiwbsQC/lL9gw9tUrNmC4PW5x3g8gZFyzjLpkpsvBArqawB20Fk\n"
    "i/2X5gJ8b7zSHdao+lcvoMCiT7N+GyB1M47ExRsRLOgeK7ScR70NwzNOIol45wpV\n"
    "EMEe4sZr4ipzen9R9t0=\n"
    "-----END PUBLIC KEY-----\n";
