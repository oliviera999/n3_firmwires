#include "n3_wifi_select.h"

#include <cstring>

namespace N3WifiSelect {

namespace {

// SSID valide = non nullptr et non vide.
bool validSsid(const char* s) { return s != nullptr && s[0] != '\0'; }

}  // namespace

size_t buildOrder(const char* const* ssids,
                  size_t ssidCount,
                  const ScanEntry* scan,
                  size_t scanCount,
                  Candidate* candOut,
                  size_t* orderOut,
                  size_t maxNetworks) {
  if (!ssids || !candOut || !orderOut) {
    return 0;
  }

  size_t netCount = ssidCount;
  if (maxNetworks > 0 && netCount > maxNetworks) {
    netCount = maxNetworks;
  }
  if (netCount == 0) {
    return 0;
  }

  // 1) Init des candidates (aucune mesure).
  for (size_t i = 0; i < netCount; i++) {
    candOut[i].rssi = kRssiAbsent;
    candOut[i].channel = 0;
    candOut[i].present = false;
    memset(candOut[i].bssid, 0, kBssidLen);
  }

  // 2) Pour chaque entree de scan, mettre a jour la meilleure candidate.
  //    strcmp exact (comme n3_wifi et ffp5cs) ; on garde le RSSI le plus fort.
  //    Comparaison STRICTE (r > cand.rssi) : a egalite, le premier AP scanne
  //    est conserve — comportement historique commun aux deux implementations.
  for (size_t j = 0; j < scanCount; j++) {
    const char* scanSsid = scan[j].ssid;
    if (!validSsid(scanSsid)) {
      continue;
    }
    const int8_t r = scan[j].rssi;
    for (size_t i = 0; i < netCount; i++) {
      if (!validSsid(ssids[i])) {
        continue;
      }
      if (strcmp(scanSsid, ssids[i]) == 0 && r > candOut[i].rssi) {
        candOut[i].rssi = r;
        memcpy(candOut[i].bssid, scan[j].bssid, kBssidLen);
        candOut[i].channel = scan[j].channel;
        candOut[i].present = true;
      }
    }
  }

  // 3a) Ordre : credentials presents, inseres tries par RSSI decroissant.
  //     Insertion sort STABLE : a RSSI egal, l'index credential le plus petit
  //     reste devant (identique au tri a bulles strict de n3_wifi).
  size_t orderCount = 0;
  for (size_t i = 0; i < netCount; i++) {
    if (!candOut[i].present) {
      continue;
    }
    size_t pos = orderCount;
    while (pos > 0 && candOut[i].rssi > candOut[orderOut[pos - 1]].rssi) {
      orderOut[pos] = orderOut[pos - 1];
      pos--;
    }
    orderOut[pos] = i;
    orderCount++;
  }

  // 3b) Puis les credentials NON detectes, dans l'ordre d'origine.
  for (size_t i = 0; i < netCount; i++) {
    if (!candOut[i].present) {
      orderOut[orderCount++] = i;
    }
  }

  return netCount;
}

}  // namespace N3WifiSelect
