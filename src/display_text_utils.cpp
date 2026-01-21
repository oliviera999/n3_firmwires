#include "display_text_utils.h"

namespace DisplayTextUtils {

size_t utf8ToCp437(const char* input, char* output, size_t outputSize) {
  if (!input || !output || outputSize == 0) {
    if (output && outputSize > 0) {
      output[0] = '\0';
    }
    return 0;
  }
  
  size_t outIdx = 0;
  const uint8_t* s = reinterpret_cast<const uint8_t*>(input);
  
  while (*s && outIdx < outputSize - 1) {
    uint8_t c = *s++;
    if (c == 0xC3) {
      uint8_t n = *s;
      if (n) {
        ++s;
        char cp437_char = 0;
        switch (n) {
          case 0xA9: cp437_char = 0x82; break; // é
          case 0xA8: cp437_char = 0x8A; break; // è
          case 0xAA: cp437_char = 0x88; break; // ê
          case 0xA0: cp437_char = 0x85; break; // à
          case 0xA2: cp437_char = 0x83; break; // â
          case 0xB9: cp437_char = 0x97; break; // ù
          case 0xBB: cp437_char = 0x96; break; // û
          case 0xA7: cp437_char = 0x87; break; // ç
          case 0xB4: cp437_char = 0x93; break; // ô
          case 0xAF: cp437_char = 0x8B; break; // ï
          case 0xAE: cp437_char = 0x8C; break; // î
          case 0x89: cp437_char = 0x90; break; // É
          case 0x87: cp437_char = 0x80; break; // Ç
          default:
            continue; // Caractère non supporté, on ignore
        }
        if (cp437_char != 0) {
          output[outIdx++] = cp437_char;
        }
      }
    } else if ((c & 0xE0) == 0xC0) {
      if (*s) ++s;
    } else if ((c & 0xF0) == 0xE0) {
      if (*s) ++s;
      if (*s) ++s;
    } else if ((c & 0xF8) == 0xF0) {
      if (*s) ++s;
      if (*s) ++s;
      if (*s) ++s;
    } else {
      output[outIdx++] = char(c);
    }
  }
  
  output[outIdx] = '\0';
  return outIdx;
}

void clipInPlace(char* text, size_t textSize, int maxChars) {
  if (!text || textSize == 0 || maxChars <= 0) {
    if (text && textSize > 0) {
      text[0] = '\0';
    }
    return;
  }
  
  size_t len = strlen(text);
  if (static_cast<int>(len) <= maxChars) {
    return;
  }
  
  if (maxChars >= 3) {
    size_t newLen = maxChars - 3;
    if (newLen < textSize - 1) {
      text[newLen] = '\0';
      strncat(text, "...", textSize - newLen - 1);
    }
  } else {
    if (maxChars < static_cast<int>(textSize)) {
      text[maxChars] = '\0';
    }
  }
}

uint8_t characterWidth(uint8_t size) {
  if (size == 0) return 6;
  return static_cast<uint8_t>(6 * size);
}

}  // namespace DisplayTextUtils











