# Audit des principaux firmwares — n3_firmwires

**Date de l'audit initial :** 11 mars 2026
**Branche :** `claude/audit-main-firmwares-5CTIL`
**Périmètre :** Tous les firmwares du dépôt `/n3_firmwires`

> **Mise à jour 2026-06-09 (alignement code) :** inventaire, chemins et versions actualisés au code courant. Les dossiers principaux sont `n3pp/` et `msp/` (et non plus `n3pp4_2/` / `msp2_5/`) ; `LVGL_Widgets` et `ratata` sont dans `à voir/`. Le constat **🔴 critique** sur les credentials en dur dans `à voir/LVGL_Widgets/src/main.cpp` est **résolu** : le fichier inclut désormais `credentials.h` (`AUTHOR_PASSWORD`, `API_KEY_VALUE`, etc.). Versions courantes : n3pp **4.39**, msp **2.43**, ffp5cs **13.92**, uploadphotosserver **2.39**, poissonglouton **0.1.2**.

---

## 1. Inventaire des firmwares

| Firmware | Dossier | Cible | Version | Rôle |
|----------|---------|-------|---------|------|
| **N3PhasmesProto** | `n3pp/` | ESP32 | 4.39 | Contrôle serre/aquaponie (capteurs sol/air, pompe, relais, OLED, web, mail) |
| **MeteoStationPrototype** | `msp/` | ESP32 | 2.43 | Station météo + tracker solaire (DHT×2, DS18B20, LDR×4, servos, OLED, web, mail) |
| **FFP5CS** | `ffp5cs/` | ESP32 / ESP32-S3 | 13.92 | Contrôleur aquaponie avancé (OTA sha256+ECDSA, SD, NVS, HMAC, offline-first) |
| **UploadPhotoServer (unifié)** | `uploadphotosserver/` | ESP32-CAM | 2.39 | Upload photo vers iot.olution.info (cibles msp1/n3pp/ffp3 via build_flags) |
| **Poissonglouton** | `poissonglouton/` | ESP32-S3 | 0.1.2 | Compteur de bouteilles (IR/ultrason, DFPlayer, LVGL, upload batch, deep sleep solaire) |
| **UploadPhotoServer FFP3 deep sleep** | `uploadphotosserver_ffp3_1_5_deppsleep/` | ESP32-CAM | 1.5 | Variante FFP3 (legacy) |
| **UploadPhotoServer N3PP deep sleep** | `uploadphotosserver_n3pp_1_6_deppsleep/` | ESP32-CAM | 1.6 | Variante N3PP (legacy) |
| **UploadPhotoServer MSP1** | `uploadphotosserver_msp1/` | ESP32-CAM | — | Variante MSP1 standalone (legacy) |
| **UploadPhotoServer legacy** | `archive/uploadphotosserver_legacy/` | ESP32-CAM | — | Historique caméra archivé |
| **LVGL_Widgets** | `à voir/LVGL_Widgets/` | ESP32-S3 | — | Prototype IHM LVGL (non actif) |
| **Ratata** | `à voir/ratata/` | Arduino UNO / ESP32-CAM | — | Voiture/robot (kit ZYC0108-EN, projet annexe) |

---

## 2. Structure et architecture

### 2.1 Bibliothèques partagées (`shared/`)

