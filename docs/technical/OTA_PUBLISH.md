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
- Version firmware définie dans `include/config.h` (`ProjectConfig::VERSION`).

## Commande

Depuis la racine du projet (ffp5cs) :

```powershell
.\scripts\publish_ota.ps1
```

### Options

| Paramètre     | Description |
|---------------|-------------|
| `-Envs`       | Liste d'envs à publier (défaut : `wroom-prod`, `wroom-beta`). Ex. : `-Envs "wroom-prod","wroom-beta"` |
| `-SkipCommit` | Ne pas faire de commit ni de push (copie + metadata uniquement). |
| `-DryRun`     | Aucun commit/push ; affiche le statut git de `ffp3/ota/`. |
| `-Build`      | Lancer `pio run -e <env>` pour chaque env avant copie si firmware absent. |
| `-BuildFs`    | Lancer `pio run -e <env> -t buildfs` pour chaque env avant copie si littlefs absent. |
| `-IncludeFs`  | Inclure le filesystem LittleFS (défaut : true). Mettre `$false` pour publier uniquement le firmware. |
| `-Validate`   | Vérifier que les tailles firmware/filesystem respectent les partitions avant publication. |
| `-OtaBaseUrl` | URL de base pour les bin_url et filesystem_url (défaut : `https://iot.olution.info/ffp3/ota`). |

Exemples :

```powershell
.\scripts\publish_ota.ps1 -DryRun
.\scripts\publish_ota.ps1 -Build
.\scripts\publish_ota.ps1 -Build -BuildFs
.\scripts\publish_ota.ps1 -Envs "wroom-prod" -IncludeFs:$false -SkipCommit
```

## Mapping env → dossier OTA

| Env PlatformIO | Sous-dossier ffp3/ota/ | Canal metadata |
|----------------|------------------------|----------------|
| `wroom-prod`   | `esp32-wroom/`         | prod           |
| `wroom-beta`   | `esp32-wroom-beta/`    | test           |

Les binaires sont attendus dans `.pio/build/<env>/firmware.bin` et `.pio/build/<env>/littlefs.bin` (filesystem).

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

Après déploiement du serveur (ffp3), les ESP32 récupèrent la nouvelle version via l'URL metadata puis téléchargent le firmware (et éventuellement le filesystem) correspondant à leur modèle et canal.

## wroom-beta : OTA manuel uniquement

Pour wroom-beta (USE_TEST_ENDPOINTS) et wroom-test (PROFILE_TEST), l’OTA au boot est désactivé pour éviter que le GET metadata HTTPS ne bloque netTask au-delà du TWDT (30 s). L’OTA reste possible manuellement via :

- Interface web locale : page `/update` → bouton « Vérifier et mettre à jour » (POST `/api/ota`)
- Serveur distant : page contrôle → bouton « Vérifier OTA » → prochain GET state renvoie `triggerOtaCheck: true` à l’ESP32

## Délai de propagation trigger OTA (serveur distant)

Quand on clique « Vérifier OTA » sur la page contrôle, l'ESP32 reçoit `triggerOtaCheck: true` **au prochain GET state**. En idle, le délai peut atteindre la période de polling (typ. jusqu'à 1 min).

## Timeout et dérogation

Règle projet : timeouts réseau ≤ 5 s. Dérogation OTA : GET metadata 20 s, exécution dans netTask avec feed watchdog, sous TWDT (30 s). Voir `include/ota_config.h`.

## Ordre firmware puis filesystem

Firmware appliqué d'abord, puis filesystem. Si le filesystem échoue après succès firmware, reboot avec nouveau firmware + ancien filesystem. Re-flash manuel via `uploadfs` possible.

## Rappel dépôt parent

Après un push dans ffp3, le dépôt parent (ffp5cs) pointe vers une nouvelle ref du sous-module. Pour versionner cette ref :

```powershell
git add ffp3
git commit -m "chore: update ffp3 ref (OTA <version>)"
```
