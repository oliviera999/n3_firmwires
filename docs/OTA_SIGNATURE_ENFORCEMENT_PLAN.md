# Plan d'activation de l'enforcement de signature OTA (`N3_OTA_REQUIRE_SIGNATURE`)

> **Statut :** chantier **différé, non bloquant**. Rien n'est activé tant que la
> flotte ne peut pas être re-flashée. Ce document décrit la séquence sûre pour une
> prochaine session. Audit source : `docs/AUDIT_GENERAL_2026-07.md` §7 et
> `n3_serveur/docs/AUDIT_CONTRAT_FIRMWARE_SERVEUR_2026-07.md`.

## État actuel (constat d'audit)

| Cible | Pile OTA | Métadonnée serveur | Signature ? | Enforcement |
|-------|----------|--------------------|-------------|-------------|
| n3pp | `shared/n3_ota` | `ota/n3pp*/metadata.json` | ✅ sha256 + ECDSA **P-521** | ❌ flag absent |
| msp | `shared/n3_ota` | `ota/msp*/metadata.json` | ✅ | ❌ flag absent |
| cam (msp1/n3pp/ffp3) | `shared/n3_ota` | `ota/cam/metadata.json` | ✅ | ❌ flag absent |
| **pgl** | `shared/n3_ota` | *(aucune cible publiée)* | — | ❌ à publier |
| **ffp5cs** | pile propre (`ota_manager*`) | `ota/metadata.json` | ❌ **md5 seul** | ❌ (train séparé) |

Points clés vérifiés :
- La clé embarquée (`shared/n3_common/src/n3_ota_pubkey.h`) est **secp521r1 / P-521**
  (les commentaires « P-256 » étaient faux — corrigés, cf. D1). Les signatures serveur
  sont P-521 → **appariées**. `mbedtls_pk_verify` (`n3_ota.cpp:104`) valide contre la
  courbe parsée → **l'enforcement ne briquera pas** n3pp/msp/cam pour cause de courbe.
- `N3_OTA_REQUIRE_SIGNATURE` (`n3_ota.cpp:331`) n'est **défini nulle part** → aujourd'hui
  fail-open sha256-only quand la signature est absente.
- Le rollback est déjà possible : **firmware** = auto-rollback ESP-IDF
  (`n3_ota_rollback`, opt-in `N3_OTA_ROLLBACK_ENABLE`) ; **serveur** =
  `n3_serveur/bin/ota-rollback.php` (voir `n3_serveur/docs/OTA_ROLLBACK.md`).

## Séquence sûre (ordre impératif : a → b → c)

### Train A — n3pp / msp / cam
1. **(a) Le serveur signe systématiquement.** Snapshoter l'état sain courant
   (`php bin/ota-rollback.php --target <t> --snapshot`), puis republier chaque cible
   aux versions courantes avec `publish_ota.py --key <clé privée P-521>`. **Automatiser**
   la signature en CI (`firmware-ota-deploy.yml`, clé privée en secret) pour qu'aucune
   métadonnée ne parte sans `signature`.
2. **(b) Validation sur banc.** Device témoin compilé avec `-DN3_OTA_REQUIRE_SIGNATURE` :
   - accepte une MAJ correctement signée ;
   - **refuse** une MAJ sans signature et une MAJ au binaire altéré.
   - Activer aussi `-DN3_OTA_ROLLBACK_ENABLE` (+ `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`)
     et vérifier le retour auto sur image précédente si l'auto-test échoue.
   - Combler la **dette de test S4** : ajouter un test natif Unity de
     `verifyFirmwareSignature` (aujourd'hui non couvert).
3. **(c) Activation du flag.** Ajouter `-DN3_OTA_REQUIRE_SIGNATURE` aux `build_flags`
   de n3pp, msp, uploadphotosserver ; bumper les versions ; rollout **progressif**.
   Ne jamais activer avant que (a)+(b) soient verts **pour la cible concernée**.

### Train B — pgl
Publier d'abord la cible pgl signée (`publish_ota.py --firmware pgl --key …` → crée
`ota/pgl/metadata.json`), valider sur banc, **puis** activer le flag côté pgl. Activer
sans cible publiée = plus aucune MAJ possible.

### Train C — ffp5cs (le plus lourd)
`ffp5cs` a sa **propre** pile OTA (md5 par défaut, `OTA_REQUIRE_SIGNATURE=false`), **mais
elle sait déjà vérifier sha256 + ECDSA** (`ota_manager_validate.cpp`, `ota_signing_pubkey.h`)
avec **la même clé P-521 que shared** (vérifié). L'enforcement ffp5cs **ne dépend donc pas** de
la migration :
1. Faire émettre **sha256 + signature** au schéma `ffp5` de `publish_ota.py` (aujourd'hui md5 seul).
2. Sur banc : `OTA_REQUIRE_SIGNATURE=true` → signé accepté, non-signé/altéré refusé.
3. Publier + rollout.
> **Tant que 1–2 ne sont pas faits : ne pas activer** (briquerait l'OTA ffp5cs).

**Migration `shared/n3_ota` (décidée) — chantier séparé.** ffp5cs étant un **sur-ensemble**
de `shared/n3_ota` (il ajoute la MAJ littlefs, le schéma channels, la sélection S3), un
« remplacement » naïf **régresserait**. Voir l'analyse et le plan par étapes dédiés :
[`OTA_FFP5CS_MIGRATION_SHARED.md`](OTA_FFP5CS_MIGRATION_SHARED.md). Cette migration est un
nettoyage d'architecture **indépendant** de la sécurité (points 1–2 ci-dessus) et nécessite un
banc (app + littlefs + S3).

## Chantiers transverses associés (non bloquants)
- **TLS** : `publish_ota.py` sert désormais les cibles n3ota en **https** (O3, fait).
  Étape suivante : livrer le bundle CA (`n3_ota_ca_cert.h`) et retirer `setInsecure()`
  une fois le certificat `iot.olution.info` stable/documenté (S2).
- **HMAC legacy** (S6/H1) : migrer n3pp/msp/pgl/upload en **X-Sig-only** (retirer le
  bloc legacy `shared/n3_data/src/n3_data.cpp:95-108` — ffp5cs le fait déjà), confirmer
  via l'audit HMAC serveur, puis retrait du chemin legacy côté serveur.
- **Versions OTA** (O4) : republier toutes les cibles aux versions courantes
  (n3pp 4.63, msp 2.66, upload 2.70, pgl 0.5.16, ffp5cs 15.20) une fois la flotte dégelée.
