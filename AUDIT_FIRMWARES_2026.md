# Audit des principaux firmwares — n3_firmwires

**Date :** 11 mars 2026
**Branche :** `claude/audit-main-firmwares-5CTIL`
**Périmètre :** Tous les firmwares du dépôt `/n3_firmwires`

---

## 1. Inventaire des firmwares

| Firmware | Dossier | Cible | Version | Rôle |
|----------|---------|-------|---------|------|
| **N3PhasmesProto** | `n3pp4_2/` | ESP32 | 4.11 | Contrôle serre/aquaponie (capteurs sol/air, pompe, relais, OLED, web, mail) |
| **MeteoStationPrototype** | `msp2_5/` | ESP32 | 2.12 | Station météo + tracker solaire (DHT×2, DS18B20, LDR×4, servos, OLED, web, mail) |
| **FFP5CS** | `ffp5cs/` | ESP32 / ESP32-S3 | 12.35 | Firmware caméra/aquarium avancé (OTA, RTC DS3231, SD, WebSocket temps réel, NVS, HMAC) |
| **UploadPhotoServer (unifié)** | `uploadphotosserver/` | ESP32-CAM | 2.8 | Upload photo vers iot.olution.info (cibles msp1/n3pp/ffp3 via build_flags) |
| **UploadPhotoServer FFP3 deep sleep** | `uploadphotosserver_ffp3_1_5_deppsleep/` | ESP32-CAM | 1.5 | Variante FFP3 avec deep sleep |
| **UploadPhotoServer N3PP deep sleep** | `uploadphotosserver_n3pp_1_6_deppsleep/` | ESP32-CAM | 1.6 | Variante N3PP avec deep sleep |
| **UploadPhotoServer MSP1** | `uploadphotosserver_msp1/` | ESP32-CAM | — | Variante MSP1 standalone |
| **LVGL_Widgets** | `à voir/LVGL_Widgets/` | ESP32 | — | Prototype IHM LVGL (non actif) |
| **Ratata** | `à voir/ratata/` | Arduino | — | Voiture/robot (projet annexe) |

---

## 2. Structure et architecture

### 2.1 Bibliothèques partagées (`shared/`)

| Lib | Rôle |
|-----|------|
| `n3_wifi` | Connexion WiFi avec scan RSSI, tri par signal, retry BSSID |
| `n3_http` | Client HTTP mutualisé (POST données, requêtes serveur) |
| `n3_time` | Synchronisation NTP et gestion heure ESP32Time |
| `n3_common` | Constantes partagées (`n3_defaults.h`), OTA (`n3_ota.cpp`) |
| `n3_battery` | Mesure tension batterie via pont diviseur (moyenne glissante) |
| `n3_data` | Structures de données capteurs |
| `n3_display` | Abstraction affichage OLED SSD1306 |
| `n3_sleep` | Deep sleep mutualisé |
| `n3_hmac` | Signature HMAC-SHA256 via mbedTLS (header `X-Signature`) |

**Point positif :** La factorisation en bibliothèques partagées est bien réalisée. Les firmwares `n3pp4_2`, `msp2_5` et `uploadphotosserver` utilisent tous `shared/` via `lib_extra_dirs = ../shared` dans PlatformIO.

### 2.2 Modularisation des firmwares principaux

**n3pp4_2** (1 092 lignes réparties en 5 modules) :
```
src/main.cpp             (429 lignes — setup/loop, orcherstration)
src/n3pp_automation.cpp  (300 lignes — arrosage, nourrissage, relais)
src/n3pp_display.cpp     ( 91 lignes — OLED)
src/n3pp_network.cpp     (156 lignes — WiFi, HTTP, NTP, web server)
src/n3pp_sensors.cpp     (116 lignes — DHT, sol, luminosité, batterie)
```

**msp2_5** (1 013 lignes réparties en 5 modules) :
```
src/main.cpp             (291 lignes — setup/loop)
src/msp_automation.cpp   (156 lignes — tracker solaire, servos, relais)
src/msp_display.cpp      ( 99 lignes — OLED)
src/msp_network.cpp      (141 lignes — WiFi, HTTP, NTP, web server)
src/msp_sensors.cpp      (326 lignes — DHT×2, DS18B20, LDR×4, batterie, pluie)
```

**ffp5cs** (20 728 lignes, très bien structuré en 30 modules) — architecture la plus aboutie.

---

## 3. Gestion des secrets

### 3.1 Mécanisme en place (correct)

