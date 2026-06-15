#pragma once
// =============================================================================
// FFP5CS — Chemins & dates de la file SD (rotation des logs)
// =============================================================================
// Extrait de sd_logger.cpp (audit §3.8) pour tester la logique pure de
// nommage/parsing des entrées de file (rotation). Un bug ici (mauvais parsing
// de séquence, format de chemin) → entrées rejouées/perdues dans le mauvais
// ordre. Aucune dépendance SD/Arduino : pur cstdio/cstdlib/cstring/ctime.
// =============================================================================

#include <cstdio>
#include <cstdlib>   // strtoul
#include <cstring>   // strrchr
#include <cstdint>
#include <cstddef>
#include <ctime>     // gmtime_r

namespace SdQueuePath {

// Répertoire de la file (relatif au point de montage SD).
constexpr const char* QUEUE_DIR = "/queue";

// Formate un epoch (UTC) en "YYYYMMDD".
inline void dateStr(uint32_t epoch, char* out, size_t outSize) {
  time_t t = (time_t)epoch;
  struct tm ti;
  gmtime_r(&t, &ti);
  snprintf(out, outSize, "%04d%02d%02d", ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday);
}

// Construit le chemin d'une entrée : "/queue/NNNNNN.dat" (séquence sur 6 chiffres mini).
inline void seqToPath(uint32_t seq, char* out, size_t outSize) {
  snprintf(out, outSize, "%s/%06lu.dat", QUEUE_DIR, (unsigned long)seq);
}

// Extrait la séquence depuis un nom d'entrée (gère un préfixe de chemin) ;
// 0 si name null ou sans chiffres en tête.
inline uint32_t seqFromEntryName(const char* name) {
  if (!name) return 0;
  const char* base = strrchr(name, '/');
  base = base ? base + 1 : name;
  return (uint32_t)strtoul(base, nullptr, 10);
}

}  // namespace SdQueuePath
