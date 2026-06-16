#pragma once
// =============================================================================
// FFP5CS — Décisions de diagnostic (pures)
// =============================================================================
// Extrait de diagnostics.cpp (Diagnostics::update, audit §3.8). Deux logiques
// pures, 100 % string/integer, sans flottant/timing/actionneur :
//   1. parseIdlePercent : parse la sortie de vTaskGetRunTimeStats() pour estimer
//      l'occupation CPU au repos (lignes "IDLE", dernière colonne en %, moyenne).
//   2. decideMinHeapSave : décide S'IL FAUT persister minFreeHeap en NVS
//      (première sauvegarde / intervalle+delta / perte critique >10KB), avec la
//      garde premier-boot UINT32_MAX. L'écriture NVS (saveULong) et le logging
//      restent dans l'appelant.
//
// Diagnostic uniquement : aucun impact sécurité/autonomie. Logique pure
// paramétrée par valeurs (seuils en arguments) -> aucune dépendance
// Arduino.h/config.h. N'utilise que <cstdint>/<cstring>/<cstddef>.
//
// Parité : transcription ligne-à-ligne du code inline d'origine (mêmes
// comparaisons, mêmes bornes de buffer, mêmes constantes par défaut).
// =============================================================================

#include <cstdint>
#include <cstring>
#include <cstddef>

namespace DiagnosticsDecision {

// Sentinelle "inconnu" pour l'idle% (aligné sur DiagnosticStats::idlePercent).
static const uint8_t IDLE_UNKNOWN = 255;

// Constantes par défaut (mêmes valeurs que diagnostics.h / diagnostics.cpp).
// L'appelant les passe explicitement à decideMinHeapSave pour garder la parité.
static const unsigned long DEFAULT_MIN_HEAP_SAVE_INTERVAL = 600000UL; // 10 min
static const uint32_t DEFAULT_MIN_HEAP_DIFF_FOR_SAVE = 1024;          // 1 KB
static const uint32_t DEFAULT_MIN_HEAP_CRITICAL_LOSS = 10240;         // 10 KB

// -----------------------------------------------------------------------------
// Helper interne : atoi non signé minimaliste (évite <cstdlib>/locale).
// Parité : sur les sous-chaînes ne contenant QUE des chiffres '0'..'9' (seul cas
// produit par parseIdlePercent, qui isole déjà les chiffres), donne le même
// résultat que atoi() de la libc.
inline int atoiSimple(const char* s) {
  int v = 0;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    ++s;
  }
  return v;
}

// -----------------------------------------------------------------------------
// 1. Parse Idle%
// -----------------------------------------------------------------------------
// Reproduit le parse inline de Diagnostics::update (v11.156, char[] sans String).
// Cherche chaque occurrence de "IDLE" dans taskStats ; pour chaque ligne (jusqu'au
// '\n'), repère le dernier '%', remonte sur les chiffres pour extraire le nombre
// (atoi), accumule somme + compte, puis renvoie la moyenne plafonnée à 100.
// Renvoie IDLE_UNKNOWN (255) si taskStats est vide ou si aucune valeur exploitable.
//
// Note de parité : utilise les mêmes bornes de buffer que l'original
// (line[120] -> lineLen < 120, numStr[4] -> len <= 3) et std::isdigit n'est PAS
// utilisé : test direct '0'..'9' identique au isdigit() ASCII de l'original sur
// chiffres (évite la dépendance <cctype> et l'UB sur char signé négatif).
inline uint8_t parseIdlePercent(const char* taskStats) {
  uint8_t idlePct = IDLE_UNKNOWN;
  if (taskStats == nullptr) {
    return idlePct;
  }
  const size_t taskStatsLen = strlen(taskStats);
  if (taskStatsLen > 0) {
    const char* taskStatsPtr = taskStats;
    // Cherche la dernière colonne en % sur les lignes contenant "IDLE"
    uint16_t sumIdle = 0;
    uint8_t cnt = 0;
    const char* searchPtr = taskStatsPtr;
    while ((searchPtr = strstr(searchPtr, "IDLE")) != nullptr) {
      int idx = static_cast<int>(searchPtr - taskStatsPtr);
      const char* lineEndPtr = strchr(searchPtr, '\n');
      int lineEnd = (lineEndPtr != nullptr) ? static_cast<int>(lineEndPtr - taskStatsPtr)
                                            : static_cast<int>(taskStatsLen);

      // Buffer fixe au lieu de String::substring() pour éviter allocations
      int lineLen = lineEnd - idx;
      if (lineLen > 0 && lineLen < 120) {  // Limite raisonnable pour une ligne
        char line[120];
        strncpy(line, taskStatsPtr + idx, static_cast<size_t>(lineLen));
        line[lineLen] = '\0';

        // Chercher le '%' depuis la fin de la ligne
        int pctPos = -1;
        for (int i = lineLen - 1; i >= 0; i--) {
          if (line[i] == '%') {
            pctPos = i;
            break;
          }
        }

        if (pctPos > 0) {
          // Remonter pour trouver le début du nombre
          int start = pctPos - 1;
          while (start >= 0 && (line[start] >= '0' && line[start] <= '9')) start--;
          start++;
          int len = pctPos - start;
          if (len > 0 && len <= 3) {
            // Extraire le nombre avec atoi() au lieu de substring().toInt()
            char numStr[4];
            strncpy(numStr, line + start, static_cast<size_t>(len));
            numStr[len] = '\0';
            uint16_t val = static_cast<uint16_t>(atoiSimple(numStr));
            sumIdle = static_cast<uint16_t>(sumIdle + val);
            cnt++;
          }
        }
      }
      searchPtr = (lineEndPtr != nullptr) ? lineEndPtr + 1 : taskStatsPtr + taskStatsLen;
    }
    if (cnt > 0) {
      uint16_t avg = static_cast<uint16_t>(sumIdle / cnt);
      idlePct = (avg > 100) ? 100 : static_cast<uint8_t>(avg);
    }
  }
  return idlePct;
}

