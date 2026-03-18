# Publication OTA distant – FFP5CS

## Emplacement unique

Les binaires et **serveur/ota/metadata.json** sont publiés dans le dépôt **n3_serveur** (`serveur/ota/`), comme n3pp et msp. Le serveur sert les fichiers sous **https://iot.olution.info/ota/** (firmware : `OTA_BASE_PATH = "/ota/"`).

## Commande

Depuis la **racine IOT_n3** :

```powershell
.\scripts\publish_ota.ps1 -Targets ffp5-wroom-prod,ffp5-wroom-beta,ffp5-s3-prod,ffp5-s3-test -Build
```

Depuis **firmwires/ffp5cs** (délégation) :

```powershell
.\scripts\publish_ota.ps1 -Build
```

Options utiles : `-SkipCommit`, `-DryRun`, `-SkipValidate`.

## Cibles PlatformIO

| Cible script        | Env PlatformIO   | Dossier OTA              | Canal metadata |
|---------------------|------------------|--------------------------|----------------|
| ffp5-wroom-prod     | wroom-prod       | esp32-wroom/             | prod           |
| ffp5-wroom-beta     | wroom-beta       | esp32-wroom-beta/        | test           |
| ffp5-s3-prod        | wroom-s3-prod    | esp32-s3/                | prod           |
| ffp5-s3-test        | wroom-s3-test    | esp32-s3-test/           | test           |

## Metadata

- Fichier : **serveur/ota/metadata.json** (JSON compact, sans clés `channels.*.default`, taille visée &lt; 2048 octets pour firmware &lt; 12.25).
- Champs par canal : `version`, `bin_url`, `size`, `md5` ; si LittleFS : `filesystem_url`, `filesystem_size`, `filesystem_md5`.
- **WROOM** (prod et beta) : pas de partition SPIFFS — pas de `buildfs` ni champs filesystem pour ces cibles.

## Vérification

```bash
curl -I https://iot.olution.info/ota/esp32-wroom/firmware.bin
curl -I https://iot.olution.info/ota/metadata.json
```

## Route legacy

`/ffp3/ota/*` reste un alias serveur (même répertoire) pour d’anciens firmwares.
