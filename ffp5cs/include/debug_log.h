#pragma once

/**
 * @file debug_log.h
 * @brief Utilitaire de logging pour le mode debug (NDJSON format)
 * 
 * Ce fichier fournit des macros et fonctions pour instrumenter le code
 * avec des logs structurés au format NDJSON, utilisés par l'outil de debug.
 * 
 * MODE PAR DÉFAUT: Serial (recommandé)
 * - Les logs NDJSON sont envoyés sur Serial
 * - Capturez-les avec: pio device monitor | Select-String '^\{' | Out-File -Append .cursor\debug.log
 * 
 * MODE ALTERNATIF: LittleFS (si besoin)
 * - Définissez DEBUG_LOG_USE_FS avant d'inclure ce fichier
 * - Les logs sont écrits dans /debug.log sur LittleFS
 * 
 * @note Ne pas utiliser en production - uniquement pour le debug
 */

#include <Arduino.h>
#include <cstring>

// Mode de logging: Serial (défaut) ou LittleFS
#ifndef DEBUG_LOG_USE_FS
  #define DEBUG_LOG_USE_SERIAL 1
#else
  #include <LittleFS.h>
  #define DEBUG_LOG_FS_PATH "/debug.log"
#endif

// Taille max d'un buffer JSON pour les données
#define DEBUG_LOG_JSON_BUF_SIZE 256

