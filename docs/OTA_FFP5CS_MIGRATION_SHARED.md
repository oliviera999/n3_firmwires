# ffp5cs OTA → `shared/n3_ota` : analyse de migration & plan

> Décision utilisateur : **migrer** ffp5cs vers la pile OTA partagée. Ce document
> établit ce que « migrer » implique réellement (l'analyse révèle un piège), les
> options, et le plan par étapes. **Rien n'est exécuté** tant que ffp5cs n'est pas
> compilable/flashable/validable sur banc (la migration touche le plus gros firmware).

## Constat : ffp5cs est un SUR-ensemble de `shared/n3_ota`

| Capacité | `shared/n3_ota` | ffp5cs (pile propre) |
|----------|:---:|:---:|
| MAJ image applicative (`U_FLASH`) | ✅ (`n3_ota.cpp:197`) | ✅ |
| **MAJ système de fichiers (littlefs.bin, `U_SPIFFS`)** | ❌ **absent** | ✅ (`filesystem_url/size/md5`) |
| Schéma métadonnée | objet plat `{version,url,sha256,signature}` (+ `metadataKey`) | **`channels`** (`prod/test` × `model`), md5 |
| Sélection carte **S3 vs WROOM** | ❌ | ✅ (`ESP32_S3_FOLDER`, `BOARD_S3`) |
| Vérif **sha256 + ECDSA** | ✅ P-521 | ✅ **déjà présente** (`ota_manager_validate.cpp`, `ota_signing_pubkey.h`, flag `OTA_REQUIRE_SIGNATURE`) |
| Rollback ESP-IDF | ✅ (`n3_ota_rollback`, opt-in) | à vérifier |
| API publique | `n3OtaCheck(N3OtaConfig)` (24 lignes de `.h`) | ~1650 lignes (`ota_manager*.cpp`) |

**Faits vérifiés :**
- La clé publique embarquée est **identique** entre ffp5cs (`ota_signing_pubkey.h`) et
  shared (`n3_ota_pubkey.h`) : **même paire ECDSA P-521**. Un seul secret serveur signe
  donc pour toutes les cibles.
- `shared/n3_ota` ne gère qu'**une seule** image (`Update.begin(..., U_FLASH)`), **sans**
  mise à jour du système de fichiers littlefs — que ffp5cs utilise (partitions ESP Mail).

> ⚠️ **Piège :** un « remplacement » naïf de la pile ffp5cs par `shared/n3_ota` **régresse**
> (perte de la MAJ littlefs, du schéma channels, de la sélection S3). « Migrer » impose donc
> d'abord d'**enrichir `shared/n3_ota`**, ou d'y **promouvoir** la pile ffp5cs.

## Corollaire important pour l'enforcement de signature (train C)

**ffp5cs sait déjà vérifier une signature ECDSA (même clé que shared).** L'enforcement de
signature sur ffp5cs **ne dépend donc pas** de la migration. Le chemin le plus court vers la
sécurité :
1. Faire émettre **sha256 + signature** au schéma `ffp5` de `tools/ota/publish_ota.py`
   (aujourd'hui md5 seul).
2. Sur banc : `OTA_REQUIRE_SIGNATURE=true` → accepte signé, refuse non-signé/altéré.
3. Rollout.

La migration `shared/n3_ota` est un **nettoyage d'architecture** (dédup ~1650 lignes),
**indépendant** et non urgent pour la sécurité.

## Options de migration

- **M1 — Enrichir `shared/n3_ota` puis adopter (recommandé si on migre).**
  Porter dans shared : MAJ littlefs (`U_SPIFFS`), schéma channels, sélection S3. Puis rebrancher
  ffp5cs **et** garder n3pp/msp/cam fonctionnels. Résultat : une seule pile OTA. Effort **élevé**,
  risque **élevé** (touche l'OTA de toute la flotte). Tests natifs à étendre + banc par carte.
- **M2 — Promouvoir la pile ffp5cs dans shared.**
  Extraire l'OTA riche de ffp5cs comme nouvelle base commune, migrer n3pp/msp/cam dessus.
  Effort élevé ; avantage : on part du code le plus complet.
- **M3 — Enforcement d'abord, migration ensuite (séquencement recommandé).**
  Traiter la sécurité (ci-dessus) sans attendre la migration ; planifier M1/M2 comme refacto
  séparée quand le banc est disponible.

## Plan par étapes (option M1)

1. **Spécifier** l'API cible de `shared/n3_ota` couvrant : app-image, littlefs, channels, S3,
   sha256+signature (déjà là), rollback. Étendre `N3OtaConfig` (ou une variante) sans casser
   les appelants n3pp/msp/cam existants.
2. **Porter la MAJ littlefs** (`U_SPIFFS`) et la **sélection d'artefact channels/S3** depuis
   `ffp5cs/src/ota_manager*.cpp` vers `shared/n3_common` ; **tests natifs** (étendre
   `test_ota_select`, ajouter un test du parseur channels + du choix littlefs).
3. **Combler la dette S4** : test natif de `verifyFirmwareSignature` (aucun aujourd'hui).
4. **Rebrancher ffp5cs** sur la pile partagée ; supprimer `ota_manager*.cpp` propres.
5. **Banc, carte par carte** (wroom-prod, wroom-test, S3) : MAJ app **et** littlefs OK ; signé
   accepté, non-signé refusé quand `REQUIRE_SIGNATURE` ; rollback auto testé.
6. **Non-régression** n3pp/msp/cam (leur OTA passe par la même pile enrichie).
7. Bumper ffp5cs (`VERSION.md`) ; mettre à jour `firmwares.manifest.json` si la topologie change.

## Checklist de validation banc (bloquant avant rollout)
- [ ] MAJ app-image (wroom) : signée acceptée, altérée refusée.
- [ ] MAJ littlefs : image FS flashée sans corruption ; ESP Mail fonctionne après.
- [ ] Carte **S3** : sélection du bon artefact, MAJ app + FS.
- [ ] `OTA_REQUIRE_SIGNATURE=true` : non-signé refusé (métadonnée serveur enrichie au préalable).
- [ ] Rollback ESP-IDF : image en échec d'auto-test → retour à la précédente.
- [ ] Non-régression n3pp/msp/cam.
- [ ] Rollback serveur disponible (`n3_serveur/bin/ota-rollback.php`) comme filet.

> **Prérequis serveur** avant enforcement ffp5cs : le schéma `ffp5` de `publish_ota.py` doit
> émettre `sha256` + `signature` (cf. `OTA_SIGNATURE_ENFORCEMENT_PLAN.md`, train C).
