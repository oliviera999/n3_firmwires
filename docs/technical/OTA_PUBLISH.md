# Publication OTA distant – FFP5CS

Procédure pour publier les firmware et filesystem compilés vers `ffp3/ota/` afin de les rendre disponibles pour la mise à jour OTA des ESP32 depuis le serveur (iot.olution.info).

## Objectif

- Recopier les `firmware.bin` et `littlefs.bin` compilés (.pio/build/<env>/) vers les sous-dossiers correspondants dans `ffp3/ota/` (esp32-wroom, esp32-wroom-beta).
- Mettre à jour `ffp3/ota/metadata.json` (versions, size, md5, bin_url, filesystem_url, filesystem_size, filesystem_md5).
- Committer et pousser les changements dans le dépôt **ffp3** (sous-module) pour que le déploiement serveur diffuse les binaires.

## Prérequis

- Build déjà effectué pour les environnements ciblés :
  - `pio run -e wroom-prod` (firmware)
  - `pio run -e wroom-prod -t buildfs` (filesystem LittleFS)
- Ou utiliser les options `-Build` et `-BuildFs` du script.
- Sous-module **ffp3** initialisé (répertoire `ffp3/` présent avec `.git`).
- Version firmware définie dans `include/config.h` (`ProjectConfig::VERSION`). Chaque build écrit aussi la version dans `.pio/build/<env>/version.txt` (script post-build `pio_write_build_version.py`) : le script de publication utilise cette version par env lorsqu’elle existe, sinon celle de `config.h`.

## Commande

Depuis la racine du projet (ffp5cs) :

```powershell
.\scripts\publish_ota.ps1
```

### Options

| Paramètre     | Description |
|---------------|-------------|
| `-Envs`       | Liste d'envs à publier (défaut : `wroom-prod`, `wroom-beta`, `wroom-s3-prod`, `wroom-s3-test`). Ex. : `-Envs "wroom-prod","wroom-s3-test"` |
| `-SkipCommit` | Ne pas faire de commit ni de push (copie + metadata uniquement). |
| `-DryRun`     | Aucun commit/push ; affiche le statut git de `ffp3/ota/`. |
| `-Build`      | Lancer `pio run -e <env>` pour chaque env avant copie si firmware absent. |
| `-BuildFs`    | Lancer `pio run -e <env> -t buildfs` pour chaque env avant copie si littlefs absent. |
| `-IncludeFs`    | Inclure le filesystem LittleFS (défaut : true). Mettre `$false` pour publier uniquement le firmware. |
| `-SkipValidate` | Désactiver la validation des tailles (firmware/fs <= tailles partition). Par défaut la validation est exécutée. |
| `-OtaBaseUrl`   | URL de base pour les bin_url et filesystem_url (défaut : `https://iot.olution.info/ffp3/ota`). |

Exemples :

```powershell
.\scripts\publish_ota.ps1 -DryRun
.\scripts\publish_ota.ps1 -Build
.\scripts\publish_ota.ps1 -Build -BuildFs
.\scripts\publish_ota.ps1 -Envs "wroom-prod" -IncludeFs:$false -SkipCommit
```

## Mapping env → dossier OTA

| Env PlatformIO   | Sous-dossier ffp3/ota/ | Canal metadata | Partitions (app / fs)      |
|------------------|------------------------|----------------|----------------------------|
| `wroom-prod`     | `esp32-wroom/`         | prod           | WROOM 0x1A0000 / 0x0B0000  |
| `wroom-beta`     | `esp32-wroom-beta/`    | test           | WROOM 0x1A0000 / 0x0B0000  |
| `wroom-s3-prod`  | `esp32-s3/`            | prod           | S3 0x6F8000 / 0x200000     |
| `wroom-s3-test`  | `esp32-s3-test/`       | test           | S3 0x6F8000 / 0x200000     |

Par défaut le script publie les quatre envs (`wroom-prod`, `wroom-beta`, `wroom-s3-prod`, `wroom-s3-test`). Les binaires sont attendus dans `.pio/build/<env>/firmware.bin` et `.pio/build/<env>/littlefs.bin` (filesystem). Le numéro de version publié pour chaque env est celui du firmware concerné : lu depuis `.pio/build/<env>/version.txt` (écrit à la compilation par `pio_write_build_version.py`), sinon depuis `include/config.h`.

## Effet

1. Création des dossiers `ffp3/ota/esp32-wroom/`, `ffp3/ota/esp32-wroom-beta/` si besoin.
2. Copie de chaque `firmware.bin` vers `ffp3/ota/<subfolder>/firmware.bin`.
3. Si `littlefs.bin` existe et `-IncludeFs` est actif : copie vers `ffp3/ota/<subfolder>/littlefs.bin`.
4. Mise à jour de `ffp3/ota/metadata.json` :
   - `channels.prod.esp32-wroom` et `channels.prod.default` pour prod ;
   - `channels.test.esp32-wroom` et `channels.test.default` pour test (wroom-beta) ;
   - Champs `filesystem_url`, `filesystem_size`, `filesystem_md5` ajoutés si filesystem publié ;
   - Champs racine legacy (version, bin_url, size, md5) = default prod.
5. Si pas `-SkipCommit` ni `-DryRun` : `git add ota/`, `git commit`, `git push` dans le dépôt **ffp3**.

