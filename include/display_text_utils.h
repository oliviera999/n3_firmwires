#pragma once

#include <Arduino.h>

namespace DisplayTextUtils {

String utf8ToCp437(const char* input);
String utf8ToCp437(const String& input);

void clipInPlace(String& text, int maxChars);

uint8_t characterWidth(uint8_t size);

}











