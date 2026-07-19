#ifndef N3_WIFI_SELECT_H
#define N3_WIFI_SELECT_H

#include <cstddef>
#include <cstdint>

// ============================================================================
// n3_wifi_select — noyau PUR de selection WiFi (sans dependance Arduino/WiFi).
//
// Extrait la logique commune, aujourd'hui DUPLIQUEE, entre :
//   - shared/n3_wifi   (n3_wifi.cpp::buildOrderFromScan)
//   - ffp5cs           (wifi_manager.cpp::connect, bloc "Cand cand[...]")
//
// L'algorithme est identique dans les deux : a partir d'un scan et d'une liste
// de credentials (SSID), il construit pour chaque credential la MEILLEURE
// candidate (RSSI le plus fort) avec son BSSID + canal, puis un ORDRE d'essai :
//   1) reseaux visibles tries par RSSI decroissant (egalite -> index credential
//      le plus petit d'abord, comportement historique de n3_wifi) ;
//   2) credentials NON detectes au scan ajoutes en fin, dans l'ordre d'origine
//      (tentative "invisible"/SSID cache geree par la couche transport).
//
// Aucune tentative de connexion ici : c'est une fonction pure, deterministe et
// testable en natif (Unity). La couche transport (bloquant vs session, scan
// Arduino vs ESP-IDF, retry sans BSSID, rescan, AP fallback...) reste propre a
// chaque firmware et appelle ce noyau pour decider QUOI essayer et DANS QUEL
// ORDRE.
// ============================================================================

namespace N3WifiSelect {

/** Longueur d'un BSSID (adresse MAC de l'AP). */
constexpr size_t kBssidLen = 6;

/** RSSI sentinelle "aucune mesure" (plus faible que tout RSSI reel, dBm). */
constexpr int8_t kRssiAbsent = -128;

/**
 * Une entree de resultat de scan, agnostique du materiel.
 * `ssid` doit rester valide pendant l'appel (non copie).
 */
struct ScanEntry {
  const char* ssid;
  int8_t rssi;
  uint8_t bssid[kBssidLen];
  uint8_t channel;
};

/**
 * Meilleure candidate trouvee au scan pour un credential donne.
 * Indexee comme la liste des SSID passee en entree.
 */
struct Candidate {
  int8_t rssi;                 // kRssiAbsent si absent
  uint8_t bssid[kBssidLen];    // 00:..:00 si absent
  uint8_t channel;             // 0 si absent
  bool present;                // true si le SSID a ete vu au scan
};

/**
 * Construit la table de candidates + l'ordre d'essai.
 *
 * @param ssids        Tableau des SSID configures (credentials). Les entrees
 *                     nullptr ou vides sont ignorees (jamais "present", mais
 *                     conservent leur slot d'index).
 * @param ssidCount    Nombre de credentials.
 * @param scan         Resultats de scan (peut etre nullptr si scanCount == 0).
 * @param scanCount    Nombre d'entrees de scan.
 * @param candOut      Sortie : table des candidates, taille >= effectiveCount.
 *                     candOut[i] correspond a ssids[i].
 * @param orderOut     Sortie : ordre d'essai (indices credential), taille
 *                     >= effectiveCount.
 * @param maxNetworks  Borne le nombre de credentials consideres (clamp type
 *                     scanMax / MAX_WIFI_SAVED_NETWORKS). 0 => pas de borne
 *                     (== ssidCount).
 *
 * @return effectiveCount : nombre de credentials consideres (== nombre
 *         d'entrees ecrites dans orderOut). Vaut min(ssidCount, maxNetworks).
 *
 * Deterministe : deux appels avec les memes entrees produisent le meme ordre.
 */
size_t buildOrder(const char* const* ssids,
                  size_t ssidCount,
                  const ScanEntry* scan,
                  size_t scanCount,
                  Candidate* candOut,
                  size_t* orderOut,
                  size_t maxNetworks = 0);

}  // namespace N3WifiSelect

#endif  // N3_WIFI_SELECT_H
