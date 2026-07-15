#pragma once
// =============================================================================
// Sélection pure d'un artefact OTA depuis le manifeste JSON (metadata.json)
// =============================================================================
// Mutualisé depuis ffp5cs `include/ota_artifact_select.h` (tranche T4.3 du
// chantier core shared) — logique verbatim, namespace conservé. Origine :
// extrait de OTAManager::selectArtifactFromMetadata (ota_manager_validate.cpp)
// pour testabilité native (audit §3.8 : tester les fonctions pures).
//
// Aucune dépendance Arduino/réseau/mbedtls : uniquement la logique de choix de
// l'artefact selon la cascade channels[env][model] → [env][default] →
// [prod][model] → [prod][default] → fallback legacy top-level.
//
// CONTRAT : le schéma du manifeste (channels/bin_url/version/size/md5, champs
// d'intégrité sha256/signature, image filesystem_*) est figé côté serveur
// (n3_serveur ota/metadata.json). Toute dérive de cette cascade casserait la
// sélection du firmware à flasher. Testé en natif (test_ota_select, assertions
// à parité avec la suite ffp5cs d'origine + tests readIntegrityFields).
// =============================================================================

#include <ArduinoJson.h>
#include <cstddef>
#include <cstring>

namespace OtaArtifactSelect {

// Remplit out* depuis un nœud candidat `v`, avec fallback sur les champs
// top-level de `doc` si un champ manque dans le nœud. Retourne true si une URL
// valide (non vide) a été trouvée (version optionnelle dans ce mode channel).
inline bool tryFillFrom(JsonVariantConst v, const JsonDocument& doc,
                        char* outVersion, size_t versionSize,
                        char* outUrl, size_t urlSize,
                        int& outSize,
                        char* outMD5, size_t md5Size) {
  if (v.isNull()) return false;

  const char* urlStr = v["bin_url"].as<const char*>();
  const char* verStr = v["version"].as<const char*>();
  int size = v["size"].is<int>() ? v["size"].as<int>() : 0;
  const char* md5Str = v["md5"].as<const char*>();

  // Fallbacks depuis top-level si certains champs manquent
  if (!urlStr || strlen(urlStr) == 0) urlStr = doc["bin_url"].as<const char*>();
  if (!verStr || strlen(verStr) == 0) verStr = doc["version"].as<const char*>();
  if (size <= 0) size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
  if (!md5Str || strlen(md5Str) == 0) md5Str = doc["md5"].as<const char*>();

  // Nécessaire au minimum : une URL
  if (urlStr && strlen(urlStr) > 0) {
    strncpy(outUrl, urlStr, urlSize - 1);
    outUrl[urlSize - 1] = '\0';
    if (verStr) {
      strncpy(outVersion, verStr, versionSize - 1);
      outVersion[versionSize - 1] = '\0';
    } else {
      outVersion[0] = '\0';
    }
    outSize = size;
    if (md5Str) {
      strncpy(outMD5, md5Str, md5Size - 1);
      outMD5[md5Size - 1] = '\0';
    } else {
      outMD5[0] = '\0';
    }
    return true;
  }
  return false;
}

// Sélection complète. `envName` ("prod"/"test") et `modelName` ("esp32-wroom"/
// "esp32-s3") sont injectés (déterminés par macros côté firmware). Retourne true
// et remplit les out-params si un artefact est trouvé.
inline bool selectArtifact(const JsonDocument& doc,
                           const char* envName, const char* modelName,
                           char* outVersion, size_t versionSize,
                           char* outUrl, size_t urlSize,
                           int& outSize,
                           char* outMD5, size_t md5Size) {
  // 1) channels[env][model] → 2) [env][default] → 3) [prod][model] → 4) [prod][default]
  if (!doc["channels"].isNull()) {
    if (tryFillFrom(doc["channels"][envName][modelName], doc,
                    outVersion, versionSize, outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFrom(doc["channels"][envName]["default"], doc,
                    outVersion, versionSize, outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFrom(doc["channels"]["prod"][modelName], doc,
                    outVersion, versionSize, outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFrom(doc["channels"]["prod"]["default"], doc,
                    outVersion, versionSize, outUrl, urlSize, outSize, outMD5, md5Size)) return true;
  }

  // 5) Fallback legacy top-level (direct) — exige URL **et** version.
  const char* urlStr = doc["bin_url"].as<const char*>();
  const char* verStr = doc["version"].as<const char*>();
  int size = doc["size"].is<int>() ? doc["size"].as<int>() : 0;
  const char* md5Str = doc["md5"].as<const char*>();
  if (urlStr && strlen(urlStr) > 0 && verStr && strlen(verStr) > 0) {
    strncpy(outUrl, urlStr, urlSize - 1);
    outUrl[urlSize - 1] = '\0';
    strncpy(outVersion, verStr, versionSize - 1);
    outVersion[versionSize - 1] = '\0';
    outSize = size;
    if (md5Str) {
      strncpy(outMD5, md5Str, md5Size - 1);
      outMD5[md5Size - 1] = '\0';
    } else {
      outMD5[0] = '\0';
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// Image filesystem (LittleFS/SPIFFS) : même cascade, champs filesystem_url /
// filesystem_size / filesystem_md5, SANS version. Le fallback top-level exige
// seulement une URL. Extrait de OTAManager::selectFilesystemFromMetadata.
// ---------------------------------------------------------------------------

inline bool tryFillFsFrom(JsonVariantConst v, const JsonDocument& doc,
                          char* outUrl, size_t urlSize,
                          int& outSize,
                          char* outMD5, size_t md5Size) {
  if (v.isNull()) return false;

  const char* urlStr = v["filesystem_url"].as<const char*>();
  int size = v["filesystem_size"].is<int>() ? v["filesystem_size"].as<int>() : 0;
  const char* md5Str = v["filesystem_md5"].as<const char*>();

  if (!urlStr || strlen(urlStr) == 0) urlStr = doc["filesystem_url"].as<const char*>();
  if (size <= 0) size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
  if (!md5Str || strlen(md5Str) == 0) md5Str = doc["filesystem_md5"].as<const char*>();

  if (urlStr && strlen(urlStr) > 0) {
    strncpy(outUrl, urlStr, urlSize - 1);
    outUrl[urlSize - 1] = '\0';
    outSize = size;
    if (md5Str) {
      strncpy(outMD5, md5Str, md5Size - 1);
      outMD5[md5Size - 1] = '\0';
    } else {
      outMD5[0] = '\0';
    }
    return true;
  }
  return false;
}

inline bool selectFilesystem(const JsonDocument& doc,
                             const char* envName, const char* modelName,
                             char* outUrl, size_t urlSize,
                             int& outSize,
                             char* outMD5, size_t md5Size) {
  if (!doc["channels"].isNull()) {
    if (tryFillFsFrom(doc["channels"][envName][modelName], doc,
                      outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFsFrom(doc["channels"][envName]["default"], doc,
                      outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFsFrom(doc["channels"]["prod"][modelName], doc,
                      outUrl, urlSize, outSize, outMD5, md5Size)) return true;
    if (tryFillFsFrom(doc["channels"]["prod"]["default"], doc,
                      outUrl, urlSize, outSize, outMD5, md5Size)) return true;
  }

  // Fallback legacy top-level (exige seulement une URL ; pas de version pour le FS).
  const char* urlStr = doc["filesystem_url"].as<const char*>();
  int size = doc["filesystem_size"].is<int>() ? doc["filesystem_size"].as<int>() : 0;
  const char* md5Str = doc["filesystem_md5"].as<const char*>();
  if (urlStr && strlen(urlStr) > 0) {
    strncpy(outUrl, urlStr, urlSize - 1);
    outUrl[urlSize - 1] = '\0';
    outSize = size;
    if (md5Str) {
      strncpy(outMD5, md5Str, md5Size - 1);
      outMD5[md5Size - 1] = '\0';
    } else {
      outMD5[0] = '\0';
    }
    return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// v14.17 — Lecture des champs d'intégrité ÉTENDUE (sha256 / signature) du nœud
// d'artefact sélectionné. Suit EXACTEMENT la même cascade que selectArtifact()
// (channels[env][model] → [env][default] → [prod][model] → [prod][default] →
// legacy top-level) afin de lire ces deux champs OPTIONNELS sur le même nœud que
// bin_url/md5. Champs absents => out vides (rétro-compatible : fallback MD5 seul).
// Fonction additive : ne modifie pas selectArtifact() ni ses tests natifs.
// ---------------------------------------------------------------------------
inline void readIntegrityFields(const JsonDocument& doc,
                                const char* envName, const char* modelName,
                                char* outSha256, size_t sha256Size,
                                char* outSignature, size_t signatureSize) {
  if (outSha256 && sha256Size) outSha256[0] = '\0';
  if (outSignature && signatureSize) outSignature[0] = '\0';

  JsonVariantConst node;
  bool found = false;
  if (!doc["channels"].isNull()) {
    const char* envs[2] = { envName, "prod" };
    const char* models[2] = { modelName, "default" };
    for (int e = 0; e < 2 && !found; ++e) {
      for (int m = 0; m < 2 && !found; ++m) {
        JsonVariantConst n = doc["channels"][envs[e]][models[m]];
        const char* url = n["bin_url"].as<const char*>();
        if (!n.isNull() && url && strlen(url) > 0) {
          node = n;
          found = true;
        }
      }
    }
  }

  // Champ du nœud, puis fallback top-level (comme md5).
  const char* sha = found ? node["sha256"].as<const char*>() : nullptr;
  if (!sha || strlen(sha) == 0) sha = doc["sha256"].as<const char*>();
  const char* sig = found ? node["signature"].as<const char*>() : nullptr;
  if (!sig || strlen(sig) == 0) sig = doc["signature"].as<const char*>();

  if (sha && outSha256 && sha256Size) {
    strncpy(outSha256, sha, sha256Size - 1);
    outSha256[sha256Size - 1] = '\0';
  }
  if (sig && outSignature && signatureSize) {
    strncpy(outSignature, sig, signatureSize - 1);
    outSignature[signatureSize - 1] = '\0';
  }
}

}  // namespace OtaArtifactSelect
