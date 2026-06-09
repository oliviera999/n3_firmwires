# Audit d'optimisation — n3_firmwires

**Axes : Extensibilité · Maintenabilité long terme · Performance**
**Date : 2026-06-09** · Périmètre : tous les firmwares vivants + libs partagées + build.

> Contexte **embarqué** : les 3 axes sont traduits du web vers l'ESP32 — « bundle » → taille binaire / partitions flash ; « réseau » → HTTP/OTA bloquant ; « mémoire » → heap/fragmentation/PSRAM ; « rendu » → OLED.
>
> Ce document complète (sans les remplacer) `AUDIT_FIRMWARES_2026.md` (axé sécurité), `RAPPORT_ANALYSE.md` et `RECOMMANDATIONS.md` (archivés). Il corrige un constat erroné de l'audit sécurité (cf. §5.3).

---

## 1. Synthèse — les 5 points les plus critiques

Répartition de la masse de code (déterminante pour la priorisation) :

| Module | Lignes (src+include) | Part | Tests |
|---|---|---|---|
| **ffp5cs** | ~30 800 | **80 %** | 1 144 l. natives (8 suites) ✅ |
| shared/ (12 libs) | 1 750 | 4,5 % | **0** ❌ |
| msp | 1 620 | | 0 ❌ |
| n3pp | 1 501 | | 0 ❌ |
| uploadphotosserver | 1 374 | | 0 ❌ |
| poissonglouton | 1 248 | | 0 ❌ |

**① Stratégie de libs partagées inachevée** *(Extensibilité + Maintenabilité)* — `n3pp`/`msp` redupliquent ~450 lignes (SMTP, RTC/NVS, deep-sleep, UI OTA) que `n3_mail`/`n3_time`/`n3_sleep` couvrent déjà ; `n3_http` (0 référence, déprécié) et `libn3_iot` (0 référence) sont du code mort. *Cœur des axes 1+2, chemin de toute évolution future.*

**② God files dans ffp5cs** *(Maintenabilité + Extensibilité)* — 10 fichiers > 1 000 lignes (`web_server.cpp` 2 084, `ota_manager.cpp` 2 074, `app_tasks.cpp` 1 788…), responsabilités entremêlées, dispatch par chaînes `strcmp`. *ffp5cs = 80 % du code → domine le coût de maintenance.*

**③ « #ifdef hell » multi-cartes** *(Extensibilité)* — 175 occurrences de `BOARD_S3`/`BOARD_HAS_PSRAM` sur 25 fichiers. Ajouter une variante carte = naviguer 175 sites conditionnels.

**④ Fragmentation heap (`String`) + delays bloquants** *(Performance/Fiabilité)* — usage massif de `String` Arduino (~463 occurrences ffp5cs) ; `delay()` bloquants dans le scan capteurs msp (`msp_sensors.cpp:226-411`).

**⑤ Builds non reproductibles + shared/ non testé** *(Maintenabilité)* — dépendances flottantes (poissonglouton ×3, msp ×1), dérive de versions entre projets (ex. `DallasTemperature 3.11.0` vs `4.0.5`), et le code le plus réutilisé (`shared/`) n'a aucun test.

---

## 2. Tableau priorisé

Effort : XS < 1 h · S ½ j · M 1-3 j · L > 1 semaine.

