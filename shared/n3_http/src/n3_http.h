#ifndef N3_HTTP_H
#define N3_HTTP_H

#include <Arduino.h>

/**
 * @deprecated Cette lib HTTP minimale est conservee pour compatibilite avec
 * le code legacy. Les nouveaux usages doivent utiliser `n3_data` (timeout par
 * defaut, HMAC, retry, codes retour normalises) ou ses extensions.
 *
 * Timeout par defaut : `N3_HTTP_TIMEOUT_MS` (cf. `n3_common/n3_defaults.h`).
 */
String n3HttpGet(const char* url, int* outResponseCode);
int n3HttpPost(const char* url, const char* contentType, const String& body);

#endif
