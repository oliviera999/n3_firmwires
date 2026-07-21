#include "n3_wifi_reconnect.h"

#include <cstring>

namespace N3WifiReconnect {

bool store(LastGoodAp& rec, uint32_t magic, const char* ssid, const uint8_t* bssid,
           int32_t channel) {
  if (!bssid || channel <= 0 || !ssid || ssid[0] == '\0') {
    rec.magic = 0;
    return false;
  }
  memcpy(rec.bssid, bssid, kBssidLen);
  rec.channel = static_cast<uint8_t>(channel);
  strncpy(rec.ssid, ssid, 32);
  rec.ssid[32] = '\0';
  rec.magic = magic;
  return true;
}

bool valid(const LastGoodAp& rec, uint32_t magic) {
  return rec.magic == magic && rec.channel != 0;
}

int matchIndex(const LastGoodAp& rec, uint32_t magic, const char* const* ssids, size_t count) {
  if (!valid(rec, magic) || !ssids) {
    return -1;
  }
  for (size_t i = 0; i < count; i++) {
    if (ssids[i] && strcmp(rec.ssid, ssids[i]) == 0) {
      return static_cast<int>(i);
    }
  }
  return -1;
}

uint32_t fastTimeoutMs(uint32_t fullTimeoutMs) {
  uint32_t t = fullTimeoutMs / 2;
  if (t < 2000) {
    t = (fullTimeoutMs < 2000) ? fullTimeoutMs : 2000;
  }
  return t;
}

}  // namespace N3WifiReconnect
