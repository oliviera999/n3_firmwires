# Compilation + déploiement OTA depuis GitHub

Ce dépôt compile **déjà** tous les firmwares en CI (`.github/workflows/firmware-ci.yml`,
*compile-check* sur push `master`/PR). Le présent document décrit le **déploiement OTA
automatisé** ajouté par-dessus : compiler avec les vrais secrets, **signer** le binaire et
le **publier** vers le serveur OTA, sans passer par la machine Windows et `publish_ota.ps1`.

## Vue d'ensemble

```
workflow_dispatch  OU  tag git "ota-deploy/<fw>/<canal>"
  └─▶ resolve ─▶ build : PlatformIO (secrets de prod) ─▶ artefact firmware.bin + version
        │
  job deploy (gate prod = GitHub Environment "prod")
        ├─ garde-fou version (refuse une version ≤ celle en ligne)
        ├─ intégrité  n3ota: sha256+ECDSA  |  ffp5: md5  ─▶ commit/push n3_serveur (serveur/ota/…)
        ├─ GitHub Release de traçabilité (bin attaché — réutilisé par le rollback)
        └─ vérification post-déploiement (re-télécharge l'URL publique, re-vérifie l'intégrité)
                                     │
                 iot.olution.info/ota/… ─▶ check OTA sur l'appareil

Rollback : Firmware OTA Rollback ─▶ récupère le bin de la Release vX ─▶ republie (--no-guard)
```

**Durcissements opérationnels** (par rapport à un simple build+push) :
- **Build-once** : le binaire testé est passé en artefact au job de déploiement
  (pas de recompilation ⇒ on signe/publie exactement ce qui a été produit), et
  conservé 90 j + attaché à une **GitHub Release** par version.
- **Garde-fou version** : `publish_ota.py --guard` lit le `metadata.json` en
  ligne et **échoue** si la version à publier n'est pas strictement supérieure
  (sinon les appareils rejettent silencieusement la MAJ). `--no-guard` pour forcer.
- **Approbation prod** : le job `deploy` n'utilise l'environnement `prod` que
  pour un déploiement **prod réel** (ni dry-run, ni test). Configurer un
  *Required reviewer* sur cet environnement ⇒ pause jusqu'à approbation manuelle.
- **Vérif post-déploiement** : `verify_published.py` rejoue le parcours d'un
  appareil (télécharge le bin publié, compare le sha256, vérifie la signature
  avec la clé extraite de `n3_ota_pubkey.h`). Plusieurs tentatives pour absorber
  un délai de déploiement serveur ; désactivable via l'input `verify_published`.

**Fichiers** :
- `.github/workflows/firmware-ota-deploy.yml` — déploiement (manuel ou tag).
- `.github/workflows/firmware-ota-rollback.yml` — retour à une version antérieure.
- `tools/ota/publish_ota.py` — garde-fou + intégrité + écriture du metadata (schémas n3ota/ffp5).
- `tools/ota/verify_published.py` — vérification bout-en-bout depuis l'URL publique.

## Cibles supportées

Deux **schémas** de metadata coexistent :
- **`n3ota`** (n3pp/msp/cam) : intégrité **sha256 + signature ECDSA**, HTTP, vérifié par
  `shared/n3_common/n3_ota`.
- **`ffp5`** (ffp5cs WROOM) : intégrité **md5**, structure `channels[prod|test][modèle]`,
  HTTPS, vérifié par `ffp5cs/ota_manager` (`OtaArtifactSelect`). **Pas de signature ECDSA.**

| Choix workflow | Schéma | Dossier | Env PlatformIO | Sortie OTA | metadata |
|----------------|--------|---------|----------------|------------|----------|
| `n3pp` (prod/test) | n3ota | `n3pp` | `esp32dev` / `esp32dev_test` | `ota/n3pp[-test]/` | objet unique |
| `msp` (prod/test) | n3ota | `msp` | `esp32dev` / `esp32dev_test` | `ota/msp[-test]/` | objet unique |
| `cam-msp1` | n3ota | `uploadphotosserver` | `msp1` | `ota/cam/msp1/` | clé `msp1` (fusion) |
| `cam-n3pp` | n3ota | `uploadphotosserver` | `n3pp` | `ota/cam/n3pp/` | clé `n3pp` (fusion) |
| `cam-ffp3` | n3ota | `uploadphotosserver` | `ffp3` | `ota/cam/ffp3/` | clé `ffp3` (fusion) |
| `ffp5-wroom` (prod/test) | ffp5 | `ffp5cs` | `wroom-prod` / `wroom-test` | `ota/esp32-wroom[-beta]/` | `channels[prod\|test][esp32-wroom]` (fusion) |