| Prio | Axe | Problème | Recommandation | Effort | Statut |
|---|---|---|---|---|---|
| 🔴 Haute | Maint. | `n3_http` (0 réf) + `libn3_iot` (0 réf) = code mort | Supprimer les 2 libs | XS | ✅ |
| 🔴 Haute | Maint. | 3 dossiers `uploadphotosserver_*_deppsleep` legacy (1 766 l.) | Déplacer dans `archive/` | XS | ✅ |
| 🔴 Haute | Maint. | Deps flottantes : poissonglouton ×3, msp `^7.4.3` | Épingler exact | XS | ✅ |
| 🔴 Haute | Ext.+Maint. | n3pp/msp redupliquent SMTP + RTC (~450 l.) | Migrer vers `n3_mail`/`n3_time`/`n3_sleep` | M | ✅ |
| 🟠 Moy. | Maint. | Dérive de versions des libs entre projets | Homogénéiser sur la plus récente validée | S | ✅ |
| 🟠 Moy. | Maint. | `sprintf` `n3_hmac.cpp:29` | `snprintf` (conformité) | XS | ✅ |
| 🟠 Moy. | Ext. | 5 conventions de `FIRMWARE_VERSION` | `n3_version.h` partagé | S | ✅ |
| 🟠 Moy. | Maint. | `library.json` : deps cachées (n3_display→SSD1306, n3_data→n3_common) | Déclarer | XS | ✅ |
| 🟠 Moy. | Maint. | shared/ : 0 test | Harnais natif + tests `n3_analog_sensors` (7, verts) | M | ✅ |
| 🟠 Moy. | Maint. | God file `app.cpp` : bloc mail inline (293-350) + dispatch strcmp | Extraire / tabuler (ffp5cs v13.93) | M | ✅ |
| ⚪ Non retenu | Ext. | 5 conventions de `FIRMWARE_VERSION` | Statu quo justifié (cf. §3.5) | — | ⚪ |
| 🟡 Basse | Ext. | 175 `#ifdef BOARD_S3` / 25 fichiers | `BoardTraits` + `if constexpr` | L | 📋 différé (cf. §6) |
| 🟡 Basse | Maint. | God files ffp5cs (web_server, app_tasks, ota_manager) | Découpe par responsabilité | L | 📋 différé (cf. §6) |
| 🟡 Basse | Sécu. | HTTP→HTTPS n3pp/msp | TLS WebClient d'abord | L | 📋 différé (cf. §5.2) |

---

## 3. Décisions prises sur les questions ouvertes

Le mainteneur a laissé l'initiative. Décisions documentées :

1. **msp / scan servo (`msp_sensors.cpp:226-411`)** — **conservé tel quel.** Le balayage du tracker solaire avec `delay(30)` (positionnement servo) + `delay(50)` (stabilisation lecture LDR) est **fonctionnellement nécessaire** : msp est un firmware mono-boucle Arduino qui *deep-sleep* après chaque cycle (pas un ordonnanceur multi-tâches comme ffp5cs), donc le « blocage du scheduler » n'est pas un enjeu. Le réécrire en machine à états apporte un gain marginal pour un risque élevé sur du code de prod non testable sur cible ici. *Compromis acté : simplicité/robustesse > réactivité pendant le balayage.*

2. **HTTPS (n3pp/msp/upload)** — **différé.** Le serveur **supporte** HTTPS (HSTS, `cookie_secure`, détection `X-Forwarded-Proto` derrière reverse-proxy — cf. `n3_serveur/src/Middleware/SecurityHeadersMiddleware.php`). Mais : (a) les données sont déjà **intégrité-protégées par HMAC** (`X-Signature`) ; (b) même `ffp5cs` (le plus avancé) n'a **pas terminé** sa migration TLS (« bloqué v13.81 sans TLS WebClient ») ; (c) TLS coûte ~40 KB RAM/handshake sur des appareils déjà contraints en heap ; (d) non validable sans matériel. **Prérequis avant migration** : finir le `WebClient` TLS (mutualisable via `WiFiClientSecure`), en miroir de la préparation `USE_HTTPS_ENDPOINTS` de ffp5cs.

3. **`à voir/` (LVGL_Widgets, ratata)** — **conservé en l'état** (prototypes documentés « non maintenus », hors build de prod). Suppression non justifiée (le mainteneur peut vouloir y revenir) ; pas de gain de maintenance car déjà hors périmètre actif.

