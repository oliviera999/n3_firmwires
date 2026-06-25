#pragma once

// -----------------------------------------------------------------------------
// Mock fonctionnel de Preferences (NVS Arduino-ESP32) pour tests natifs.
// Stockage en memoire, persistant entre instances Preferences (comme la vraie
// NVS qui survit aux reboots). Permet de tester les round-trips load/persist
// de PglCounter. reset() vide tout (= flash effacee).
//
// Couvre uniquement les operations utilisees par pgl_counter.cpp /
// pgl_event_journal.cpp : ULong, UShort, Bytes.
// -----------------------------------------------------------------------------

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace PglPrefsMock {

struct Value {
  uint32_t u32 = 0;
  uint16_t u16 = 0;
  std::vector<uint8_t> bytes;
  bool isBytes = false;
};

// Stockage global : namespace -> (cle -> valeur). Survit aux instances.
inline std::map<std::string, std::map<std::string, Value>>& store() {
  static std::map<std::string, std::map<std::string, Value>> s;
  return s;
}

inline void reset() { store().clear(); }

}  // namespace PglPrefsMock

class Preferences {
 public:
  bool begin(const char* name, bool /*readOnly*/ = false) {
    _ns = name ? name : "";
    return true;
  }
  void end() { _ns.clear(); }

  // --- ULong ---
  size_t putULong(const char* key, uint32_t value) {
    PglPrefsMock::store()[_ns][key].u32 = value;
    return sizeof(uint32_t);
  }
  uint32_t getULong(const char* key, uint32_t defaultValue = 0) {
    auto& ns = PglPrefsMock::store()[_ns];
    auto it = ns.find(key);
    return it == ns.end() ? defaultValue : it->second.u32;
  }

  // --- UShort ---
  size_t putUShort(const char* key, uint16_t value) {
    PglPrefsMock::store()[_ns][key].u16 = value;
    return sizeof(uint16_t);
  }
  uint16_t getUShort(const char* key, uint16_t defaultValue = 0) {
    auto& ns = PglPrefsMock::store()[_ns];
    auto it = ns.find(key);
    return it == ns.end() ? defaultValue : it->second.u16;
  }

  // --- Bytes ---
  size_t putBytes(const char* key, const void* value, size_t len) {
    auto& v = PglPrefsMock::store()[_ns][key];
    v.isBytes = true;
    v.bytes.assign(static_cast<const uint8_t*>(value),
                   static_cast<const uint8_t*>(value) + len);
    return len;
  }
  size_t getBytesLength(const char* key) {
    auto& ns = PglPrefsMock::store()[_ns];
    auto it = ns.find(key);
    if (it == ns.end() || !it->second.isBytes) return 0;
    return it->second.bytes.size();
  }
  size_t getBytes(const char* key, void* out, size_t maxLen) {
    auto& ns = PglPrefsMock::store()[_ns];
    auto it = ns.find(key);
    if (it == ns.end() || !it->second.isBytes) return 0;
    size_t n = it->second.bytes.size();
    if (n > maxLen) n = maxLen;
    memcpy(out, it->second.bytes.data(), n);
    return n;
  }

  bool remove(const char* key) {
    return PglPrefsMock::store()[_ns].erase(key) > 0;
  }
  bool clear() {
    PglPrefsMock::store()[_ns].clear();
    return true;
  }

 private:
  std::string _ns;
};