> **ffp5cs — périmètre.** Seul **WROOM (firmware seul)** est géré (conforme à
> `ffp5cs/docs/technical/OTA_PUBLISH.md` : pas d'image filesystem pour WROOM).
> Les cibles **S3** et les **images LittleFS** sont un suivi séparé (S3 est aussi
> hors CI pour une incompat. toolchain ESP32Servo). `ffp5cs/scripts/publish_ota.ps1`
> reste disponible pour le flux Windows complet (S3 + filesystem).

## Format d'intégrité (rappel)

- **n3ota** (`n3_ota.cpp`) : `sha256` = hex des octets du `.bin` ; `signature` =
  **base64 d'une signature ECDSA DER sur le digest sha256** = `openssl dgst -sha256
  -sign cle.pem firmware.bin`. Clé publique : `shared/n3_common/src/n3_ota_pubkey.h`.
- **ffp5** (`OtaArtifactSelect`) : `md5` (hex) + `size` (octets) du `.bin`. Aucune signature.

## Configuration requise (une seule fois)

Dans **Settings ▸ Secrets and variables ▸ Actions** du dépôt :

**Secrets :**
| Nom | Schéma | Contenu |
|-----|--------|---------|
| `OTA_SIGNING_KEY` | n3ota | Clé **privée** ECDSA (PEM) — celle dont la publique est dans `n3_ota_pubkey.h`. |
| `CREDENTIALS_H` | n3ota | `credentials.h` de prod complet (WiFi `WIFI_LIST`, SMTP, `API_KEY`). |
| `FFP5CS_SECRETS_H` | ffp5 | `ffp5cs/include/secrets.h` de prod (WiFi). |
| `FFP5CS_SECRETS_CONFIG_H` | ffp5 | `ffp5cs/include/secrets_config.h` de prod (`API_KEY`, destinataire). |
| `N3_SERVEUR_DEPLOY_TOKEN` | tous | PAT avec **Contents: write** sur `n3_serveur`. |

> Sans les vrais secrets de prod, le binaire OTA serait hors-ligne (pas de WiFi).

**Variables (facultatives, défauts entre parenthèses) :**
| Nom | Défaut | Rôle |
|-----|--------|------|
| `N3_SERVEUR_REPO` | `oliviera999/n3_serveur` | Dépôt servant `iot.olution.info`. |
| `N3_SERVEUR_OTA_ROOT` | `serveur/ota` | Racine OTA dans ce dépôt. |
| `OTA_BASE_URL` | `http://iot.olution.info/ota` | Préfixe public n3ota (garde-fou + vérif). |
| `OTA_BASE_URL_HTTPS` | `https://iot.olution.info/ota` | Préfixe public ffp5 (HTTPS). |

**Environnement (pour l'approbation prod) :** créer un environnement **`prod`**
(*Settings ▸ Environments*) avec un **Required reviewer**. Le job `deploy` s'y
rattache uniquement pour un déploiement prod réel et se met alors en pause
jusqu'à approbation. (Les déploiements `test` et les dry-run ne sont pas gatés.)

> ⚠️ Vérifier `N3_SERVEUR_OTA_ROOT` : le chemin réel dépend de l'arborescence de `n3_serveur`
> (le firmware sert `/ota/…`, mais l'emplacement des fichiers dans le repo peut différer).

## Utilisation

### A. Déclenchement manuel
1. **Actions ▸ Firmware OTA Deploy ▸ Run workflow**.
2. Choisir **firmware** + **canal** (`test` recommandé pour valider), laisser **dry_run = true**
   d'abord : compile + garde-fou + intégrité, **ne pousse rien** (affiche le metadata).
3. Relancer avec **dry_run = false** pour publier réellement dans `n3_serveur`.
4. Les appareils prennent la MAJ au prochain check OTA (comparaison de version).

### B. Déclenchement par tag git
```bash
git tag ota-deploy/n3pp/test        # ou ota-deploy/ffp5-wroom/prod, ota-deploy/msp/prod …
git push origin ota-deploy/n3pp/test
```
Un tag `ota-deploy/<firmware>/<canal>` lance un **déploiement réel** (dry_run=false) + vérification.
La version provient toujours de la source (manifest) — pensez à la bumper avant. La Release de
traçabilité créée (`ota-<fw>-<canal>-v<version>`) a un préfixe distinct et **ne re-déclenche pas**.

### C. Rollback (revenir à une version antérieure)
**Actions ▸ Firmware OTA Rollback ▸ Run workflow** → firmware + canal + **version cible**.
Le workflow récupère le binaire signé de la **Release** `ota-<fw>-<canal>-v<version>` (donc la
version doit avoir été déployée une fois) et le **republie sans recompiler** (`--no-guard`, car
on redescend en version). `dry_run = true` par défaut.

## Bump de version

`publish_ota.py` lit la version dans la source déclarée par `firmwares.manifest.json`
(`versionSource`). Pensez à **incrémenter la version** du firmware avant de déployer
(skill `bump-firmware-version`) : un appareil n'accepte la MAJ que si la version distante
est **strictement supérieure** (`compareVersions` dans `n3_ota.cpp`).

## Test local du script

```bash
openssl ecparam -name prime256v1 -genkey -noout -out /tmp/key.pem
python tools/ota/publish_ota.py --firmware n3pp --channel test \
  --bin n3pp/.pio/build/esp32dev_test/firmware.bin \
  --key /tmp/key.pem --ota-root /tmp/ota --dry-run
```

## Évolutions possibles

- **ffp5cs S3 + image LittleFS** : étendre le schéma `ffp5` aux modèles `esp32-s3`
  et aux champs `filesystem_url/size/md5` (build `-t buildfs`). À valider sur appareil.
- **Validation sur appareil de test** avant tout déploiement prod d'un nouveau schéma.
