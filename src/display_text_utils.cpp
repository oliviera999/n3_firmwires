#include "display_text_utils.h"

namespace DisplayTextUtils {

String utf8ToCp437(const char* input) {
  String out;
  if (!input) return out;
  const uint8_t* s = reinterpret_cast<const uint8_t*>(input);
  while (*s) {
    uint8_t c = *s++;
    if (c == 0xC3) {
      uint8_t n = *s;
      if (n) {
        ++s;
        switch (n) {
          case 0xA9: out += char(0x82); break; // é
          case 0xA8: out += char(0x8A); break; // è
          case 0xAA: out += char(0x88); break; // ê
          case 0xA0: out += char(0x85); break; // à
          case 0xA2: out += char(0x83); break; // â
          case 0xB9: out += char(0x97); break; // ù
          case 0xBB: out += char(0x96); break; // û
          case 0xA7: out += char(0x87); break; // ç
          case 0xB4: out += char(0x93); break; // ô
          case 0xAF: out += char(0x8B); break; // ï
          case 0xAE: out += char(0x8C); break; // î
          case 0x89: out += char(0x90); break; // É
          case 0x87: out += char(0x80); break; // Ç
          default:
            break;
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
      out += char(c);
    }
  }
  return out;
}

String utf8ToCp437(const String& input) {
  return utf8ToCp437(input.c_str());
}

void clipInPlace(String& text, int maxChars) {
  if (maxChars <= 0) {
    text.clear();
    return;
  }
  if (static_cast<int>(text.length()) <= maxChars) {
    return;
  }
  if (maxChars >= 3) {
    text = text.substring(0, maxChars - 3);
    text += "...";
  } else {
    text = text.substring(0, maxChars);
  }
}

uint8_t characterWidth(uint8_t size) {
  if (size == 0) return 6;
  return static_cast<uint8_t>(6 * size);
}

}  // namespace DisplayTextUtils











