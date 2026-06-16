#pragma once
// =============================================================================
// FFP5CS — Conversion booléenne des valeurs de commande serveur — logique pure
// =============================================================================
// Cœur SANS ArduinoJson de `parseBoolFromDoc` (gpio_parser.cpp) : la conversion
// d'un token texte ("1"/"true"/"on"/"0"/"false") ou d'un entier vers un bool.
// Le serveur distant peut envoyer la valeur d'un GPIO comme bool JSON, entier,
// ou string ("1", "true", "on", ...). Le *dispatch de type* (est-ce un bool /
// int / string ?) reste dans gpio_parser.cpp via ArduinoJson ; ce header ne
// gère que les cas int et string, en parité exacte avec l'original.
//
// Table de vérité reproduite à l'identique (case-insensitive, comme strcasecmp) :
//   string  "1" | "true" | "on"           -> true   (reconnu)
//   string  "0" | "false"                 -> false  (reconnu)
//   string  inconnu / vide / nullptr      -> non reconnu (l'appelant -> false)
//   int     == 1                          -> true ; sinon false
//
// Aucune dépendance Arduino/ArduinoJson/config -> testable nativement (g++).
// =============================================================================

#include <cstring>

namespace GpioBoolParse {

// Token texte -> bool, case-insensitive (parité strcasecmp).
// Reconnaît exactement les tokens de l'original : "1"/"true"/"on" (true) et
// "0"/"false" (false). Écrit le résultat dans `out` et retourne true UNIQUEMENT
// si le token est reconnu. Pour un token inconnu, vide, ou nullptr, retourne
// false et NE modifie PAS `out` (l'appelant retombe alors sur son défaut false,
// exactement comme le `return false` terminal de parseBoolFromDoc).
inline bool fromToken(const char* s, bool& out) {
  if (s == nullptr) {
    return false;
  }
  if (strcasecmp(s, "1") == 0 || strcasecmp(s, "true") == 0 || strcasecmp(s, "on") == 0) {
    out = true;
    return true;
  }
  if (strcasecmp(s, "0") == 0 || strcasecmp(s, "false") == 0) {
    out = false;
    return true;
  }
  return false;
}

// Entier -> bool : règle de l'original, strictement `valeur == 1`
// (donc 0, 2, -1, ... -> false ; seul 1 -> true).
inline bool fromInt(int v) {
  return v == 1;
}

}  // namespace GpioBoolParse