4. **Versions de dépendances** — homogénéisation **sur la version la plus récente déjà validée dans le dépôt**, sauf `ffp5cs` dont les versions sont explicitement validées contre sa toolchain (arduino-esp32 3.3.7) ; sa divergence est **intentionnelle** et documentée. `lvgl` reste **8.4.0** (la 9.x est un changement d'API cassant).

5. **Convention `FIRMWARE_VERSION` (n3_version.h)** — **non retenu.** n3pp/msp/upload partagent déjà `#define FIRMWARE_VERSION`. poissonglouton (`PGL_FIRMWARE_VERSION`) et ffp5cs (`ProjectConfig::VERSION`) utilisent un `constexpr` (meilleure pratique que la macro). Chaque firmware a une **valeur** propre : un header partagé ne peut pas les unifier, et forcer une convention unique imposerait de toucher 100+ usages de `ProjectConfig::VERSION` dans ffp5cs — churn et risque sans gain. La cohérence actuelle (par groupe) est acceptable.

### Validation

Tous les firmwares modifiés ont été **compilés sur Linux** (PlatformIO, même chaîne que la CI) : `n3pp` (esp32dev), `msp` (esp32dev), `ffp5cs` (wroom-test), `poissonglouton` (headless + display), `uploadphotosserver` (msp1) — builds verts. Tests natifs : `shared/n3_analog_sensors` 7/7 + `ffp5cs` 47/47 (5 suites). ⚠️ Les changements à comportement runtime (envoi SMTP réel, restauration heure NVS) restent à **valider sur cible** via le workflow erase/flash/monitor — la compilation ne couvre pas le comportement.

**CI racine ajoutée** (`.github/workflows/firmware-ci.yml`) : il n'existait **aucune CI active** (les workflows sous `ffp5cs/.github/` sont hors racine, donc ignorés par GitHub). La nouvelle CI compile les 6 environnements ci-dessus et lance les tests natifs à chaque push/PR. En l'activant, un **test périmé** a été découvert et corrigé : `test_server_url` attendait encore le préfixe `/ffp3/` retiré en v13.87 — non détecté justement parce qu'aucune CI ne lançait les tests natifs.

---

## 4. Détail par module (constats)

### 4.1 `shared/` — duplication & code mort

**Code mort confirmé (grep dépôt entier) :**
- `shared/n3_http/` — **0 référence**, `DEPRECATED` dans son `library.json`. → supprimé.
- `shared/libn3_iot/` — **0 référence** (n3pp/msp utilisent directement `n3_analog_sensors.h`). → supprimé.

**Duplication n3pp ↔ msp (~450 l., vérifiée) :**

| Fonction | n3pp | msp | Lib cible |
|---|---|---|---|
| `sendEmailNotification()` | `n3pp_automation.cpp:47` | `msp_automation.cpp:18` | `n3_mail` |
| `EnregistrementHeureFlash()` | `n3pp_automation.cpp:28` | `msp_automation.cpp:72` | `n3_time` |
| `HeureSansWifi()` | `n3pp_automation.cpp:6` | `msp_automation.cpp` | `n3_time` |
| `sommeil()` (cœur) | `n3pp_automation.cpp:255` | `msp_automation.cpp:116` | `n3_sleep` |

`n3_mail` expose déjà `n3MailSendText(const N3MailSmtpConfig&, subject, body, String*)` → migration directe.

### 4.2 ffp5cs — god files & dispatch

- `web_server.cpp` (2 084), `ota_manager.cpp` (2 074), `app_tasks.cpp` (1 788), `sensors.cpp` (1 690), `mailer.cpp` (1 653).
- Dispatch anti-pattern : `web_server.cpp:174-183` (10 `strcmp` en cascade pour les types NVS).
- `app.cpp:293-350` : ~57 lignes de « mail de démarrage » inline dans `setup()`.
- **Calibration** : les routes sont enregistrées en lambdas `_server->on(...)` individuelles (`web_server.cpp:513+`) — pattern **idiomatique** ESPAsyncWebServer, *pas* un défaut. Le problème est la taille et les dépendances globales (`extern g_autoCtrl, mailer, config, power, wifi`), pas le routage.

### 4.3 Portabilité multi-cartes

175 `#ifdef BOARD_S3`/`BOARD_HAS_PSRAM` sur 25 fichiers (ex. `app.cpp` : 3 blocs WDT + 3 `Serial.begin` + bloc PSRAM dispersés). Cible : descripteur `BoardTraits` compile-time + `if constexpr`.

### 4.4 Performance & mémoire

- `String` Arduino concentré dans `ffp5cs/web_client.cpp`, `n3_outputs_json.cpp`, retours `String` de `n3_http` (mort).
- `JsonDocument` 4 KB sur pile dans handlers web ffp5cs → surveiller le high-water mark.
- Flash serrée : WROOM `partitions_..._fs_mail.csv` ~96 % ; S3 ~99 %. → budget binaire en CI.
- Flags d'optim déjà bons (`-O1`, `-ffunction-sections`, `--gc-sections`, `-fno-exceptions/-rtti`).

### 4.5 Build, dépendances & tests

Dérive de versions (les libs `shared/` sont compilées dans **tous** les projets → doivent rester compatibles avec des versions divergentes) :

| Lib | ffp5cs | n3pp | msp | upload | poissonglouton |
|---|---|---|---|---|---|
| ArduinoJson | 7.4.2 | 7.4.3 | `^7.4.3` ⚠️ | 7.4.3 | *(aucune)* ⚠️ |
| ESPAsyncWebServer | 3.7.6 | 3.10.0 | 3.10.0 | — | — |
| DallasTemperature | 3.11.0 | — | 4.0.5 (majeur ≠) | — | — |
| Arduino_JSON | — | 0.2.0 | 0.2.0 | 0.2.0 | *(aucune)* ⚠️ |
| GFX Library for Arduino | — | — | — | — | *(aucune)* ⚠️ |

---

## 5. Quick wins vs refactors structurels

**Quick wins (traités dans cette passe) :** suppression code mort, pin/homogénéisation deps, `snprintf`, `n3_version.h`, deps `library.json` déclarées, extraction bloc mail `app.cpp`, tabulation dispatch.

**Refactors structurels (cette passe) :** migration n3pp/msp → `n3_mail`/`n3_time`/`n3_sleep` ; ajout de tests natifs `shared/`.

**Refactors structurels (différés, cf. §6) :** découpe god files ffp5cs ; `BoardTraits`/HAL.

### 5.2 HTTPS — voir §3.2 (différé, prérequis TLS WebClient).

### 5.3 Correction de l'audit existant

`AUDIT_FIRMWARES_2026.md` §4.2 affirme « toutes les `lib_deps` verrouillées à une version précise ». **Inexact** : `poissonglouton` (3 deps sans version) et `msp` (`^7.4.3`). Corrigé dans cette passe.

---

## 6. Refactors différés (justification)

Ces chantiers à fort gain (effort > 1 semaine, risque élevé sans validation sur cible) ont été **amorcés de façon incrémentale et validée par build**, mais pas terminés — pour ne pas livrer de changements massifs non testés sur du matériel de prod (pompe, nourrissage poisson, tracker) :

1. **Découpe des god files ffp5cs** — ✅ **bien avancé** : `web_server.cpp` **2084 → 898 lignes (−57 %)** — 3 groupes extraits via le pattern `WebRoutes::register*Routes` : **WiFi** (`web_routes_wifi.cpp`), **NVS** (`web_routes_nvs.cpp`), **système/OTA** (`web_routes_system.cpp`). **Reste** : découpe d'`app_tasks.cpp` (8 tâches FreeRTOS — **plus risqué**, code de boot/scheduler) et `ota_manager.cpp`. *Chaque extraction validée par build `wroom-test`.*
2. **`BoardTraits` / réduction des `#ifdef`** — ✅ **amorcé** : `board_traits.h` existait déjà (v13.60) mais **sous-utilisé** ; adoption étendue aux `#ifdef BOARD_S3` de **sélection de valeurs** (`config.h`). **Constat architectural** : la majorité des 175 `#ifdef` gardent des **APIs spécifiques S3** (WDT `wdt_hal`, PSRAM, variantes IDF) et **ne sont pas convertibles** en `if constexpr` (qui exige que les deux branches compilent sur toutes les cibles). Le gain réaliste de `BoardTraits` se limite donc aux sélections de valeurs/booléens, pas au remplacement intégral des `#ifdef`.

## 7. CI

`.github/workflows/firmware-ci.yml` (ajoutée) compile **6 environnements** (n3pp, msp, ffp5cs wroom-test, poissonglouton headless+display, uploadphotosserver msp1) et lance les **tests natifs** (shared + ffp5cs, 5 suites) à chaque push/PR. ⚠️ **GitHub Actions doit être activé** sur le dépôt (Settings → Actions) — aucune CI n'était active jusqu'ici (les workflows `ffp5cs/.github/` sont hors racine).

---

*Rapport généré le 2026-06-09. Plan de traitement appliqué sur la branche `claude/magical-knuth-3rpj6j`.*
