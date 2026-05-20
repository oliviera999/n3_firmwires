#pragma once

// =============================================================================
// FFP5CS - Board / Profile traits (constexpr)
// =============================================================================
// v13.60 (audit général 2026-05) : helpers `constexpr` pour réduire la pollution
// des `#if defined(BOARD_S3)` / `BOARD_HAS_PSRAM` / `PROFILE_*` éparpillés dans
// le code. Permet d'écrire `if constexpr (BoardTraits::isS3()) { ... }` au lieu
// d'une cascade de directives préprocesseur.
//
// IMPORTANT : ne remplace pas les `#if` partout. Certaines parties (ex. inclusions
// d'en-têtes spécifiques S3, déclarations `extern` plateforme) doivent rester
// préprocesseur. Utiliser ces helpers principalement dans les *implémentations*
// (`.cpp`) où le code « commun » varie sur des constantes ou un branchement.
// =============================================================================

namespace BoardTraits {

// --- Cible matérielle ---
constexpr bool isS3() {
#if defined(BOARD_S3)
    return true;
#else
    return false;
#endif
}

constexpr bool isWroom() {
#if defined(BOARD_WROOM)
    return true;
#else
    return false;
#endif
}

constexpr bool hasPsram() {
#if defined(BOARD_HAS_PSRAM)
    return true;
#else
    return false;
#endif
}

// --- Profil de build ---
constexpr bool isProd() {
#if defined(PROFILE_PROD)
    return true;
#else
    return false;
#endif
}

constexpr bool isBeta() {
#if defined(PROFILE_BETA)
    return true;
#else
    return false;
#endif
}

constexpr bool isTest() {
#if defined(PROFILE_TEST)
    return true;
#else
    return false;
#endif
}

constexpr bool isDev() {
#if defined(PROFILE_DEV)
    return true;
#else
    return false;
#endif
}

// Profil « non-production » : test / beta / dev / unknown.
// Pratique pour activer logs verbeux, endpoints debug, etc.
constexpr bool isNonProd() {
    return !isProd();
}

// --- Endpoints serveur ---
constexpr bool useTestEndpoints() {
#if defined(USE_TEST_ENDPOINTS)
    return true;
#else
    return false;
#endif
}

constexpr bool useTest3Endpoints() {
#if defined(USE_TEST3_ENDPOINTS)
    return true;
#else
    return false;
#endif
}

constexpr bool useLocalServerEndpoints() {
#if defined(USE_LOCAL_SERVER_ENDPOINTS)
    return true;
#else
    return false;
#endif
}

// --- Web ---
constexpr bool dangerousEndpointsEnabled() {
#if defined(FFP_ENABLE_DANGEROUS_ENDPOINTS)
    return true;
#else
    return false;
#endif
}

constexpr bool asyncWebServerDisabled() {
#if defined(DISABLE_ASYNC_WEBSERVER)
    return true;
#else
    return false;
#endif
}

}  // namespace BoardTraits