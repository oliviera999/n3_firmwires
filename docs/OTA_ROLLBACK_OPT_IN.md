# Rollback OTA au premier boot — capacité opt-in (T4.4, chantier shared)

> **Statut : livré DÉSACTIVÉ par défaut.** Aucun firmware ne l'active à ce jour.
> ⚠️ **Test sur cible obligatoire** avant toute activation en production
> (exigence du cahier des charges `docs/CHANTIER_CORE_SHARED_TRANCHES.md`, §5.T4.4).

## Principe

Après un flash OTA, la nouvelle image peut démarrer **en attente de validation**
(`ESP_OTA_IMG_PENDING_VERIFY`). Le firmware exécute alors un **auto-test**
(typiquement : WiFi connecté + un échange serveur réussi) :

- auto-test **OK** → `esp_ota_mark_app_valid_cancel_rollback()` : l'image est validée ;
- échec **persistant** après une fenêtre de grâce →
  `esp_ota_mark_app_invalid_rollback_and_reboot()` : la carte redémarre sur
  l'**image précédente** (filet de sécurité contre une MAJ qui brique le nœud).

Source d'inspiration : mécanisme de rollback natif d'ESP-IDF (`app_update` /
`esp_ota_ops.h`, [espressif/esp-idf](https://github.com/espressif/esp-idf),
licence Apache-2.0) — adapté aux conventions du dépôt (brique `shared/`, logique
de décision pure testée en natif).

## Ce que fournit `shared/n3_common`

| Élément | Fichier | Disponibilité |
|---|---|---|
| Décision pure `OtaRollback::decide(pendingVerify, selfTestOk, elapsedMs, graceMs)` | `n3_ota_rollback.h` | toujours (testée en natif, `test_ota_rollback`) |
| `n3OtaRollbackIsPendingVerify()` / `n3OtaRollbackMarkValid()` / `n3OtaRollbackRejectAndReboot()` | `n3_ota_rollback.{h,cpp}` | **seulement avec `-DN3_OTA_ROLLBACK_ENABLE`** |

Règles de la décision (voir tests natifs) :
- image non `PENDING_VERIFY` → `None` (mécanisme inerte) ;
- auto-test OK → `MarkValid` **même après la fenêtre** (un WiFi lent ne condamne
  pas une image saine déjà connectée) ;
- échec à l'expiration de la fenêtre (`elapsedMs >= graceMs`) → `Rollback`.

## Précondition matérielle/toolchain — À LIRE AVANT D'ACTIVER

Le passage d'une image par `ESP_OTA_IMG_PENDING_VERIFY` **exige un bootloader
compilé avec `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`**.

> **Mise à jour 2026-11-07 (ffp5cs 15.34).** Contrairement à ce qu'affirmait ce document, les deux
> cores pré-compilés du dépôt **activent** cette option : `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE=y` et
> `CONFIG_APP_ROLLBACK_ENABLE=y` dans `framework-arduinoespressif32-libs/esp32/sdkconfig` (pioarduino
> 55.03.37 / arduino-esp32 3.3.7) comme dans `framework-arduinoespressif32/tools/sdk/esp32/sdkconfig` et
> `esp32s3/sdkconfig` (espressif32@6.13.0 / arduino-esp32 2.0.17). ffp5cs s'appuie dessus depuis 14.17
> (`SystemBoot::validatePendingOta`) et, depuis 15.34, ne valide l'image qu'après un **contact serveur
> signé accepté** (sinon `esp_ota_mark_app_invalid_rollback_and_reboot()` après 15 min) — scénario de
> banc décrit dans `ffp5cs/VERSION.md` (15.34). Pour n3pp / msp, la capacité reste **opt-in** (flag
> `N3_OTA_ROLLBACK_ENABLE`) : le portage 4.76 / 2.79 n'active pas le rollback, il journalise seulement
> (`n3_contact_diag`). Le paragraphe ci-dessous décrit le cas d'un bootloader SANS l'option.

Avec un bootloader standard sans l'option, l'état reste
`ESP_OTA_IMG_UNDEFINED`, aucune image n'attend de validation, et l'API se réduit
à des **no-ops sûrs** (pas de rollback possible, pas d'effet de bord non plus).

Activer réellement la capacité suppose donc, au choix :
1. un build avec sdkconfig custom activant l'option (ex. pioarduino
   `custom_sdkconfig`, à valider par carte/partitionnement) ; **ou**
2. attendre une évolution des cores pré-compilés.

C'est précisément pourquoi le test sur cible est obligatoire : l'état
`PENDING_VERIFY`, le déclenchement du rollback et la survie de l'ancienne image
doivent être observés sur banc avant tout déploiement.

## Câblage type (firmware volontaire, une fois l'opt-in décidé)

```ini
; platformio.ini (env de test banc UNIQUEMENT d'abord)
build_flags =
    ${env.build_flags}
    -DN3_OTA_ROLLBACK_ENABLE
```

```cpp
#include "n3_ota_rollback.h"

// En fin de setup(), après Wificonnect() et le premier échange serveur :
#if defined(N3_OTA_ROLLBACK_ENABLE)
const bool selfTestOk = (WiFi.status() == WL_CONNECTED) && postOkThisWake;
switch (OtaRollback::decide(n3OtaRollbackIsPendingVerify(), selfTestOk,
                            millis(), 120000 /* 2 min */)) {
  case OtaRollback::Action::MarkValid: n3OtaRollbackMarkValid(); break;
  case OtaRollback::Action::Rollback:  n3OtaRollbackRejectAndReboot(); break;
  case OtaRollback::Action::None:      break;  // fenêtre en cours ou rien à faire
}
#endif
```

> Cohorte deep-sleep : la « fenêtre de grâce » se raisonne par réveil — si
> l'image est encore `PENDING_VERIFY` au réveil suivant, `millis()` repart de
> zéro ; prévoir un compteur `RTC_DATA_ATTR` de réveils en échec si l'on veut un
> budget multi-réveils (à trancher sur banc).

## Checklist de validation sur cible (avant généralisation)

1. Build banc avec `N3_OTA_ROLLBACK_ENABLE` + bootloader rollback (sdkconfig custom).
2. OTA d'une image saine → vérifier `PENDING_VERIFY` au boot, puis `[OTA][ROLLBACK] image validee`.
3. OTA d'une image sabotée (WiFi coupé / serveur inaccessible) → vérifier le
   rollback effectif vers l'ancienne version après la fenêtre.
4. Vérifier l'interaction avec `n3OtaSyncBootPartition()` (appelé au boot par
   n3pp/msp/upload) : la resynchronisation de la partition de boot ne doit pas
   masquer l'état de rollback.
5. Documenter le verdict dans le `VERSION.md` du firmware pilote.
