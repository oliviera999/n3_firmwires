# wroom-beta-local — build, flash et campagne de tests

**Env** : `wroom-beta-local` (hérite de `wroom-beta` → `wroom-prod`, plateforme **pioarduino**).  
**But** : valider le firmware contre le **serveur PHP Docker local** (`USE_LOCAL_SERVER_ENDPOINTS`), avec logs série et endpoints `*-test`.  
**Voir aussi** : [COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md](COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md) (pioarduino 2 passes, flash cohérent), [serveur/README.md](../../../../serveur/README.md) (stack Docker).

---

## 1. Prérequis

| Élément | Fichier / commande |
|---------|-------------------|
| Secrets firmware | `include/secrets.h` (copier `include/secrets.h.example`) |
| URL serveur LAN | `include/local_server_overrides.h` (copier `.example`) — ex. `http://192.168.0.158:8082` |
| Stack serveur | `cd serveur` → `.\tools\local-docker.ps1 up` puis `smoke` / `test` |
| WiFi | Réseaux dans `secrets.h` ; l’ESP32 et la machine Docker doivent être sur le **même LAN** (ou routage explicite vers l’IP du override) |
| Port série | Ex. **COM5** (CP210x) — adapter selon la machine |

**Vérification serveur** (depuis le PC qui héberge Docker) :

```powershell
Invoke-WebRequest -Uri "http://192.168.0.158:8082/ping" -UseBasicParsing
```

Si le firmware affiche `[HTTP] GET ... code=-1` au boot, le serveur est injoignable depuis le réseau WiFi de la carte (Docker arrêté, mauvaise IP, pare-feu).

---

## 2. Compilation

### 2.1 Commande

```powershell
cd firmwires\ffp5cs
$env:PYTHONUTF8 = "1"
pio run -e wroom-beta-local
```

Durée typique : **~9–15 min** (phase 1 IDF + phase 2 application) au premier build ; **~2–3 min** si le cache pioarduino est chaud.

### 2.2 Warmup si `FRAMEWORK_DIR None` ou échec phase 1

Avant le premier `wroom-beta-local` de la session (ou après nettoyage cache) :

```powershell
cd firmwires\n3pp
pio run -e esp32dev
cd ..\ffp5cs
pio run -e wroom-beta-local
```

Alternative documentée : `pio run -e wroom-prod` une fois avec succès, puis `wroom-beta-local`.

### 2.3 Valider le binaire