namespace DebugLog {

#if DEBUG_LOG_USE_SERIAL

/**
 * @brief Écrit un log NDJSON sur Serial (mode par défaut)
 * 
 * @param location Emplacement du code (ex: "app.cpp:142")
 * @param message Message descriptif
 * @param dataJson Données JSON optionnelles (peut être nullptr)
 * @param sessionId ID de session (défaut: "debug-session")
 * @param runId ID du run (défaut: "run1")
 * @param hypothesisId ID de l'hypothèse testée (ex: "A", "B")
 * 
 * @note Les logs sont envoyés sur Serial au format NDJSON (une ligne par log)
 *       Capturez-les avec: pio device monitor | Select-String '^\{' | Out-File -Append .cursor\debug.log
 */
inline void log(const char* location, 
                const char* message,
                const char* dataJson = nullptr,
                const char* sessionId = "debug-session",
                const char* runId = "run1",
                const char* hypothesisId = "") {
  // Construire l'objet JSON minimal (format NDJSON)
  Serial.print("{\"timestamp\":");
  Serial.print(millis());
  Serial.print(",\"location\":\"");
  Serial.print(location);
  Serial.print("\",\"message\":\"");
  // Échapper les guillemets dans le message
  for (const char* p = message; *p; p++) {
    if (*p == '"' || *p == '\\') {
      Serial.print('\\');
    }
    Serial.print(*p);
  }
  Serial.print("\"");
  
  // Ajouter les données JSON si fournies
  if (dataJson && strlen(dataJson) > 0) {
    Serial.print(",\"data\":");
    Serial.print(dataJson);
  }
  
  // Ajouter sessionId
  if (sessionId && strlen(sessionId) > 0) {
    Serial.print(",\"sessionId\":\"");
    Serial.print(sessionId);
    Serial.print("\"");
  }
  
  // Ajouter runId
  if (runId && strlen(runId) > 0) {
    Serial.print(",\"runId\":\"");
    Serial.print(runId);
    Serial.print("\"");
  }
  
  // Ajouter hypothesisId
  if (hypothesisId && strlen(hypothesisId) > 0) {
    Serial.print(",\"hypothesisId\":\"");
    Serial.print(hypothesisId);
    Serial.print("\"");
  }
  
  Serial.println("}");
}

#else // DEBUG_LOG_USE_FS

/**
 * @brief Vérifie si LittleFS est monté et disponible
 * @return true si LittleFS est disponible, false sinon
 */
inline bool isFSAvailable() {
  // LittleFS est monté dans bootstrap_storage.cpp
  // On vérifie juste si on peut ouvrir un fichier
  File test = LittleFS.open("/", "r");
  if (test) {
    test.close();
    return true;
  }
  return false;
}

/**
 * @brief Écrit un log NDJSON dans le fichier /debug.log (mode LittleFS)
 * 
 * @param location Emplacement du code (ex: "app.cpp:142")
 * @param message Message descriptif
 * @param dataJson Données JSON optionnelles (peut être nullptr)
 * @param sessionId ID de session (défaut: "debug-session")
 * @param runId ID du run (défaut: "run1")
 * @param hypothesisId ID de l'hypothèse testée (ex: "A", "B")
 * 
 * @note Cette fonction échoue silencieusement si LittleFS n'est pas disponible
 *       pour ne pas bloquer le système en cas de problème
 */
inline void log(const char* location, 
                const char* message,
                const char* dataJson = nullptr,
                const char* sessionId = "debug-session",
                const char* runId = "run1",
                const char* hypothesisId = "") {
  // Vérifier que LittleFS est disponible
  if (!isFSAvailable()) {
    return; // Skip silencieusement si FS non disponible
  }
  
  File f = LittleFS.open(DEBUG_LOG_FS_PATH, "a"); // mode append
  if (!f) {
    return; // Échec ouverture, skip silencieusement
  }
  
  // Construire l'objet JSON minimal (format NDJSON)
  f.print("{\"timestamp\":");
  f.print(millis());
  f.print(",\"location\":\"");
  f.print(location);
  f.print("\",\"message\":\"");
  // Échapper les guillemets dans le message
  for (const char* p = message; *p; p++) {
    if (*p == '"' || *p == '\\') {
      f.print('\\');
    }
    f.print(*p);
  }
  f.print("\"");
  
  // Ajouter les données JSON si fournies
  if (dataJson && strlen(dataJson) > 0) {
    f.print(",\"data\":");
    f.print(dataJson);
  }
  
  // Ajouter sessionId
  if (sessionId && strlen(sessionId) > 0) {
    f.print(",\"sessionId\":\"");
    f.print(sessionId);
    f.print("\"");
  }
  
  // Ajouter runId
  if (runId && strlen(runId) > 0) {
    f.print(",\"runId\":\"");
    f.print(runId);
    f.print("\"");
  }
  
  // Ajouter hypothesisId
  if (hypothesisId && strlen(hypothesisId) > 0) {
    f.print(",\"hypothesisId\":\"");
    f.print(hypothesisId);
    f.print("\"");
  }
  
  f.println("}");
  f.close();
}

#endif // DEBUG_LOG_USE_SERIAL

#if !DEBUG_LOG_USE_SERIAL

/**
 * @brief Nettoie le fichier de log (supprime toutes les entrées)
 * 
 * @return true si le nettoyage a réussi, false sinon
 */
inline bool clear() {
  if (!isFSAvailable()) {
    return false;
  }
  
  if (LittleFS.exists(DEBUG_LOG_FS_PATH)) {
    return LittleFS.remove(DEBUG_LOG_FS_PATH);
  }
  return true; // Fichier n'existe pas = déjà propre
}

/**
 * @brief Obtient la taille du fichier de log
 * 
 * @return Taille en bytes, ou 0 si erreur
 */
inline size_t getSize() {
  if (!isFSAvailable()) {
    return 0;
  }
  
  if (!LittleFS.exists(DEBUG_LOG_FS_PATH)) {
    return 0;
  }
  
  File f = LittleFS.open(DEBUG_LOG_FS_PATH, "r");
  if (!f) {
    return 0;
  }
  
  size_t size = f.size();
  f.close();
  return size;
}

#endif // !DEBUG_LOG_USE_SERIAL

} // namespace DebugLog

// =============================================================================
// MACROS DE CONVENANCE
// =============================================================================