- Un fichier `credentials.h.example` à la racine sert de template clair et documenté.
- `credentials.h` (copie locale non versionnée) est dans `.gitignore`.
- `n3pp4_2` et `msp2_5` incluent `credentials.h` via leur `n3pp_config.h` / `msp_config.h`.
- `ffp5cs` utilise `secrets_config.h` (priorité) ou `../../credentials.h` (fallback) avec cascade `#if __has_include`.
- `uploadphotosserver` inclut directement `credentials.h`.

### 3.2 Problèmes identifiés

#### 🔴 CRITIQUE — Credentials en dur dans `à voir/LVGL_Widgets/src/main.cpp`

```cpp
// Lignes 78-82 et 97 :
#define SMTP_HOST       "smtp.gmail.com"
#define AUTHOR_EMAIL    "arnould.svt@gmail.com"
#define AUTHOR_PASSWORD "ddbfvlkssfleypdr"   // ← mot de passe d'application Gmail en clair
String inputMessageMailAd = "oliv.arn.lau@gmail.com";
String apiKeyValue = "fdGTMoptd5CD2ert3";   // ← clé API IoT en clair
```

**Risque :** Exposition de credentials réels dans le dépôt (même dans un sous-dossier "à voir"). Si le dépôt est partagé ou rendu public, ces informations sont compromises.
**Action requise :** Révoquer le mot de passe d'application Gmail `ddbfvlkssfleypdr`, remplacer par `#include "credentials.h"`.

#### 🟠 MOYEN — Valeurs de fallback exposées dans `ffp5cs/include/config.h`

```cpp
// Lignes 21-24 (fallback sans secrets_config.h ni credentials.h) :
constexpr const char* WEB_AUTH_USER = "admin";
constexpr const char* WEB_AUTH_PASS = "ffp3";   // ← mot de passe par défaut visible
```

**Risque :** Si un build est effectué sans fichier de secrets, le firmware embarque des credentials prévisibles. L'interface web sera accessible avec `admin/ffp3`.
**Recommandation :** Documenter clairement que ce fallback est pour le développement uniquement ; ajouter un `#warning "Compilation sans secrets_config.h — credentials par défaut utilisés"`.

#### 🟡 INFO — Communications HTTP non chiffrées vers le serveur

Les URLs serveur dans tous les firmwares utilisent `http://` :
```cpp
// n3pp4_2, msp2_5 :
"http://iot.olution.info/n3pp/..."
"http://iot.olution.info/msp1/..."
```