| Lib | Rôle |
|-----|------|
| `n3_analog_sensors` | Lecture ADC filtrée (médiane + rejet outliers + EMA) — luminosité, pont diviseur, humidité sol |
| `n3_battery` | Mesure tension batterie via pont diviseur (délègue à `n3_analog_sensors`) |
| `n3_wifi` | Connexion WiFi avec scan RSSI, tri par signal, retry BSSID |
| `n3_http` | GET/POST HTTP minimal — **déprécié** (préférer `n3_data`) |
| `n3_data` | POST `application/x-www-form-urlencoded` avec HMAC body (`X-Signature`) + HMAC FFP3 |
| `n3_hmac` | Signature HMAC-SHA256 via mbedTLS (header `X-Signature`) |
| `n3_mail` | Envoi email SMTP (ESP Mail Client) |
| `n3_time` | Sauvegarde/restauration heure NVS, raison de réveil (ESP32Time) |
| `n3_common` | OTA `n3_ota` (sha256 + ECDSA P-256), constantes `n3_defaults.h`, parsing `n3_outputs_json` |
| `n3_sleep` | Deep sleep mutualisé (timer + GPIO ext0) |
| `n3_display` | Abstraction affichage OLED SSD1306 |
| `libn3_iot` | Drivers capteurs génériques (DHT, DS18B20, analogique) — conservé pour compat |

**Point positif :** La factorisation en bibliothèques partagées est bien réalisée. Les firmwares `n3pp`, `msp` et `uploadphotosserver` utilisent tous `shared/` via `lib_extra_dirs = ../shared` dans PlatformIO (et `ffp5cs` pour certaines libs). Détail à jour : voir [`shared/README.md`](shared/README.md).

### 2.2 Modularisation des firmwares principaux

**n3pp** (~1 284 lignes réparties en 6 modules) :
```
src/main.cpp             (313 lignes — setup/loop, orchestration)
src/n3pp_globals.cpp     (134 lignes — variables globales, version)
src/n3pp_automation.cpp  (335 lignes — arrosage, nourrissage, relais)
src/n3pp_display.cpp     ( 93 lignes — OLED)
src/n3pp_network.cpp     (233 lignes — WiFi, HTTP, NTP, web server)
src/n3pp_sensors.cpp     (176 lignes — DHT, sol, luminosité, batterie)
```

**msp** (~1 381 lignes réparties en 5 modules) :
```
src/main.cpp             (439 lignes — setup/loop)
src/msp_automation.cpp   (173 lignes — tracker solaire, servos, relais)
src/msp_display.cpp      (101 lignes — OLED)
src/msp_network.cpp      (255 lignes — WiFi, HTTP, NTP, web server)
src/msp_sensors.cpp      (413 lignes — DHT×2, DS18B20, LDR×4, batterie, pluie)
```

**ffp5cs** (~23 400 lignes, structuré en 36 modules `src/`) — architecture la plus aboutie.

---

## 3. Gestion des secrets

### 3.1 Mécanisme en place (correct)

- Un fichier `credentials.h.example` à la racine sert de template clair et documenté.
- `credentials.h` (copie locale non versionnée) est dans `.gitignore`.
- `n3pp` et `msp` incluent `credentials.h` via leur `n3pp_config.h` / `msp_config.h`.
- `ffp5cs` utilise `secrets_config.h` (priorité) ou `../../credentials.h` (fallback) avec cascade `#if __has_include`.
- `uploadphotosserver` inclut directement `credentials.h`.

### 3.2 Problèmes identifiés

#### ✅ RÉSOLU (2026-06) — Credentials en dur dans `à voir/LVGL_Widgets/src/main.cpp`

Le prototype LVGL inclut désormais `#include "credentials.h"` et lit ses secrets depuis ce fichier (non versionné) :

```cpp
#include "credentials.h"        // ligne 21
config.login.password = AUTHOR_PASSWORD;   // depuis credentials.h
String apiKeyValue = API_KEY_VALUE;        // depuis credentials.h
```

Le mot de passe d'application Gmail et la clé API ne sont plus en dur dans le source. **Constat clos.** (Si un mot de passe réel avait été commité dans l'historique, penser tout de même à le révoquer.)

#### ✅ RÉSOLU/AMÉLIORÉ (2026-06) — Fallback credentials dans `ffp5cs/include/config.h`

Le fallback `admin/ffp3` a été remplacé par un placeholder `CHANGEZ_MOI`, et le **profil PROD** est protégé par un `static_assert` qui **bloque la compilation** si `WEB_AUTH_PASS` vaut encore le placeholder :