// -----------------------------------------------------------------------------
// 2. Décision de sauvegarde minHeap
// -----------------------------------------------------------------------------
// Reproduit la cascade if/else if/else if de Diagnostics::update (v11.173/174).
// L'appelant a déjà mis à jour minFreeHeap (= freeHeap au moment du nouveau
// minimum) AVANT d'appeler cette fonction ; ici on décide seulement quelle
// branche s'applique pour que l'appelant fasse l'écriture NVS + le log adéquat.
//
// Garde premier-boot : lastSavedMinHeap == UINT32_MAX neutralise les branches
// 2 et 3 (sinon lastSavedMinHeap - minFreeHeap déborderait).
enum class MinHeapSaveReason : uint8_t {
  NO_SAVE = 0,        // aucune sauvegarde (premier boot non concerné, ou pas assez de diff)
  FIRST_SAVE = 1,     // 1. première sauvegarde (lastHeapSave == 0)
  INTERVAL_DELTA = 2, // 2. intervalle minimum écoulé ET différence significative
  CRITICAL_LOSS = 3   // 3. perte très importante (> seuil critique, ex. 10KB)
};

// Décide si une sauvegarde minHeap est nécessaire et pourquoi.
// Paramètres temporels en unsigned long (parité avec millis()/_lastHeapSave).
// Les seuils sont passés explicitement (défauts dans DEFAULT_MIN_HEAP_*).
inline MinHeapSaveReason decideMinHeapSave(uint32_t minFreeHeap,
                                           uint32_t lastSavedMinHeap,
                                           unsigned long now,
                                           unsigned long lastHeapSave,
                                           unsigned long minHeapSaveInterval,
                                           uint32_t minHeapDiffForSave,
                                           uint32_t criticalLossBytes) {
  // 1. Première sauvegarde
  if (lastHeapSave == 0) {
    return MinHeapSaveReason::FIRST_SAVE;
  }
  // 2. Sauvegarde si intervalle minimum écoulé ET différence significative
  // v11.174: vérif lastSavedMinHeap != UINT32_MAX pour éviter overflow premier boot
  else if (lastSavedMinHeap != UINT32_MAX &&
           (now - lastHeapSave > minHeapSaveInterval) &&
           (lastSavedMinHeap - minFreeHeap > minHeapDiffForSave)) {
    return MinHeapSaveReason::INTERVAL_DELTA;
  }
  // 3. Sauvegarde si perte très importante (plus de 10KB)
  // v11.173: garde lastSavedMinHeap != UINT32_MAX (premier boot)
  else if (lastSavedMinHeap != UINT32_MAX &&
           lastSavedMinHeap - minFreeHeap > criticalLossBytes) {
    return MinHeapSaveReason::CRITICAL_LOSS;
  }
  return MinHeapSaveReason::NO_SAVE;
}

}  // namespace DiagnosticsDecision
