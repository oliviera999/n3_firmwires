#pragma once
// =============================================================================
// FFP5CS - Configuration logging (façade)
// =============================================================================
// v13.60 (audit général 2026-05) : façade légère qui ré-exporte le contenu de
// `config.h`. Le découpage effectif des sections (LogConfig + macros LOG_* + stub
// NullSerial) reste dans `config.h` pour ne pas casser la compilation des ~37
// fichiers `src/*.cpp` qui incluent encore `config.h`.
//
// Migration progressive : remplacer `#include "config.h"` par cet en-tête plus
// ciblé dans les fichiers qui n'utilisent que les macros LOG_*. Le contenu sera
// déplacé physiquement dans une release ultérieure (suivi VERSION.md).
// =============================================================================
#include "config.h"