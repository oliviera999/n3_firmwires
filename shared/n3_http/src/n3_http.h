#ifndef N3_HTTP_H
#define N3_HTTP_H

#include <Arduino.h>

String n3HttpGet(const char* url, int* outResponseCode);
int n3HttpPost(const char* url, const char* contentType, const String& body);

#endif
