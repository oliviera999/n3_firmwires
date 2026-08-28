#pragma once

/**
 * n3_outputs_json
 *
 * Helpers de parsing JSON pour le contrat /outputs_state des firmwares legacy
 * (n3pp / msp). Deux formats serveur sont acceptes :
 *   1) Plat (legacy / getState firmware) :
 *      { "100": "mail@x", "106": "0", "107": "300", "110": "0", "112": "0", ... }
 *   2) Nested (API realtime `/api/outputs/state`) :
 *      { "timestamp": …, "outputs": [ { "gpio": 112, "state": "0", … }, … ] }
 * Les cles sont des numeros de GPIO virtuels. Cle 112 = VeilleInfinie
 * (1 = veille infinie sous SeuilPontDiv, 0 = sommeil timer). Absente ->
 * defaut firmware conserve.
 *
 * Ces helpers sont utilises par firmwires/n3pp/src/n3pp_network.cpp et
 * firmwires/msp/src/msp_network.cpp (factorisation v4.39 / 2.43).
 *
 * Necessite Arduino_JSON (arduino-libraries/Arduino_JSON).
 */

#include <Arduino.h>
#include <Arduino_JSON.h>

namespace N3Outputs {

/** Lit un int depuis une cle, retourne `defaultValue` si absent/invalide. */
int readIntByKey(JSONVar& obj, const char* key, int defaultValue);

/**
 * Variante non-destructive : si la cle est presente et exploitable, met
 * `*outValue` a jour et retourne true ; sinon `*outValue` n'est pas modifie
 * et retourne false. Permet de detecter explicitement un champ absent vs un
 * champ a 0 (utile pour appliquer une politique "garder valeur locale").
 */
bool tryReadIntByKey(JSONVar& obj, const char* key, int* outValue);

/** Lit une string ; defaut si absent ; stringifie une valeur non-string. */
String readStringByKey(JSONVar& obj, const char* key, const String& defaultValue);

} // namespace N3Outputs