**Risque :** Les données de capteurs et la clé API transitent en clair sur le réseau. HMAC-SHA256 (`X-Signature`) protège l'intégrité mais pas la confidentialité.
**Recommandation :** Migrer vers HTTPS si le serveur le supporte (l'ESP32 supporte TLS via `WiFiClientSecure`).

---

## 4. Qualité du code

### 4.1 Fonctions C non sécurisées

| Fichier | Ligne | Problème |
|---------|-------|---------|
| `shared/n3_hmac/src/n3_hmac.cpp` | 29 | `sprintf(hexOutput + (i*2), "%02x", ...)` — buffer connu (65 octets, boucle 32 itérations), risque nul mais non idiomatique |

**Note :** `ffp5cs` utilise correctement `snprintf` et a une fonction utilitaire `Utils::safeStrncpy`. Les firmwares `n3pp4_2` et `msp2_5` n'utilisent pas de `sprintf`/`strcpy` non bornés dans leur code propre.

Le `sprintf` dans `n3_hmac.cpp` écrit exactement `2 × 32 = 64` caractères dans un buffer de 65 → pas de dépassement effectif, mais à remplacer par `snprintf` pour la conformité.

### 4.2 Versions et lib_deps

**Point positif :** Toutes les `lib_deps` sont verrouillées à une version précise dans les `platformio.ini` (`@7.4.3`, `@3.4.10`, etc.), ce qui garantit des builds reproductibles.

| Firmware | Version firmware |
|----------|-----------------|
| n3pp4_2 | 4.11 |
| msp2_5 | 2.12 |
| ffp5cs | 12.35 |
| uploadphotosserver | 2.8 |

### 4.3 OTA (Over-The-Air)

- `n3pp4_2` et `msp2_5` : OTA présent via `shared/n3_common/src/n3_ota.cpp` + `upload_hook_otadata.py`.
- `ffp5cs` : OTA complet avec `ota_manager.cpp` (2 035 lignes), gestion des partitions, rollback.
- `uploadphotosserver` : OTA inclus via `n3_ota`.

**Point d'attention :** L'OTA s'effectue en HTTP (update via serveur HTTP). Si un attaquant est sur le même réseau, il pourrait théoriquement injecter un firmware malveillant. Envisager la vérification de signature OTA.

### 4.4 Deep sleep et cohérence des wakeup

- `n3pp4_2` et `msp2_5` : Deep sleep géré via `shared/n3_sleep`, `RTC_DATA_ATTR` pour la persistance des variables.
- `uploadphotosserver_*_deppsleep` : Deep sleep sur ESP32-CAM avec gestion d'exposition caméra au réveil.

### 4.5 `à voir/` — Dossier non maintenu

- `LVGL_Widgets` : Prototype Arduino avec LVGL, credentials en dur (voir §3.2), code non modulaire.
- `ratata` : Projet voiture/robot Arduino, sans rapport avec les firmwares IoT.

Ces dossiers ne sont pas buildés en production mais leur présence dans le dépôt crée un risque (credentials exposés dans LVGL_Widgets).

---

## 5. Build et configuration PlatformIO

### 5.1 Partition flash

| Firmware | Partition |
|----------|-----------|
| n3pp4_2 | `../config/partitions/min_spiffs.csv` |
| msp2_5 | `../config/partitions/min_spiffs.csv` |
| ffp5cs | Partition dédiée (ESP-IDF, gestion complète) |
| uploadphotosserver | Partition par défaut (AI-THINKER CAM) |

Le fichier `config/partitions/min_spiffs.csv` est partagé entre `n3pp4_2` et `msp2_5` — à vérifier qu'il est bien présent dans le dépôt.

### 5.2 Ports COM

Les `platformio.ini` de `n3pp4_2` et `msp2_5` ne contiennent plus de `upload_port = COM3` figé (supprimé lors d'un audit précédent). **Résolu.**

---

## 6. Tableau récapitulatif des actions

| Priorité | Action | Firmware | État |
|----------|--------|----------|------|
| 🔴 Haute | Révoquer et supprimer credentials Gmail en dur (`AUTHOR_PASSWORD "ddbfvlkssfleypdr"`) | `à voir/LVGL_Widgets` | **À faire** |
| 🔴 Haute | Remplacer API key en dur (`fdGTMoptd5CD2ert3`) par `#include "credentials.h"` | `à voir/LVGL_Widgets` | **À faire** |
| 🟠 Moyenne | Ajouter `#warning` si fallback sans secrets dans `ffp5cs/config.h` | `ffp5cs` | **À faire** |
| 🟠 Moyenne | Remplacer `sprintf` par `snprintf` dans `n3_hmac.cpp:29` | `shared/n3_hmac` | **À faire** |
| 🟡 Basse | Migrer serveur HTTP → HTTPS pour les envois de données | `n3pp4_2`, `msp2_5`, `uploadphotosserver` | Analyse requise |
| 🟡 Basse | Vérification de signature pour les mises à jour OTA | Tous | Analyse requise |
| ✅ Fait | Modularisation en 5 modules | `n3pp4_2`, `msp2_5` | Fait |
| ✅ Fait | Bibliothèques partagées (`shared/`) | Tous | Fait |
| ✅ Fait | `credentials.h` dans `.gitignore` | Tous | Fait |
| ✅ Fait | Versions `lib_deps` verrouillées | Tous | Fait |
| ✅ Fait | HMAC-SHA256 (`X-Signature`) | `n3pp4_2`, `msp2_5` | Fait |
| ✅ Fait | OTA sur tous les firmwares principaux | Tous | Fait |
| ✅ Fait | `FIRMWARE_VERSION` harmonisée | Tous | Fait |

---

## 7. Résumé exécutif

Le dépôt est dans un **état globalement bon** : la modularisation est réalisée, les bibliothèques partagées évitent la duplication, les secrets sont externalisés via `credentials.h` (non versionné), et les dépendances sont verrouillées.

**Le seul problème critique** est la présence de credentials Gmail réels (`arnould.svt@gmail.com` / `ddbfvlkssfleypdr`) et d'une clé API (`fdGTMoptd5CD2ert3`) en dur dans `à voir/LVGL_Widgets/src/main.cpp`. **Ces credentials doivent être révoqués immédiatement** si ce dépôt a été partagé ou est susceptible de l'être.

Les actions de priorité moyenne (avertissement de compilation, `snprintf`) sont des améliorations de robustesse sans impact sécurité immédiat. Les pistes HTTPS/OTA signé sont des améliorations à plus long terme.

---

*Rapport généré le 11 mars 2026 — branche `claude/audit-main-firmwares-5CTIL`.*
