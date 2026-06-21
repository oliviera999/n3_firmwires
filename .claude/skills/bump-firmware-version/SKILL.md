---
name: bump-firmware-version
description: Bump the firmware version of an n3_firmwires project (n3pp, msp, uploadphotosserver, ffp5cs, poissonglouton) and update its VERSION.md history. Use after modifying a firmware, before committing, or when asked to "bump/increment the firmware version" or "release a firmware". Each firmware defines its version in its own source file — see firmwares.manifest.json.
---

# Bump de version d'un firmware

Chaque firmware porte **sa propre version**, déclarée dans sa source. La référence machine est le
champ `versionSource` de `firmwares.manifest.json`.

## Où se trouve la version

| Firmware | Fichier | Define / motif |
|----------|---------|----------------|
| **n3pp** | `n3pp/include/n3pp_config.h` | `#define FIRMWARE_VERSION "x.y"` |
| **msp** | `msp/include/msp_config.h` | `#define FIRMWARE_VERSION "x.y"` |
| **uploadphotosserver** | `uploadphotosserver/include/config.h` | `#define FIRMWARE_VERSION "x.y"` |
| **poissonglouton** | `poissonglouton/include/config.h` | `PGL_FIRMWARE_VERSION = "x.y"` |
| **ffp5cs** | `ffp5cs/include/config.h` (`VERSION = "…"`) ou `ffp5cs/VERSION.md` | voir manifest |

## Procédure

1. **Lire** la version actuelle dans la source du firmware concerné.
2. **Incrémenter** de façon cohérente avec l'historique du firmware (la plupart suivent
   un schéma `MAJOR.MINOR` ; respecter le format déjà en place, ne pas changer le style).
3. **Écrire** la nouvelle valeur dans la source.
4. **Mettre à jour** le `VERSION.md` du firmware : ajouter une entrée d'historique
   (numéro + description claire du changement).
5. Si le firmware expose sa version au serveur (POST version, OTA), vérifier l'alignement avec
   `n3_serveur` quand un contrat change.

## Vérifications

- Ne bumper que le(s) firmware(s) réellement modifié(s).
- Ne pas coder la version en dur ailleurs que dans la source officielle ci-dessus.
- Recompiler le firmware après le bump (skill `build-firmware`) pour confirmer que ça build.
