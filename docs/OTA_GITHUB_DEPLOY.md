# Compilation + déploiement OTA depuis GitHub

Ce dépôt compile **déjà** tous les firmwares en CI (`.github/workflows/firmware-ci.yml`,
*compile-check* sur push `master`/PR). Le présent document décrit le **déploiement OTA
automatisé** ajouté par-dessus : compiler avec les vrais secrets, **signer** le binaire et
le **publier** vers le serveur OTA, sans passer par la machine Windows et `publish_ota.ps1`.

## Vue d'ensemble

```
workflow_dispatch ─▶ build PlatformIO (secrets de prod) ─▶ signature sha256+ECDSA
                                                              │
                          firmware.bin + metadata.json ◀──────┘
                                     │
                       commit/push ──▶ n3_serveur (serveur/ota/…)
                                     │
                       https://iot.olution.info/ota/… ─▶ n3OtaCheck() sur l'appareil
```

Le workflow : `.github/workflows/firmware-ota-deploy.yml`.
La signature/écriture du metadata : `tools/ota/publish_ota.py` (portable, remplace
le `.ps1` Windows dans le contexte CI ; **même format** de metadata, vérifié par
`shared/n3_common/n3_ota`).

## Cibles supportées

| Choix workflow | Dossier | Env PlatformIO | Sortie OTA | metadata |
|----------------|---------|----------------|------------|----------|
| `n3pp` (prod/test) | `n3pp` | `esp32dev` / `esp32dev_test` | `ota/n3pp[-test]/` | objet unique |
| `msp` (prod/test) | `msp` | `esp32dev` / `esp32dev_test` | `ota/msp[-test]/` | objet unique |
| `cam-msp1` | `uploadphotosserver` | `msp1` | `ota/cam/msp1/` | clé `msp1` (fusion) |
| `cam-n3pp` | `uploadphotosserver` | `n3pp` | `ota/cam/n3pp/` | clé `n3pp` (fusion) |
| `cam-ffp3` | `uploadphotosserver` | `ffp3` | `ota/cam/ffp3/` | clé `ffp3` (fusion) |

**ffp5cs n'est pas géré** ici : son OTA suit un schéma différent (structure `channels`,
`md5`, HTTPS) — il conserve `ffp5cs/scripts/publish_ota.ps1`. Évolution possible (voir plus bas).

## Format de signature (rappel)

Le firmware embarqué (`n3_ota.cpp`) vérifie :
- `sha256` = hex minuscule des octets du `.bin` ;
- `signature` = **base64 d'une signature ECDSA DER sur le digest sha256**, soit exactement
  `openssl dgst -sha256 -sign cle.pem firmware.bin`. Clé publique :
  `shared/n3_common/src/n3_ota_pubkey.h` (committée).

## Configuration requise (une seule fois)

Dans **Settings ▸ Secrets and variables ▸ Actions** du dépôt :

**Secrets :**
| Nom | Contenu |
|-----|---------|
| `OTA_SIGNING_KEY` | Clé **privée** ECDSA P-256 (PEM) — celle dont la publique est dans `n3_ota_pubkey.h`. |
| `CREDENTIALS_H` | Contenu **complet** du `credentials.h` de prod (WiFi `WIFI_LIST`, SMTP, `API_KEY`). Indispensable : un binaire OTA sans vrais identifiants serait hors-ligne. |
| `N3_SERVEUR_DEPLOY_TOKEN` | PAT (fine-grained) avec **Contents: write** sur le dépôt `n3_serveur`. |

**Variables (facultatives, défauts entre parenthèses) :**
| Nom | Défaut | Rôle |
|-----|--------|------|
| `N3_SERVEUR_REPO` | `oliviera999/n3_serveur` | Dépôt servant `iot.olution.info`. |
| `N3_SERVEUR_OTA_ROOT` | `serveur/ota` | Racine OTA dans ce dépôt. |

> ⚠️ Vérifier `N3_SERVEUR_OTA_ROOT` : le chemin réel dépend de l'arborescence de `n3_serveur`
> (le firmware sert `/ota/…`, mais l'emplacement des fichiers dans le repo peut différer).

## Utilisation

1. Onglet **Actions ▸ Firmware OTA Deploy ▸ Run workflow**.
2. Choisir le **firmware**, le **canal** (`test` par défaut, recommandé pour valider), et
   laisser **dry_run = true** d'abord : compile + signe, **ne pousse rien** (affiche le metadata).
3. Relancer avec **dry_run = false** pour publier réellement dans `n3_serveur`.
4. Les appareils prennent la MAJ au prochain `n3OtaCheck()` (comparaison de version).

Le déclenchement est **manuel** à dessein (un OTA flashe des appareils physiques). Un
déclenchement sur tag de version est possible en évolution (voir ci-dessous).

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

- **Déclenchement par tag** (`ota-n3pp-v4.43`) pour un release automatisé.
- **Support ffp5cs** : adapter `publish_ota.py` au schéma `channels`/`md5`/HTTPS.
- **Validation post-déploiement** : `curl -I` sur l'URL publiée pour confirmer la mise en ligne.
