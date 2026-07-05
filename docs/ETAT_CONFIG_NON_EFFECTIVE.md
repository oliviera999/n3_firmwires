# État des configurations « implémentées mais pas (forcément) effectives » — Firmwares

> Recensement des fonctionnalités **présentes dans le code** mais **inactives / non déployées**
> (fichier de secrets non fourni, env jamais buildé en CI, cible OTA absente, feature volontairement
> neutralisée). Objectif : ne plus confondre « le code existe » et « c'est effectif ».
>
> Portée : CI / builds / tests, OTA, versionnage, mails firmware.
> Voir aussi le pendant serveur dans `n3_serveur/docs/ETAT_CONFIG_NON_EFFECTIVE.md`.

Légende : ✅ actif · ⚠️ implémenté mais conditionné · ❌ implémenté mais inactif / non déployé.

## Mails / SMTP firmware

| Élément | Emplacement | Statut | Pour l'activer |
|---|---|---|---|
| Lib `n3_mail` + taxonomie `n3_notify` (P1–P4) | `shared/n3_mail/` | ✅ Compilée et testée en CI (`test_mail`, `test_notify`). | — |
| Alertes mail n3pp / msp | `n3pp/src/n3pp_automation.cpp`, `msp/src/msp_automation.cpp` | ⚠️ Actives par défaut (`enableEmailChecked="checked"`) mais SMTP + destinataire viennent de `credentials.h` **non versionné** (le `.example` n'a que des placeholders). Télécommandable par le serveur (clé `101`). | Fournir un `credentials.h` de prod (app password) ; garder le toggle serveur sur `checked`/`full`. |
| Mails debug/événement CAM | `uploadphotosserver/src/main.cpp`, `include/config.h` | ⚠️ `MAIL_NOTIFICATIONS_ENABLED=1` mais double garde `#if defined(SMTP_*)` + runtime `remoteMailNotifEnabled`. | SMTP réel + flag distant activé. |
| Mail « démarrage OTA » CAM | `uploadphotosserver/src/main.cpp` (`otaMailStartCallback`) | ❌ Volontairement neutralisé (ne fait que logguer) : TLS + sha256 OTA sur la loopTask = stack canary panic sur ESP32-CAM sans PSRAM. | Non trivial (contrainte mémoire) — laisser tel quel sauf refonte tâche dédiée. |
| Mailer ffp5cs | `ffp5cs/src/mailer.cpp` (`-DFEATURE_MAIL=1`) | ✅ Actif, dépend de `ffp5cs/include/secrets.h` (non versionné). | Fournir `secrets.h` de prod. |
| HMAC n3pp / msp (`API_SIG_SECRET`) | `firmwires/credentials.h` + lib `n3_hmac` | ⚠️ Supporté si la macro est définie dans `credentials.h` (voir `.example`) **et** alignée sur `API_SIG_SECRET` du `.env` serveur. Souvent absent en prod → POST en `api_key` legacy. | Ajouter `API_SIG_SECRET` dans `credentials.h`, reflasher. |
| HMAC poissonglouton (`PGL_API_SIG_SECRET`) | `poissonglouton/src/pgl_network.cpp` | ❌ Option firmware ; le serveur PGL ne valide que `api_key` (`PGL_API_KEY`). | Ne pas activer tant que le serveur n'implémente pas la validation. |

## CI / builds / tests

| Élément | Emplacement | Statut | Note |
|---|---|---|---|
| Suites natives shared `test_data` & `test_net_stats` | `.github/workflows/firmware-ci.yml`, `shared/tests_native/` | ✅ **Corrigé** : ajoutées au loop CI (elles existaient mais n'étaient jamais exécutées). `test_data` échouait sur un trou du mock natif (`String::substring` absent) — mock complété. Les 6 suites shared passent (50 cas). | Listes de suites **codées en dur** → toute nouvelle suite doit être ajoutée à la main, sinon ignorée silencieusement. |
| `ffp5cs/wroom-prod` + envs S3 / beta / https / tls | `.github/workflows/firmware-ci.yml`, `ffp5cs/platformio.ini` | ❌ Jamais buildés en CI (seul `wroom-test`). `wroom-s3-test` retiré (incompat `ESP32Servo@3.0.5` vs arduino-esp32 2.0.x) ; `wroom-prod-https` bloqué par garde-fou (pas de transport TLS). | Réactiver après alignement des versions de libs. |
| Variantes poissonglouton (`-pir`, `-sleep`, `-jc3248*`, `-debug`) | `poissonglouton/platformio.ini` | ❌ Définies mais hors CI. | Ajouter à la matrice si maintenues. |

## OTA

| Élément | Emplacement | Statut | Pour l'activer |
|---|---|---|---|
| Lib `n3_ota` (sha256 + ECDSA P-256) + clé publique embarquée | `shared/n3_common/n3_ota/` | ✅ Présente ; branchée sur n3pp / msp / cam / ffp5cs. | — |
| `OTA_CA_CERT` (validation du certif serveur) | `shared/n3_common/.../n3_ota_pubkey.h` | ❌ Non défini → mode `setInsecure()` : TLS chiffré mais **certificat non vérifié** (Phase 2 non activée). Authenticité assurée par la signature ECDSA. | Récupérer le CA de `iot.olution.info` et définir `OTA_CA_CERT`. |
| **OTA poissonglouton** | `poissonglouton/` (`PGL_ENABLE_OTA=1`, interroge `/ota/pgl/`), `firmwares.manifest.json`, `tools/ota/publish_ota.py`, `.github/workflows/firmware-ota-{deploy,rollback}.yml` | ✅ **Câblé** : `otaTarget:"pgl"`, cible `pgl` (schéma n3ota, prod-only) ajoutée à `publish_ota.py` + aux workflows deploy/rollback + doc `OTA_GITHUB_DEPLOY.md`. Binaire construit depuis `pgl-s3-display` (env de prod). Validé localement (metadata généré, canal test rejeté). Le dossier serveur `/ota/pgl/` est créé au 1er déploiement (mkdir automatique). | Déclencher un déploiement : `Actions ▸ Firmware OTA Deploy ▸ firmware=pgl` (ou tag `ota-deploy/pgl/prod`). ⚠️ Une seule cible OTA pour pgl : couvre **`pgl-s3-display` (JC4827W543)** ; les variantes `pgl-s3-jc3248` / `pgl-s3-headless` ont un binaire distinct non couvert. |
| Pipeline `firmware-ota-deploy.yml` (+ rollback) | `.github/workflows/` | ⚠️ Complet, mais dépend de secrets GitHub (`OTA_SIGNING_KEY`, `CREDENTIALS_H`, `FFP5CS_SECRETS_H`, `FFP5CS_SECRETS_CONFIG_H`, `N3_SERVEUR_DEPLOY_TOKEN`) + environnement `prod` (required reviewers). Déclenchement manuel/par tag, jamais sur push. | Provisionner les secrets et l'environnement dans Settings › Secrets. |

## Versionnage & catalogue

| Élément | Statut | Note |
|---|---|---|
| Bump de version firmware | ⚠️ 100 % manuel (skill `bump-firmware-version`). `publish_ota.py` a un garde-fou (refuse une version ≤ déployée) mais **n'incrémente pas**. | Aucune Action de bump/tag automatique. |
| `firmwares.manifest.json` (`pioEnvs`) | ⚠️ Reflète un sous-ensemble curé, pas tous les envs de `platformio.ini`. | ✅ **Corrigé partiellement** : ajout de `n3pp-https` / `msp-https` (envs canoniques listés dans `CLAUDE.md`). Les variantes expérimentales (S3, jc3248, pir, sleep, beta, tls) restent volontairement hors catalogue. |

## Ce qui ne peut PAS être activé sans intervention manuelle / décision

- **Secrets `credentials.h` / `secrets.h`** : hors dépôt → mails firmware inopérants sans eux.
- **Secrets GitHub OTA-deploy + environnement `prod`** : à provisionner côté repo.
- ~~**OTA poissonglouton** : décision de câbler la chaîne de publication ou de la désactiver.~~ ✅ Chaîne câblée (prod, `pgl-s3-display`) ; reste à lancer un déploiement quand une version est prête. Si la flotte mélange plusieurs cartes (JC4827 / JC3248 / headless), décider comment couvrir les binaires distincts.
- **`OTA_CA_CERT`** : nécessite le CA du serveur (Phase 2 TLS).
- **Réactivation des envs ffp5cs S3 / https** : dépend d'un alignement de versions de libs.
