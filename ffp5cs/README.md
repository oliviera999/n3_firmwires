# 🐟 FFP5CS ESP32 Aquaponie Controller

**Système de contrôle automatisé pour aquaponie avec ESP32**

[![Version](https://img.shields.io/badge/version-13.80-blue.svg)](VERSION.md)
[![ESP32](https://img.shields.io/badge/ESP32-WROOM%20%7C%20S3-green.svg)](platformio.ini)
[![Framework](https://img.shields.io/badge/framework-Arduino-orange.svg)](platformio.ini)
[![License](https://img.shields.io/badge/license-MIT-yellow.svg)](LICENSE)

---

## 🚀 Démarrage rapide

### 📖 Documentation
👉 **[📚 Documentation technique](docs/README.md)** — structure, config, rapports

### ⚡ Installation rapide
1. **Cloner le projet**
   ```bash
   git clone <repository-url>
   cd ffp5cs
   ```

2. **Configuration PlatformIO**
   ```bash
   pio run -e wroom-test       # ESP32-WROOM (recommandé pour le dev)
   # ou
   pio run -e wroom-s3-test    # ESP32-S3 (16MB, sans PSRAM)
   ```

3. **Flash initial**
   ```bash
   pio run -e wroom-test -t upload
   ```

4. **Monitoring**
   ```bash
   pio device monitor -e wroom-test
   ```

---

## 🎯 Fonctionnalités principales

### 🌊 Gestion aquaponie
- **Nourrissage automatique** programmable (matin/midi/soir)
- **Contrôle niveau d'eau** (aquarium, potager, réserve)
- **Remplissage automatique** depuis la réserve
- **Détection marées** pour mise en veille

### 🌡️ Monitoring environnement
- **Température air/eau** (DHT22, DS18B20)
- **Humidité** et **luminosité**
- **Alertes email** configurables
- **Interface web** temps réel

### ⚡ Optimisations système
- **Mode veille** intelligent (Light Sleep)
- **Reconnexion WiFi** automatique
- **Mise à jour OTA** (firmware + filesystem)
- **Monitoring mémoire** et performance

---

## 🏗️ Architecture

### 📁 Structure du projet
```
ffp5cs/
├── src/                    # Code source ESP32
│   ├── automatism/         # Modules spécialisés (feeding, sleep, etc.)
│   ├── app.cpp            # Point d'entrée principal (setup/loop)
│   ├── sensors.cpp        # Gestion capteurs bas niveau
│   ├── actuators.cpp      # Contrôle actionneurs bas niveau
│   └── web_server.cpp     # Interface web (AsyncWebServer)
├── include/                # En-têtes (Headers)
│   ├── config.h           # Configuration unifiée du projet
│   ├── automatism.h       # API de l'automate
│   └── web_server.h       # API du serveur web
├── data/                  # Interface web (index.html, pages/, assets/, sw.js)
├── docs/                  # Documentation
│   ├── README.md         # Vue d'ensemble
│   ├── technical/        # Références techniques (seuils ESP32 vs serveur)
│   ├── reports/          # Rapports (conformité, NVS, corrections, etc.)
│   └── references        # Référence emails (32 types)
└── platformio.ini        # Configuration build
```

### 🔧 Environnements de build (extrait — voir `platformio.ini` pour la liste complète)
- **`wroom-prod`** — Production ESP32-WROOM (LTO désactivé, sans web async, optimisé flash)
- **`wroom-test`** — Test ESP32-WROOM (debug activé, logs série, web async)
- **`wroom-beta`** — Bêta ESP32-WROOM (canal OTA test, endpoints `*-test`)
- **`wroom-beta-local`** — Bêta contre serveur Docker local (`USE_LOCAL_SERVER_ENDPOINTS`)
- **`wroom-s3-test`** — Test ESP32-S3 16 Mo sans PSRAM (env serveur `s3test`)
- **`wroom-s3-test-psram`** / **`-psram-v2`** — ESP32-S3 16 Mo avec PSRAM 8 Mo
- **`wroom-s3-prod`** — Production ESP32-S3 16 Mo

---

## 📊 État du projet

### ✅ Fonctionnalités stables
- [x] Nourrissage automatique
- [x] Monitoring capteurs
- [x] Interface web responsive
- [x] Mise à jour OTA
- [x] Mode veille optimisé
- [x] Reconnexion WiFi

### 🔄 Améliorations Récentes (audit général 2026-05)

**v13.80 — migration contrat firmware↔serveur (mode dual rétrocompatible)**
- [x] Flag `USE_HTTPS_ENDPOINTS` + env `wroom-prod-https` (pilote opt-in)
- [x] Module `hmac_sign.cpp/h` : HMAC-SHA256 (mbedtls) avec nonce HW RNG
- [x] En-têtes `X-Sig-Timestamp/Nonce/Hmac` ajoutés aux POST/GET (en complément `api_key`)
- [x] `secrets_config.h.example` : sections `API_SIG_SECRET` et `OTA_PUBLIC_KEY_HEX` documentées
- [x] Doc complète : `docs/technical/MIGRATION_HMAC_HTTPS.md`
- [ ] Signature OTA Ed25519 : implémentation reportée à v13.85

**v13.70 — robustesse mémoire/réseau + tests Unity**
- [x] NVS : pattern v13.46 (`isKey` avant compare) généralisé à `saveInt` et `saveULong`
- [x] Heartbeat : compteur diagnostique `netHeartbeatDroppedCount()` (file postSender saturée)
- [x] `MIN_HEAP_FOR_SMTP` / `MIN_HEAP_FOR_HTTPS` aliases (différenciation future v13.80+)
- [x] Inventaire DRAM `config.h` actualisé (~60-65 KB BSS app vs ~50 KB annoncé)
- [x] `task_monitor` étendu : postSender + ota (display retiré, task supprimée v13.65+)
- [x] Tests Unity : `test_sensor_validation` (12 cas), `test_gpio_mapping` (9 cas)

**v13.65 — refactor architecture ciblé (gros découpages reportés à v13.66+)**
- [x] Mailer : footer mail utilise le cache capteurs (au lieu de `sensors.read()` bloquant 1-7s)
- [x] Automatism : `getCachedReadings()` ajouté (non bloquant) avec fallbacks `SensorConfig::Fallback::*`
- [ ] Découpage `app_tasks.cpp` (1709 l.) → reporté v13.66 (validation hardware requise)
- [ ] Extraction routes admin de `web_server.cpp` (1949 l.) → reporté v13.67
- [ ] `app_context.h` forward declarations + `extern` globals → reporté v13.68

**v13.60 — hygiène + sécurité moyenne + restauration beta-local**
- [x] 6 façades `config_*.h` + `board_traits.h` (constexpr) — préparation découpage `config.h`
- [x] CORS `*` retiré sur `/dbvars`, `/json`, `/wifi/*`, `/debug-logs` (UI same-origin)
- [x] OTA metadata HTTPS : `skip_cert_common_name_check = false` (audit)
- [x] AP secours S3 : WPA2 via `Secrets::AP_FALLBACK_PASSWORD` (legacy: ouvert)
- [x] `lib_deps` Async épinglées (`AsyncTCP@3.3.5`, `ESPAsyncWebServer@3.7.6`)
- [x] `platformio.ini` documenté (matrice des 13 environnements en commentaire)
- [x] Suite `wroom-beta-local` vérifiée présente (5 scripts + JSON scenarios + `.env.example`)
- [x] `platformio-native.ini` + `build_all_envs.ps1` vérifiés présents (audit corrigé)
- [x] `include/local_server_overrides.h.example` vérifié présent

**v13.53 — fonctionnel critique**
- [x] `wlAqua = 0` corrigé : fallback sur `_lastValidWlAqua` puis `Fallback::WATER_LEVEL_AQUA` (évite fausse alerte inondation)
- [x] `wlPota` symétrique avec `_lastValidWlPota` ajouté (anciennement forçait 0 sans fallback)
- [x] `GPIOMap` pompes/lumière référencent `Pins::POMPE_AQUA`/`POMPE_RESERV`/`LUMIERE` (cohérence WROOM↔S3)
- [x] Plages ultrason 4000/5000 mm documentées (capteur HC-SR04 vs validation niveau eau)
- [x] `mailer.cpp` : feed TWDT pendant `_smtp.connect` et `MailClient.sendMail` (anti-reboot SMTP > 30s)
- [x] `web_client.cpp` : mutex HTTP avec timeout (au lieu de `portMAX_DELAY`) — évite blocage GET par POST 18s
- [x] `SystemBoot::initWatchdog()` extrait de `app.cpp` (factorise les 3 blocs TWDT WROOM/S3/IDF4-5)

**v13.52 — sécurité critique**
- [x] Audit exhaustif consolidé : `docs/reports/AUDIT_GENERAL_2026-05.md`
- [x] Sécurité web : routes admin (`/api/wakeup feed`, `/api/remote-flags`, `/mailtest`, `/testota`, `/fs/format`) protégées par `webAuthIsAuthenticated`
- [x] Sécurité cookie session : `esp_fill_random` (HW RNG) + `HttpOnly; SameSite=Strict` + TTL 24 h + rotation à chaque login
- [x] Sécurité WebSocket port 81 : authentification obligatoire via token session (`{"type":"auth","token":...}`), déconnexion après 5 s sans auth
- [x] `/wifi/saved` : plus de mots de passe WiFi en clair, seulement `hasPassword` booléen
- [x] Bug correction `API_KEY = API_KEY` (auto-référence) dans `include/config.h`
- [x] `static_assert PROFILE_PROD` rejette le placeholder `CHANGEZ_MOI` pour `API_KEY` et `WEB_AUTH_PASS`

> Roadmap audit : v13.52 (sécurité critique) → v13.53 (fonctionnel critique) → v13.60 (hygiène + beta-local) → v13.65 (refactor ciblé) → v13.70 (robustesse + tests) → **v13.80 (HMAC + HTTPS dual)** → v13.90 (bascule).

### 📈 Métriques
- **Uptime**: 24/7 stable
- **Mémoire**: <80% utilisation
- **Latence WebSocket**: <100ms
- **Temps de boot**: ~15 secondes

---

## 🛠️ Développement

### 📋 Prérequis
- **PlatformIO** v6.0+
- **ESP32-WROOM-32** ou **ESP32-S3**
- **Arduino Framework**

### 🔨 Commandes utiles
```bash
# Build et upload
pio run -e wroom-test -t upload

# Monitoring série
pio device monitor -e wroom-test

# Nettoyage
pio run -e wroom-test -t clean

# Upload filesystem
pio run -e wroom-test -t uploadfs
```

### 📚 Documentation
- **[Documentation](docs/README.md)** — structure, compilation, principes
- **[Seuils ESP32 / serveur](docs/technical/SEUILS_SERVEUR_ESP32.md)** — différences volontaires
- **[Rapports](docs/reports/)** — conformité, NVS, corrections, origine problèmes

---

## 🐛 Résolution de problèmes

### 🔍 Diagnostics courants
1. **Problème WiFi** → Logs série 115200 baud, vérifier RSSI et reconnexion
2. **Erreurs mémoire** → [Rapport origine problèmes](docs/reports/monitoring/reports/ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md) (DHT22, heap, watchdog)
3. **OTA échoué** → Vérifier WiFi, quota firmware, `pio run -e wroom-test -t upload`
4. **Capteurs instables** → [Résumé corrections](docs/reports/corrections/RESUME_CORRECTIONS_APPLIQUEES.md) (DHT22, queue, timeouts)

### 📊 Monitoring en temps réel
- **Interface web**: `http://ffp3-XXXX.local`
- **Logs série**: 115200 baud
- **WebSocket**: Mise à jour temps réel

---

## 📈 Historique des versions

L'historique complet est tenu dans **[VERSION.md](VERSION.md)** (source unique).

Versions récentes :
- **v13.80** (2026-05) — Audit général : migration contrat firmware↔serveur (mode dual). HTTPS opt-in via `USE_HTTPS_ENDPOINTS`, HMAC-SHA256 en complément d'api_key, doc MIGRATION_HMAC_HTTPS.md.
- **v13.70** (2026-05) — Audit général : robustesse mémoire/réseau (NVS isKey, heartbeat counter, MIN_HEAP_SMTP/HTTPS aliases) + tests Unity (sensor_validation, gpio_mapping) + inventaire DRAM actualisé.
- **v13.65** (2026-05) — Audit général : refactor architecture ciblé (mailer cache, Automatism::getCachedReadings). Découpages app_tasks.cpp / web_server.cpp reportés à v13.66+.
- **v13.60** (2026-05) — Audit général : hygiène + sécurité moyenne (CORS, OTA CN, AP WPA2, lib_deps épinglées, façades config_*.h, board_traits.h) + restauration suite beta-local.
- **v13.53** (2026-05) — Audit général : fonctionnel critique (wlAqua/wlPota fallback, GPIOMap via Pins::*, SMTP feed TWDT, mutex HTTP timeout, initWatchdog factorisé).
- **v13.52** (2026-05) — Audit général : sécurité web critique (auth routes admin, WebSocket auth, cookie hardened, bug API_KEY corrigé, /wifi/saved sans mdp).
- **v13.51** (2026-05) — netRPC GET outputs/state : libération du slot après échec notifié.
- **v13.49** (2026-04) — Journaux série : phases HTTP/WiFi explicites, nettoyage logs DBG.
- **v13.46** (2026-04) — NVS `saveBool` correctif première écriture (snap_* veille).
- **v13.45** (2026-04) — sdkconfig WROOM (CPU/SPIRAM) + retrait LTO wroom-prod (panic cache).

[Voir toutes les versions](VERSION.md)

---

## 🤝 Contribution

### 📝 Standards de développement
- **Version**: Incrémenter à chaque modification
- **Monitoring**: 90s après chaque déploiement
- **Tests**: Validation sur hardware réel
- **Documentation**: Mise à jour obligatoire

### 🔄 Workflow
1. **Modifier le code**
2. **Incrémenter la version** (config.h)
3. **Tester sur ESP32**
4. **Monitoring 90s + analyse logs**
5. **Documenter les changements**
6. **Commit avec numéro de version**

---

## 📞 Support

### 📚 Documentation
- **[Documentation](docs/README.md)** — vue d'ensemble, structure, rapports
- **[Technique](docs/technical/)** — seuils ESP32 vs serveur
- **[Rapports](docs/reports/)** — conformité, NVS, corrections, monitoring

### 🔍 Debugging
- **Logs série**: 115200 baud
- **Interface web**: Diagnostic en temps réel
- **WebSocket**: Monitoring live

---

## 📄 Licence

MIT License - Voir [LICENSE](LICENSE) pour plus de détails.

---

## 🙏 Remerciements

- **ESP32 Community** pour le support technique
- **PlatformIO** pour l'environnement de développement
- **Arduino Framework** pour la simplicité d'utilisation

---

*Dernière mise à jour: 2026-05-20 - Version 13.52*
