#pragma once

// =============================================================================
// FFP5CS - Identité OTA de la carte (canal + modèle + dossier de métadonnées)
// =============================================================================
// v15.31 — Avant, le modèle OTA ne dépendait que de la famille de puce
// (BOARD_S3 → "esp32-s3", sinon "esp32-wroom") : les bancs câblés autrement
// (PINMAP_UNIVERSAL, PINMAP_S3_CARRIER) lisaient le MÊME canal que les cartes
// historiques et auraient installé une image au mauvais câblage (relais pilotés
// par les mauvaises broches, sonde d'eau perdue) à la prochaine publication.
//
// Désormais chaque câblage non historique a sa PROPRE clé de modèle ET son
// PROPRE fichier de métadonnées (ota/<modele>/metadata.json). Tant que rien n'y
// est publié, le serveur répond 404 → aucune mise à jour (comportement sûr).
// Les cartes historiques gardent EXACTEMENT la même URL (ota/metadata.json) et
// la même clé ("esp32-s3" / "esp32-wroom") : aucun changement pour la production.
//
// Header autonome (macros seules, aucune dépendance) : testé en natif pour chaque
// combinaison de macros (test/test_ota_model_*).
// =============================================================================

namespace OtaModel {

// Canal OTA du manifeste : channels.<ENV>.<MODEL> (logique inchangée depuis v14).
#if defined(PROFILE_TEST) || defined(PROFILE_DEV) || defined(USE_TEST_ENDPOINTS)
inline constexpr const char* ENV = "test";
#else
inline constexpr const char* ENV = "prod";  // PROFILE_PROD, ou défaut sécurisé
#endif

// Modèle OTA (clé du manifeste) et sous-dossier des métadonnées (vide = historique).
#if defined(BOARD_S3) && defined(PINMAP_S3_CARRIER)
inline constexpr const char* MODEL = "esp32-s3-carrier";
inline constexpr const char* METADATA_SUBDIR = "esp32-s3-carrier/";
#elif defined(BOARD_S3) && defined(PINMAP_UNIVERSAL)
inline constexpr const char* MODEL = "esp32-s3-universal";
inline constexpr const char* METADATA_SUBDIR = "esp32-s3-universal/";
#elif !defined(BOARD_S3) && defined(PINMAP_UNIVERSAL)
inline constexpr const char* MODEL = "esp32-wroom-universal";
inline constexpr const char* METADATA_SUBDIR = "esp32-wroom-universal/";
#elif defined(BOARD_S3)
inline constexpr const char* MODEL = "esp32-s3";         // câblage historique (production)
inline constexpr const char* METADATA_SUBDIR = "";
#else
inline constexpr const char* MODEL = "esp32-wroom";      // câblage historique (production)
inline constexpr const char* METADATA_SUBDIR = "";
#endif

// true si la carte suit un câblage historique (manifeste partagé ota/metadata.json).
constexpr bool isHistoricPinmap() { return METADATA_SUBDIR[0] == '\0'; }

}  // namespace OtaModel
