# Référence WiFi et OTA — Firmwares projet n3

*Document centralisant les comportements communs et les schémas des composants WiFi et OTA, dans le cadre de l'harmonisation des firmwares (option 2).*

---

## 1. Credentials et secrets

| Firmware | Fichier | Contenu |
|----------|---------|---------|
| n3pp4_2, msp2_5, uploadphotosserver | `firmwires/credentials.h` | WiFi (WIFI_LIST), SMTP, API_KEY. Copier `credentials.h.example`. |
| ffp5cs | `ffp5cs/include/secrets.h` | WiFi (WIFI_LIST), SMTP. Copier `secrets.h.example`. |
| ffp5cs | `ffp5cs/include/secrets_config.h` | API_KEY, DEFAULT_RECIPIENT. Copier `secrets_config.h.example`. |

**Structure WiFi commune** : `{ ssid, password }` — alignée entre `N3WifiCredential` (n3_wifi, credentials.h) et `WifiManager::Credential` (ffp5cs).

**Cross-projet** : ffp5cs peut utiliser `firmwires/credentials.h` pour API_KEY si `secrets_config.h` est absent (fallback automatique dans `config.h`). Aligné avec serveur `.env` (API_KEY).

---

## 2. API WiFi commune

Comportement partagé par n3_wifi et ffp5cs WifiManager :

| Aspect | n3_wifi (n3pp, msp, uploadphotosserver) | WifiManager (ffp5cs) |
|--------|----------------------------------------|----------------------|
| Scan | Oui, tri par RSSI | Oui, tri par RSSI |
| BSSID / canal | Utilisation si réseau visible | Idem |
| Timeout par tentative | 5 s | 5 s |
| Retry sans BSSID | Oui si échec | Oui |
| AP de secours | Non | Oui (si aucun réseau trouvé) |
| Délai entre réseaux | 250 ms (configurable) | Configurable |

Constantes : `WIFI_CONNECT_TIMEOUT_MS = 5000` alignée partout.

---

## 3. Schéma OTA unifié

### Cibles n3pp, msp (prod et test)

| Cible | URL metadata | Script publication |
|-------|--------------|--------------------|
| n3pp | `http://iot.olution.info/ota/n3pp/metadata.json` | `scripts/publish_ota.ps1` (racine IOT_n3) |
| n3pp-test | `http://iot.olution.info/ota/n3pp-test/metadata.json` | idem |
| msp | `http://iot.olution.info/ota/msp/metadata.json` | idem |
| msp-test | `http://iot.olution.info/ota/msp-test/metadata.json` | idem |

**Structure metadata.json** (n3pp, msp) :
```json
{
  "version": "4.10",
  "url": "http://iot.olution.info/ota/n3pp/firmware.bin",
  "md5": "..."
}
```

### Cibles caméra (msp1, n3pp, ffp3)

| Cible | URL metadata | Clé metadata | Script publication |
|-------|--------------|--------------|--------------------|
| cam-msp1 | `http://iot.olution.info/ota/cam/metadata.json` | `msp1` | `scripts/publish_ota.ps1` |
| cam-n3pp | idem | `n3pp` | idem |
| cam-ffp3 | idem | `ffp3` | idem |

**Structure metadata.json** (cam) :
```json
{
  "msp1": { "version": "2.6", "url": "http://.../ota/cam/msp1/firmware.bin", "md5": "..." },
  "n3pp": { "version": "2.6", "url": "http://.../ota/cam/n3pp/firmware.bin", "md5": "..." },
  "ffp3": { "version": "2.6", "url": "http://.../ota/cam/ffp3/firmware.bin", "md5": "..." }
}
```

### ffp5cs

| Env | URL metadata | Structure | Script publication |
|-----|--------------|-----------|--------------------|
| wroom, s3, etc. | `https://iot.olution.info/ffp3/ota/metadata.json` | `channels` (esp32-wroom, esp32-s3) | `firmwires/ffp5cs/scripts/publish_ota.ps1` |

**Propriétés** : HTTPS, structure `channels` avec bin_url, filesystem_url, size, md5. Détails : `ffp5cs/docs/technical/OTA_PUBLISH.md`.

---

## 4. Règle version + metadata

À chaque modification du firmware : **incrémenter la version** (config.h, main.cpp ou constante) et **mettre à jour le metadata.json** lors de la publication OTA. Le script `publish_ota.ps1` extrait la version du code source ; synchroniser si dérive.
