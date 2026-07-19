# n3_wifi_select — noyau pur de sélection WiFi

Extrait la **logique commune dupliquée** entre `shared/n3_wifi`
(`buildOrderFromScan`) et `ffp5cs/src/wifi_manager.cpp` (`connect()`), sous forme
d'une fonction **pure, déterministe, sans dépendance Arduino/WiFi** — donc
testable en natif (Unity, suite `test_wifi_select`).

À partir d'un scan et d'une liste de credentials (SSID), le noyau :

1. calcule pour chaque credential la **meilleure candidate** (RSSI le plus fort)
   avec son **BSSID + canal** ;
2. produit l'**ordre d'essai** : réseaux visibles triés par RSSI décroissant
   (égalité → index credential le plus petit d'abord), puis credentials non
   détectés ajoutés en fin dans l'ordre d'origine (SSID cachés).

Il **ne connecte pas** : la couche transport (bloquant vs session non bloquante,
scan Arduino vs ESP-IDF, retry sans BSSID, rescan invisible, AP fallback, override
MAC 4G, modem-sleep…) reste propre à chaque firmware et **appelle ce noyau** pour
décider *quoi* essayer et *dans quel ordre*.

## API

```cpp
#include "n3_wifi_select.h"
using namespace N3WifiSelect;

const char* ssids[] = {"NetA", "NetB", "NetC"};
ScanEntry   scan[]  = { {"NetB", -45, {0xAA,…}, 6}, … };

Candidate cand[3];
size_t    order[3];
size_t n = buildOrder(ssids, 3, scan, scanCount, cand, order, /*maxNetworks=*/0);

for (size_t k = 0; k < n; ++k) {
  size_t i = order[k];                 // index credential à essayer, meilleur d'abord
  if (cand[i].present)  connectWith(ssids[i], cand[i].bssid, cand[i].channel);
  else                  connectPlain(ssids[i]);   // SSID caché → rescan/plain begin
}
```

## Chemin d'adoption (prototype — non encore branché en production)

- **`shared/n3_wifi`** : remplacer le corps de `buildOrderFromScan()` par un appel
  à `N3WifiSelect::buildOrder()` (mapping `WiFi.SSID(j)/RSSI(j)/BSSID(j)/channel(j)`
  → `ScanEntry`). La machine à états (`kTryConnect`, retry sans BSSID, rescan
  invisible) reste inchangée.
- **`ffp5cs`** : remplacer le bloc `Cand cand[…]` + `std::sort` de `connect()` par
  le même appel (mapping depuis `wifi_ap_record_t`). L'échelle de 4 tentatives, le
  fallback AP, l'override MAC et le modem-sleep restent inchangés. `../shared` est
  déjà dans `lib_extra_dirs` → il suffit d'ajouter l'`#include`.

Comportement volontairement **identique** aux deux implémentations actuelles
(comparaison RSSI stricte, `strcmp` exact, égalités → ordre d'origine), pour un
remplacement sans changement de comportement observable.

## Tests

```bash
cd shared/tests_native && pio test -c platformio-native.ini -e native -f test_wifi_select
```
