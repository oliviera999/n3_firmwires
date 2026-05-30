#pragma once

#include <Arduino.h>

struct N3DataField {
  const char* name;
  String value;
};

struct N3PostConfig {
  const char* url;
  const char* apiKey;           // cle API pour signature HMAC body (header X-Signature). NULL = pas de signature legacy
  const N3DataField* fields;
  size_t fieldCount;
  void (*onSending)();
  void (*onResult)(int httpCode);
  // Auth contrat FFP3 (depuis n3_data 1.1) : si sigSecret est defini et que
  // currentEpochSeconds > 0, on ajoute "timestamp" + "signature" au body.
  // signature = HMAC-SHA256(timestamp, sigSecret).
  // Cote serveur, valide par SignatureValidator (FFP3 / Msp / N3pp).
  const char* sigSecret;         // partage avec l'env serveur API_SIG_SECRET (NULL = pas de HMAC FFP3)
  unsigned long currentEpochSeconds; // 0 = pas d'horodatage dispo (firmware sans NTP)
};

/**
 * Envoie un POST URL-encoded avec les champs fournis.
 *
 * Authentification (par ordre de priorite) :
 *   1. Si `sigSecret` non NULL et `currentEpochSeconds > 0` : ajoute
 *      `timestamp` + `signature` (HMAC-SHA256(timestamp, sigSecret)) au body
 *      et au header `X-Signature`. C'est le contrat FFP3/Msp/N3pp moderne.
 *   2. Si `apiKey` non NULL : ajoute `X-Api-Key` + signature HMAC(body, apiKey)
 *      en header (compat ancien serveur legacy via header).
 *
 * Retourne le code HTTP ou une valeur negative en cas d'erreur reseau.
 */
int n3DataPost(const N3PostConfig& config);

/**
 * GET HTTP simple. Retourne le body de la reponse.
 * outHttpCode recoit le code HTTP si non-NULL.
 * deviceApiKey : si non NULL, envoie le header X-Api-Key (auth serveur galerie / device).
 */
String n3DataGet(const char* url, unsigned int* outHttpCode, const char* deviceApiKey = nullptr);