/**
 * @brief Macro simple pour logger un message avec données JSON
 * 
 * @param loc Emplacement (ex: __FILE__ ":" STRINGIFY(__LINE__))
 * @param msg Message descriptif
 * @param jsonData Données JSON (chaîne C valide, ex: "{\"heap\":50000}")
 * @param hypId ID de l'hypothèse (ex: "A")
 * 
 * @example
 *   DEBUG_LOG_DATA("app.cpp:142", "Function entry", "{\"heapFree\":50000}", "A");
 */
#define DEBUG_LOG_DATA(loc, msg, jsonData, hypId) \
  do { \
    DebugLog::log(loc, msg, jsonData, "debug-session", "run1", hypId); \
  } while(0)

/**
 * @brief Macro pour logger avec formatage printf
 * 
 * @param loc Emplacement
 * @param fmt Format printf
 * @param ... Arguments printf
 * 
 * @example
 *   DEBUG_LOG_FMT("app.cpp:142", "Heap: %u bytes", ESP.getFreeHeap());
 */
#define DEBUG_LOG_FMT(loc, fmt, ...) \
  do { \
    char _buf[DEBUG_LOG_JSON_BUF_SIZE]; \
    snprintf(_buf, sizeof(_buf), fmt, ##__VA_ARGS__); \
    DebugLog::log(loc, _buf); \
  } while(0)

/**
 * @brief Macro pour logger avec données structurées (format automatique)
 * 
 * @param loc Emplacement
 * @param msg Message
 * @param hypId ID hypothèse
 * @param ... Paires clé-valeur (ex: "heapFree", 50000, "heapMin", 45000)
 * 
 * @example
 *   DEBUG_LOG_STRUCT("app.cpp:142", "Memory check", "A", 
 *                    "heapFree", ESP.getFreeHeap(), 
 *                    "heapMin", ESP.getMinFreeHeap());
 */
#define DEBUG_LOG_STRUCT(loc, msg, hypId, ...) \
  do { \
    char _jsonBuf[DEBUG_LOG_JSON_BUF_SIZE]; \
    _jsonBuf[0] = '{'; \
    int _pos = 1; \
    const char* _keys[] = {__VA_ARGS__}; \
    int _count = sizeof(_keys) / sizeof(_keys[0]); \
    for (int _i = 0; _i < _count - 1; _i += 2) { \
      if (_i > 0) { \
        _pos += snprintf(_jsonBuf + _pos, sizeof(_jsonBuf) - _pos, ","); \
      } \
      _pos += snprintf(_jsonBuf + _pos, sizeof(_jsonBuf) - _pos, "\"%s\":", _keys[_i]); \
      if (strchr(_keys[_i + 1], '"') == nullptr) { \
        _pos += snprintf(_jsonBuf + _pos, sizeof(_jsonBuf) - _pos, "%s", _keys[_i + 1]); \
      } else { \
        _pos += snprintf(_jsonBuf + _pos, sizeof(_jsonBuf) - _pos, "\"%s\"", _keys[_i + 1]); \
      } \
    } \
    _pos += snprintf(_jsonBuf + _pos, sizeof(_jsonBuf) - _pos, "}"); \
    if (_pos < (int)sizeof(_jsonBuf)) { \
      DebugLog::log(loc, msg, _jsonBuf, "debug-session", "run1", hypId); \
    } \
  } while(0)

/**
 * @brief Macro simplifiée pour logger juste un message
 * 
 * @param loc Emplacement
 * @param msg Message
 */
#define DEBUG_LOG_SIMPLE(loc, msg) \
  DebugLog::log(loc, msg)

// =============================================================================
// MACRO POUR CRÉER DES RÉGIONS PLIABLES DANS L'ÉDITEUR
// =============================================================================

/**
 * @brief Démarre une région de logs de debug (pour auto-fold dans l'éditeur)
 * 
 * Utilisez cette macro avant vos logs de debug pour que l'éditeur
 * puisse les masquer automatiquement.
 */
#define DEBUG_LOG_REGION_START() \
  // #region agent log

/**
 * @brief Termine une région de logs de debug
 */
#define DEBUG_LOG_REGION_END() \
  // #endregion agent log
