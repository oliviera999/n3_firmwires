---
name: external-inspiration
description: Draw inspiration from well-known GitHub repositories or proven libraries (arduino-esp32/ESP-IDF patterns, reputable Arduino libs, reference OTA/HMAC projects…) while writing firmware code — and cite the source properly. Use when adopting an external pattern/approach/snippet, evaluating how a respected project solves a problem, or adding a dependency, so the borrowing is credited, license-safe, and adapted to the repo's conventions.
---

# S'inspirer de bonnes pratiques externes (avec citation)

Tu es **libre de t'inspirer d'excellentes pratiques** décrites dans des dépôts GitHub connus et
accessibles ou des bibliothèques éprouvées — c'est encouragé pour la qualité, la robustesse et la
sécurité du code embarqué. La contrepartie est **toujours** de créditer et de rester propre.

> Règle de référence : section « Inspiration — bonnes pratiques externes » de `CLAUDE.md`.

## Quand utiliser ce skill

- Tu reprends une **approche, un pattern ou un extrait** d'un projet externe (ex. gestion OTA,
  vérification sha256/ECDSA, HMAC, scan WiFi RSSI, deep sleep, drivers capteurs…).
- Tu regardes **comment un projet reconnu** (arduino-esp32, ESP-IDF, une lib Arduino populaire)
  résout un problème avant d'écrire ta version.
- Tu envisages d'**ajouter une bibliothèque** (`lib_deps` PlatformIO) plutôt que réimplémenter.

## Procédure (obligatoire)

1. **Identifier la source** : nom du projet / de la bibliothèque, URL, et version/commit si pertinent.
2. **Vérifier la licence** : ne jamais copier-coller du code sous licence incompatible. Adapter /
   réécrire, mentionner licence + origine. **En cas de doute sur la compatibilité, demander avant
   d'intégrer.**
3. **Adapter, ne pas plaquer** : rester cohérent avec les conventions du dépôt — mutualiser dans les
   libs `shared/`, respecter la toolchain (pioarduino / espressif32) et le style existant, plutôt que
   dupliquer un pattern externe tel quel.
4. **Citer** :
   - dans le **message de commit / la description de PR** (ce dont on s'est inspiré + lien) ;
   - **en commentaire de code** juste au-dessus du passage concerné si l'emprunt est localisé, avec la
     licence si du code a été repris.

## Exemple de citation en code

```cpp
// Inspiré de <projet> (<url>, vX.Y, licence MIT) : schéma de retry exponentiel du POST.
// Adapté aux conventions n3_data (URL-encoded + HMAC).
```

## Rappels

- ✅ Préférer une **dépendance PlatformIO** propre à un copier-coller quand la lib existe et est maintenue.
- ✅ L'inspiration externe ne dispense **jamais** des règles du dépôt (secrets non versionnés, bump de
  version, tests natifs, `firmwares.manifest.json` à jour).
