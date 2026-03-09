#pragma once

#include <Arduino.h>

struct N3DataField {
  const char* name;
  String value;
};

struct N3PostConfig {
  const char* url;
  const char* apiKey;           // cle API pour signature HMAC (NULL = pas de HMAC)
  const N3DataField* fields;
  size_t fieldCount;
  void (*onSending)();          // callback avant envoi (peut etre NULL)
  void (*onResult)(int httpCode); // callback apres envoi (peut etre NULL)
};

/**
 * Envoie un POST URL-encoded avec les champs fournis.
 * Si apiKey != NULL, signe la requete via HMAC-SHA256 (header X-Signature).
 * Retourne le code HTTP ou une valeur negative en cas d'erreur reseau.
 */
int n3DataPost(const N3PostConfig& config);

/**
 * GET HTTP simple. Retourne le body de la reponse.
 * outHttpCode recoit le code HTTP si non-NULL.
 */
String n3DataGet(const char* url, unsigned int* outHttpCode);