Utiliser le répertoire du **dernier build réussi** (souvent **`.pio\build\wroom-beta-local\`**, pas un ancien miroir sous `C:\pio-builds\` si les tailles divergent) :

```powershell
$bd = ".pio\build\wroom-beta-local"
(Get-Item "$bd\firmware.bin").Length    # attendu : ~1,55–1,65 Mo (beta + web async)
Test-Path "$bd\src\app.cpp.o"           # True
Get-Content "$bd\version.txt"           # ex. 13.84
```

Contrôles rapides :

- `firmware.bin` **&lt; 1,0 Mo** → stub phase 1 uniquement, **ne pas flasher**.
- Chaîne **`FFP5CS`** présente dans le binaire (optionnel).

---

## 3. Flash sur COMx (Windows)

### 3.1 Libérer le port

```powershell
cd firmwires\ffp5cs
. .\scripts\Release-ComPort.ps1
Release-ComPortIfNeeded -Port COM5
```

Fermer le moniteur Cursor/VSCode sur le même COM avant flash.

### 3.2 Upload PlatformIO

```powershell
pio run -e wroom-beta-local -t upload --upload-port COM5
pio run -e wroom-beta-local -t uploadfs --upload-port COM5
```

**Partition LittleFS** : `wroom-beta-local` utilise `partitions_esp32_wroom_ota_fs_medium.csv` (704 Ko) et réactive le serveur web (`-UDISABLE_ASYNC_WEBSERVER`). Sans cela, `uploadfs` échoue avec `LFS_ERR_NOSPC` : le dossier `data/` (~220 Ko) ne tient pas dans la partition mail de 64 Ko héritée de `wroom-prod`.

### 3.3 CP210x / pas d’auto-bootloader (erreur `Wrong boot mode 0x13`)

Sur certaines cartes (pont **CP210x** sans câblage DTR→GPIO0), `pio upload` échoue alors que la carte tourne déjà (`boot:0x13`).

**Procédure manuelle** puis esptool :

1. Maintenir **BOOT** (GPIO0).
2. Appuyer **RST** (EN), relâcher RST, relâcher BOOT quand `Connecting...` apparaît.
2. Flash jeu **cohérent** (même dossier de build) :

```powershell
$bd = ".pio\build\wroom-beta-local"
$py = "$env:USERPROFILE\.platformio\penv\Scripts\python.exe"
$esptool = "$env:USERPROFILE\.platformio\packages\tool-esptoolpy\esptool.py"
& $py $esptool --chip esp32 --port COM5 --baud 115200 --before no_reset --after hard_reset `
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB `
  0x1000 "$bd\bootloader.bin" 0x8000 "$bd\partitions.bin" 0x10000 "$bd\firmware.bin"
```

En cas d’échec : relancer en boucle (fenêtre ~60 s) ou réessayer après **rebranchement USB** si `COM5` a disparu (`Lost connection` vers 60–70 % d’écriture).

### 3.4 Boot attendu (série 115200)

Logs typiques :

- `=== BOOT FFP5CS v13.84 ===`
- `[HTTP] GET outputs/state: ... URL=http://<IP_LAN>:8082/ffp3/api/outputs-test/state`
- WiFi OK → IP locale de la carte (ex. `192.168.31.x`)
- HTTP **code=200** si Docker joignable ; **code=-1** si serveur down / mauvaise IP

Sans capteurs branchés : timeouts ultrason / I2C normaux sur banc d’essai.

---

## 4. Campagne de tests automatisés

### 4.1 Script tout-en-un (build + upload + monitor)

```powershell
.\build_upload_monitor_wroom_beta_local.ps1 -Port COM5
```

(Équivalent historique : `build_upload_monitor_wroom_beta_local_com4.ps1` avec `-Port COM4`.)

### 4.2 Test bidirectionnel panneau de contrôle ↔ ESP32

Vérifie explicitement les **deux sens** (commandes web → BDD → poll ESP → actionneurs ; mesures ESP → POST → BDD) et la **persistance** des variables (`ffp3Outputs2`, fenêtre priorité web 10 s — voir `serveur/docs/SYNCHRONISATION_BIDIRECTIONNELLE.md`).

```powershell
# Prérequis : Docker up + ping OK sur l'IP de local_server_overrides.h
cd serveur
.\tools\local-docker.ps1 up
curl.exe -s -m 5 http://192.168.0.158:8082/ping

cd ..\firmwires\ffp5cs
.\scripts\test_bidirectional_control_panel_local.ps1 -Port COM5 -SerialSeconds 120
```

Le script produit un rapport dans `logs/bidirectional_control_*.md` :
- GET `/ffp3/api/outputs-test/state` (comme l’ESP32)
- POST `/ffp3/api/outputs-test/toggle` (comme le panneau `/aquaponie-control-test`)
- re-GET avec `?fresh=1` pour vérifier la persistance
- capture série : `fetchRemoteState`, `code=200`, `post-data-test`

**Alignement clés** : `API_KEY` dans `include/secrets.h` = `local_api_key_change_me` (comme `serveur/.env.docker.example` et `scripts/.beta-local-test.env`).

### 4.3 Suites dédiées

```powershell
# Série seule (assertions logs)
.\scripts\test_wroom_beta_local_serial.ps1 -Port COM5 -MonitorSeconds 150

# Docker + appareil
.\scripts\test_wroom_beta_local_docker_integration.ps1 -Port COM5 -AuthMode both

# Batterie quick ou full (token / session)
.\scripts\run_wroom_beta_local_test_suite.ps1 -Port COM5 -Campaign quick -Auth both
```

Secrets optionnels pour la batterie : `scripts/.beta-local-test.env` (copier depuis `.beta-local-test.env.example`).

### 4.3 Ordre recommandé

1. `local-docker.ps1 up` + smoke sur la machine dont l’IP est dans `local_server_overrides.h`.
2. Build `wroom-beta-local` (§2).
3. Flash COMx (§3).
4. `run_wroom_beta_local_test_suite.ps1 -Campaign quick`.
5. Si OK → `-Campaign full` ou intégration Docker.

---

## 5. Dépannage rapide

| Symptôme | Action |
|----------|--------|
| `Missing Arduino framework directory 'None'` | Warmup `n3pp` (§2.2) |
| `Arduino.h: No such file` en phase 2 | Rebuild sans interruption ; voir [COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md](COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md) §3 |
| `Wrong boot mode 0x13` | §3.3 BOOT+RST ou esptool `--before no_reset` |
| `COMx` introuvable après flash | Rebrancher USB, attendre réapparition du port |
| HTTP `-1` partout | Docker / IP / pare-feu ; `curl http://<IP>:8082/ping` depuis le PC ; si `docker` CLI bloque, redémarrer Docker Desktop puis `local-docker.ps1 up` |
| `configSynced=0` dans les logs | Normal tant que GET `outputs-test/state` échoue — pas de config serveur appliquée |
| Toggle panneau sans effet ESP | Vérifier même table **test** (`outputs-test` ↔ `aquaponie-control-test`) ; attendre un poll ESP (~6 s) |
| `C:\pio-builds\...\firmware.bin` plus gros que `.pio\build\...` | Préférer **`.pio\build\wroom-beta-local`** après le dernier `pio run` réussi |
| Guru Meditation **Cache error** au boot | Flash **jeu homogène** (bootloader + partitions + firmware du **même** build) — [COMPILATION](COMPILATION_WROOM_PIOARDUINO_ET_ENVS.md) §7 |

---

*Dernière mise à jour : 2026-06 — validation terrain COM5 (CP210x), build v13.84, override `192.168.0.158:8082`.*
