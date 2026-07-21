#ifndef N3_WIFI_RECONNECT_H
#define N3_WIFI_RECONNECT_H

#include <cstddef>
#include <cstdint>

// ============================================================================
// n3_wifi_reconnect — noyau PUR du "fast reconnect" WiFi (sans dépendance
// Arduino/WiFi/RTC).
//
// Extrait la logique commune, aujourd'hui DUPLIQUÉE quasi verbatim, entre :
//   - shared/n3_wifi   (N3WifiLastGood + rememberLastGood + startFastReconnect)
//   - poissonglouton   (PglLastGoodAp + rememberLastGoodAp + tryFastReconnect)
//
// Principe du fast reconnect : après un deep sleep (ou une déconnexion), au lieu
// de relancer un scan complet, on ré-associe directement au dernier AP connu
// (SSID + BSSID + canal mémorisés en RTC), ce qui est bien plus rapide. Le
// record survit au deep sleep via RTC_DATA_ATTR côté firmware.
//
// Ce module ne touche PAS au matériel : il fournit le type de record, sa
// validation/construction, la décision "le dernier AP est-il encore un réseau
// configuré ?" et la politique de timeout — le tout pur, déterministe, testable
// en natif (Unity). Le firmware garde : l'instance RTC_DATA_ATTR, l'appel
// WiFi.begin(ssid, pass, canal, bssid), et sa machine à états.
//
// NB — ffp5cs est volontairement HORS périmètre : son "fast reconnect"
// (PowerManager::reconnectWithSavedCredentials) est un cache SSID+PSK en RAM
// (survit au light sleep, pas au reboot), sans pinning BSSID/canal ni RTC ;
// modèle distinct, aucun recouvrement avec ce noyau.
//
// Le `magic` est un PARAMÈTRE (et non une constante figée) pour que chaque
// firmware conserve sa propre valeur sentinelle : un record RTC écrit par un
// firmware antérieur reste ainsi valide après OTA (pas d'invalidation forcée).
// ============================================================================

namespace N3WifiReconnect {

/** Longueur d'un BSSID (adresse MAC de l'AP). */
constexpr size_t kBssidLen = 6;

/**
 * Record du dernier AP connecté avec succès, destiné à être stocké en
 * RTC_DATA_ATTR côté firmware (survit au deep sleep). Layout stable et commun
 * à n3_wifi et poissonglouton (compatibilité binaire RTC préservée à travers
 * les OTA tant que ce layout ne change pas).
 */
struct LastGoodAp {
  uint32_t magic;              // sentinelle de validité (propre à chaque firmware)
  uint8_t bssid[kBssidLen];    // BSSID de l'AP
  uint8_t channel;             // canal (0 = invalide)
  char ssid[33];               // SSID (32 + NUL)
};

/**
 * Mémorise l'AP courant dans `rec`. Valide les entrées comme les deux
 * implémentations d'origine : BSSID non nul, canal > 0, SSID non vide. Si une
 * entrée est invalide, `rec.magic` est mis à 0 (record invalidé) et false est
 * renvoyé. Sinon `rec` est renseigné (magic = `magic`) et true est renvoyé.
 *
 * @param channel canal courant (typiquement WiFi.channel(), int32_t) ; <= 0 => invalide.
 */
bool store(LastGoodAp& rec, uint32_t magic, const char* ssid, const uint8_t* bssid,
           int32_t channel);

/** true si `rec` est un record valide pour ce `magic` (magic + canal != 0). */
bool valid(const LastGoodAp& rec, uint32_t magic);

/**
 * Si `rec` est valide ET que son SSID correspond à l'un des `ssids` configurés,
 * renvoie l'index de ce credential ; sinon -1. Les entrées nullptr sont ignorées.
 * `strcmp` exact (comme les implémentations d'origine).
 */
int matchIndex(const LastGoodAp& rec, uint32_t magic, const char* const* ssids, size_t count);

/**
 * Politique de timeout du fast reconnect : moitié du timeout complet, avec un
 * plancher à 2000 ms (borné au timeout complet si celui-ci est < 2000 ms).
 * Identique à n3_wifi ; iso pour poissonglouton sur sa config réelle (15 s -> 7,5 s).
 */
uint32_t fastTimeoutMs(uint32_t fullTimeoutMs);

}  // namespace N3WifiReconnect

#endif  // N3_WIFI_RECONNECT_H
