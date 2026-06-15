#pragma once
// config_secrets.h — secrets (API_KEY/recipient/web-auth) résolus depuis
// secrets_config.h > credentials.h > placeholder, + validation PROFILE_PROD.
// Extrait de config.h (audit : découpe du god-header). Inclus en PREMIER par
// config.h car NetworkConfig/ApiConfig/WebAuthConfig/EmailConfig en dépendent.
// API_KEY : secrets_config.h > credentials.h (firmwires/) > défaut. Aligné avec msp/n3pp et serveur .env.
//
// v13.52: correction du bug auto-référence `API_KEY = API_KEY` dans la branche credentials.h.
//   credentials.h (firmwires/credentials.h) utilise `#define API_KEY "..."` (macro). L'ancien code
//   `constexpr const char* API_KEY = API_KEY;` voyait sa LHS et sa RHS toutes deux remplacées par
//   le préprocesseur en `constexpr const char* "..." = "...";`, ce qui ne compile pas.
//   On capture la valeur via un macro intermédiaire puis on `#undef` pour éviter la pollution.
#if __has_include("secrets_config.h")
    #include "secrets_config.h"
#elif __has_include("../../credentials.h")
    #include "../../credentials.h"
    #ifdef API_KEY
        #define FFP5CS_CRED_API_KEY_VALUE API_KEY
        #undef API_KEY
    #else
        #define FFP5CS_CRED_API_KEY_VALUE "CHANGEZ_MOI"
    #endif
    namespace Secrets {
        constexpr const char* API_KEY = FFP5CS_CRED_API_KEY_VALUE;
        constexpr const char* DEFAULT_RECIPIENT = "changez@moi.example";
        constexpr const char* WEB_AUTH_USER = "admin";
        constexpr const char* WEB_AUTH_PASS = "CHANGEZ_MOI";  // Remplacer dans secrets_config.h
    }
    #undef FFP5CS_CRED_API_KEY_VALUE
#else
    namespace Secrets {
        constexpr const char* API_KEY = "CHANGEZ_MOI";
        constexpr const char* DEFAULT_RECIPIENT = "changez@moi.example";
        constexpr const char* WEB_AUTH_USER = "admin";
        constexpr const char* WEB_AUTH_PASS = "CHANGEZ_MOI";  // Obligatoire : configurer dans secrets_config.h
    }
#endif

// v13.52: Helper constexpr et static_assert pour rejeter en PROFILE_PROD un secret resté
// au placeholder "CHANGEZ_MOI" (oubli de configurer secrets_config.h).
namespace SecretsValidation {
    constexpr bool strEq(const char* a, const char* b) {
        while (*a && *b && *a == *b) { ++a; ++b; }
        return *a == *b;
    }
}
#if defined(PROFILE_PROD)
static_assert(!SecretsValidation::strEq(Secrets::API_KEY, "CHANGEZ_MOI"),
    "PROFILE_PROD: Secrets::API_KEY vaut encore le placeholder \"CHANGEZ_MOI\". "
    "Configurer firmwires/ffp5cs/include/secrets_config.h avec une vraie cle API.");
#endif
