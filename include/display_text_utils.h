#pragma once

#include <Arduino.h>

namespace DisplayTextUtils {

// Convertit UTF-8 en CP437 et écrit dans le buffer
// Retourne le nombre de caractères écrits (sans le '\0')
size_t utf8ToCp437(const char* input, char* output, size_t outputSize);

// Tronque un texte en place à maxChars caractères (ajoute "..." si nécessaire)
void clipInPlace(char* text, size_t textSize, int maxChars);

uint8_t characterWidth(uint8_t size);

}