Après déploiement du serveur (ffp3), les ESP32 récupèrent la nouvelle version via l'URL metadata puis téléchargent le firmware (et éventuellement le filesystem) correspondant à leur modèle et canal. Les URLs servies (firmware.bin, littlefs.bin) doivent renvoyer les mêmes octets que ceux pour lesquels `size` et `md5` ont été calculés dans metadata.json (éviter cache ou déploiement partiel incohérent).

## OTA selon l’environnement

- **wroom-prod / wroom-beta** : OTA au boot (si serveur joignable) et périodique (toutes les 2 h), plus OTA manuel et trigger distant.
- **wroom-s3-test** (PROFILE_TEST + BOARD_S3) : OTA au boot et périodique **activés** (comme prod). OTA manuel et trigger distant également disponibles.
- **wroom-test** (PROFILE_TEST + BOARD_WROOM) uniquement : OTA au boot et périodique **désactivés** (GET metadata HTTPS peut bloquer netTask au-delà du TWDT 30 s). L’OTA reste possible manuellement via :

- Interface web locale : page `/update` → bouton « Vérifier et mettre à jour » (POST `/api/ota`)
- Serveur distant : page contrôle → bouton « Vérifier OTA » → prochain GET state renvoie `triggerOtaCheck: true` à l’ESP32

## Délai de propagation trigger OTA (serveur distant)

Quand on clique « Vérifier OTA » sur la page contrôle, l'ESP32 reçoit `triggerOtaCheck: true` **au prochain GET state**. En idle, le délai peut atteindre la période de polling (typ. jusqu'à 1 min).

## Timeout et dérogation

Règle projet : timeouts réseau ≤ 5 s. Dérogation OTA : GET metadata 20 s, exécution dans netTask avec feed watchdog, sous TWDT (30 s). Voir `include/ota_config.h`.

## Ordre firmware puis filesystem

Firmware appliqué d'abord, puis filesystem. Si le filesystem échoue après succès firmware, reboot avec nouveau firmware + ancien filesystem. Re-flash manuel via `uploadfs` possible.

## Intégrité filesystem OTA

La vérification d’intégrité du filesystem OTA côté appareil repose sur :

- **Taille** : le firmware compare la taille téléchargée à la taille attendue (metadata `filesystem_size`) et à la taille de la partition. Un écart fait échouer l’OTA (sauf si `OTA_UNSAFE_FORCE` est actif).
- **HTTPS** : le téléchargement se fait en HTTPS ; l’intégrité et la confidentialité du flux reposent sur TLS.

Le champ **filesystem_md5** est calculé par le script et écrit dans `metadata.json`, mais le firmware **ne vérifie pas** ce MD5 (implémentation non réalisée côté device, relecture complète de la partition nécessaire). En résumé : intégrité filesystem = taille + HTTPS, pas de vérification MD5 côté appareil.

## Rôle du MD5 et intégrité firmware

- **Firmware (binaire app)** : quand les métadonnées fournissent un champ `md5`, le firmware le transmet à l’API `Update.setMD5()` pour vérifier le flux reçu. Le MD5 sert à **détecter corruption et troncature** du binaire ; il ne remplace pas la confidentialité, assurée par **HTTPS** (BASE_URL_SECURE).
- **Metadata** : le champ `md5` dans `metadata.json` est la référence d’intégrité côté client pour le firmware. Le script calcule ce hash (MD5) après copie des binaires ; il est aligné avec l’API Arduino Update qui n’accepte que MD5 pour la vérification du flux.
- **Évolution optionnelle** : si l’écosystème permet à l’avenir une vérification SHA256 du flux OTA, on pourra ajouter un champ `sha256` dans `metadata.json` et une vérification côté firmware ; en attendant, MD5 reste la référence pour l’intégrité du binaire app.

## Vérification Content-Length (serveur)

Le firmware (avec `OTA_UNSAFE_FORCE = false`) exige que les réponses HTTP pour les binaires OTA envoient un en-tête **Content-Length** correct. Sans cela, l’OTA échoue.

À vérifier côté hébergeur de `https://iot.olution.info/ffp3/ota/` :

- Les réponses pour `firmware.bin` et `littlefs.bin` doivent envoyer **Content-Length** (taille en octets du fichier).
- En cas de reverse proxy (nginx, Apache, CDN), ne pas supprimer Content-Length ni forcer le transfert chunked sans longueur.
- Servir les fichiers en mode body de taille connue (fichiers statiques).

Vérification rapide après déploiement :

```bash
curl -I https://iot.olution.info/ffp3/ota/esp32-wroom/firmware.bin
curl -I https://iot.olution.info/ffp3/ota/esp32-wroom/littlefs.bin
```

Confirmer la présence de `Content-Length: <taille>` cohérente avec les fichiers. Si Content-Length est absent ou 0, l’OTA distant échouera (ou il faudrait temporairement réactiver `OTA_UNSAFE_FORCE` en exception, non recommandé en production).

## Rappel dépôt parent

Après un push dans ffp3, le dépôt parent (ffp5cs) pointe vers une nouvelle ref du sous-module. Pour versionner cette ref :

```powershell
git add ffp3
git commit -m "chore: update ffp3 ref (OTA <version>)"
```
