/**
 * FFP5CS: force-include pour envs WROOM (pioarduino).
 * Garantit que FS et LittleFS sont déclarés avant la compilation des libs
 * (ex. ESP Mail Client) lorsque le CPPPATH du framework n'est pas appliqué aux libs.
 * C++ uniquement : évite d'inclure <FS.h>/<LittleFS.h> dans les unités C (pas de <memory>).
 * Utilisation : build_flags = -include "include/ForceLittleFS.h"
 */
#pragma once

#ifdef __cplusplus
#include <FS.h>
#include <LittleFS.h>
#endif
