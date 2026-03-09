#pragma once

#include <Arduino.h>

/**
 * Calcule le HMAC-SHA256 d'un message avec une cle donnee.
 * Retourne la signature en hexadecimal (64 caracteres + '\0').
 * buffer doit faire au moins 65 octets.
 * Retourne true si le calcul a reussi.
 */
bool n3HmacSha256(const char* key, const char* message, char* hexOutput, size_t hexOutputSize);

/**
 * Ajoute un header X-Signature au HTTPClient avant un POST.
 * Signe le body avec la cle API via HMAC-SHA256.
 * Compatibilite ascendante : api_key reste dans le body.
 */
void n3HmacSignRequest(class HTTPClient& http, const char* apiKey, const char* body);