```cpp
static_assert(!SecretsValidation::strEq(WebAuthConfig::WEB_AUTH_PASS, "CHANGEZ_MOI"),
    "PROFILE_PROD: WebAuthConfig::WEB_AUTH_PASS vaut encore le placeholder ...");
```

Un build prod sans secrets configurés échoue donc explicitement. **Constat clos.**

#### 🟡 INFO — Communications HTTP non chiffrées vers le serveur

Les URLs serveur des firmwares historiques (`n3pp`, `msp`) utilisent `http://` :
```cpp
"http://iot.olution.info/n3pp/..."
"http://iot.olution.info/msp1/..."
```

Côté `ffp5cs`, un flag `USE_HTTPS_ENDPOINTS` et l'env `wroom-prod-https` sont préparés (TLS métier en cours, cf. VERSION.md v13.80+).

**Risque :** Les données de capteurs et la clé API transitent en clair sur le réseau. HMAC-SHA256 (`X-Signature`) protège l'intégrité mais pas la confidentialité.
**Recommandation :** Migrer vers HTTPS si le serveur le supporte (l'ESP32 supporte TLS via `WiFiClientSecure`).

---

## 4. Qualité du code

### 4.1 Fonctions C non sécurisées

| Fichier | Ligne | Problème |
|---------|-------|---------|
| `shared/n3_hmac/src/n3_hmac.cpp` | 29 | `sprintf(hexOutput + (i*2), "%02x", ...)` — buffer connu (65 octets, boucle 32 itérations), risque nul mais non idiomatique |

**Note :** `ffp5cs` utilise correctement `snprintf` et a une fonction utilitaire `Utils::safeStrncpy`. Les firmwares `n3pp` et `msp` n'utilisent pas de `sprintf`/`strcpy` non bornés dans leur code propre. Le `sprintf` de `n3_hmac.cpp:29` est **toujours présent** (à remplacer par `snprintf` pour la conformité, risque effectif nul).

Le `sprintf` dans `n3_hmac.cpp` écrit exactement `2 × 32 = 64` caractères dans un buffer de 65 → pas de dépassement effectif, mais à remplacer par `snprintf` pour la conformité.

### 4.2 Versions et lib_deps

**Point positif :** Toutes les `lib_deps` sont verrouillées à une version précise dans les `platformio.ini` (`@7.4.3`, `@3.4.10`, etc.), ce qui garantit des builds reproductibles.

| Firmware | Version firmware |
|----------|-----------------|
| n3pp | 4.39 |
| msp | 2.43 |
| ffp5cs | 13.92 |
| uploadphotosserver | 2.39 |
| poissonglouton | 0.1.2 |

### 4.3 OTA (Over-The-Air)

- `n3pp` et `msp` : OTA via `shared/n3_common/src/n3_ota.cpp`.
- `ffp5cs` : OTA complet (`ota_manager`), gestion des partitions, rollback.
- `uploadphotosserver` : OTA inclus via `n3_ota`.

**✅ Mise à jour (2026-06) :** `n3_ota` **vérifie désormais le `sha256`** du binaire téléchargé avant flash, et une **signature ECDSA P-256** si le champ `signature` est présent dans `metadata.json` (`verifyFirmwareSignature` / `verifyRemoteFirmwareIntegrity`). Le transport reste HTTP, mais l'intégrité/authenticité du binaire est protégée. Le constat « envisager la vérification de signature OTA » est donc **traité** pour les firmwares utilisant `n3_common`.

### 4.4 Deep sleep et cohérence des wakeup

- `n3pp` et `msp` : Deep sleep géré via `shared/n3_sleep`, `RTC_DATA_ATTR` pour la persistance des variables.
- `uploadphotosserver` (+ variantes legacy `*_deppsleep`) : Deep sleep sur ESP32-CAM avec gestion d'exposition caméra au réveil.
- `poissonglouton` : Deep sleep avec réveil IR (ext0) ou timer fallback, optimisé alimentation solaire.

### 4.5 `à voir/` — Dossier non maintenu

- `LVGL_Widgets` : Prototype ESP32-S3 avec LVGL, secrets désormais externalisés via `credentials.h` (voir §3.2), code non modulaire.
- `ratata` : Projet voiture/robot (kit ZYC0108-EN), sans rapport avec les firmwares IoT.

Ces dossiers ne sont pas buildés en production. Le risque credentials de `LVGL_Widgets` est résolu (§3.2) ; ils restent toutefois non maintenus.

---

## 5. Build et configuration PlatformIO

### 5.1 Partition flash

| Firmware | Partition |
|----------|-----------|
| n3pp | `../config/partitions/min_spiffs.csv` |
| msp | `../config/partitions/min_spiffs.csv` |
| ffp5cs | Partition dédiée (ESP-IDF, gestion complète) |
| uploadphotosserver | Partition par défaut (AI-THINKER CAM) |

Le fichier `config/partitions/min_spiffs.csv` est **présent** dans le dépôt et partagé entre `n3pp` et `msp`. **Résolu.**

### 5.2 Ports COM

Les `platformio.ini` de `n3pp` et `msp` ne contiennent plus de `upload_port = COM3` figé (supprimé lors d'un audit précédent). **Résolu.**

---

## 6. Tableau récapitulatif des actions

| Priorité | Action | Firmware | État |
|----------|--------|----------|------|
| 🟠 Moyenne | Remplacer `sprintf` par `snprintf` dans `n3_hmac.cpp:29` | `shared/n3_hmac` | **À faire** |
| 🟡 Basse | Migrer serveur HTTP → HTTPS pour les envois de données | `n3pp`, `msp`, `uploadphotosserver` | Préparé sur ffp5cs (`USE_HTTPS_ENDPOINTS`) |
| ✅ Fait | Credentials externalisés (`credentials.h`) | `à voir/LVGL_Widgets` | Fait (2026-06) |
| ✅ Fait | Fallback secrets durci (placeholder + static_assert PROD) | `ffp5cs` | Fait (2026-06) |
| ✅ Fait | Vérification sha256 + signature ECDSA OTA | `n3_common` (n3pp/msp/cam) | Fait |
| ✅ Fait | Modularisation en modules | `n3pp`, `msp` | Fait |
| ✅ Fait | Bibliothèques partagées (`shared/`) | Tous | Fait |
| ✅ Fait | `credentials.h` dans `.gitignore` | Tous | Fait |
| ✅ Fait | Versions `lib_deps` verrouillées | Tous | Fait |
| ✅ Fait | HMAC-SHA256 (`X-Signature`) | `n3pp`, `msp` | Fait |
| ✅ Fait | OTA sur tous les firmwares principaux | Tous | Fait |
| ✅ Fait | `FIRMWARE_VERSION` harmonisée | Tous | Fait |

---

## 7. Résumé exécutif

Le dépôt est dans un **état globalement bon** : la modularisation est réalisée, les bibliothèques partagées évitent la duplication, les secrets sont externalisés via `credentials.h` (non versionné), et les dépendances sont verrouillées.

**État à jour (2026-06) :** le constat critique de mars 2026 (credentials en dur dans `à voir/LVGL_Widgets`) est **résolu** (inclusion de `credentials.h`). Le fallback secrets de `ffp5cs` est durci (placeholder `CHANGEZ_MOI` + `static_assert` bloquant en PROD). L'OTA vérifie le `sha256` et une signature ECDSA P-256 optionnelle.

Il subsiste surtout l'amélioration `snprintf` dans `n3_hmac.cpp:29` (robustesse, impact sécurité nul) et la migration HTTP → HTTPS des envois de données (préparée sur `ffp5cs`).

---

*Rapport initial : 11 mars 2026 — branche `claude/audit-main-firmwares-5CTIL`. Actualisé le 2026-06-09 pour alignement avec le code courant.*
