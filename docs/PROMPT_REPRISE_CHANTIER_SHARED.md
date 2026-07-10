# Prompt de reprise — chantier « core architectural partagé »

> Copier-coller le bloc ci-dessous comme premier message d'une **nouvelle session**
> Claude Code sur le dépôt `oliviera999/n3_firmwires`. Il est autonome, mais les deux
> documents qu'il référence font foi pour tout détail.

---

Tu reprends un chantier de refactorisation en cours sur ce monorepo de firmwares
ESP32 (famille « salle aérée n³ » : n3pp, msp, uploadphotosserver, ffp5cs,
poissonglouton). Objectif du chantier : construire un **core architectural partagé**
dans `shared/` en prenant le meilleur de chaque firmware, sans jamais régresser.

## Lecture obligatoire avant toute action (dans cet ordre)

1. `CLAUDE.md` (règles du dépôt : secrets, versionnage, CI, interdits).
2. `docs/CHANTIER_CORE_SHARED_TRANCHES.md` — **cahier des charges du chantier** :
   état livré (PR #86 mergée : tranches L1, L1b, L2), règles invariantes (méthode
   additive→câblage, CI = vérificateur, versionnage), **pièges gelés**
   (A6 HeureSansWifi, A7 PontDiv, A10 clés 104/105, N3NetStats non thread-safe,
   nonce par firmware, zéro String en chemin chaud, 3 toolchains, psramFound),
   et le détail des tranches suivantes T1→T6.
3. `docs/PROPOSITION_REFACTORISATION_SHARED.md` — l'analyse complète qui fonde le
   chantier (3 axes, vérification adversariale, matrice « meilleur de la famille »,
   plan L1→L7, sections « ne pas toucher »).

## Phase 0 — Vérification de la première PR (#86, mergée) — OBLIGATOIRE

Avant d'écrire du code, exécute le « Protocole de vérification » (§3 du cahier des
charges) :
- Vérifie que les runs `firmware-ci.yml` sur `master` depuis le merge de #86 sont
  **verts** (tests natifs Unity + tous les builds matriciels). Utilise les outils
  GitHub MCP (`actions_list`, `get_job_logs`) — le CLI `gh` n'est pas disponible.
- Relis le diff mergé sur les 5 points sensibles listés au §3 (HMAC n3_data/ffp5cs/
  upload, suppression des 6 headers locaux ffp5cs, include `n3_hmac_canonical.cpp`
  dans `test_data.cpp`).
- Si quelque chose est rouge ou incohérent : **corrige d'abord**, dans une PR dédiée,
  avant toute nouvelle tranche.
- Note : master évolue en parallèle (versions ffp5cs, envs CI `*-https`, nouvelles
  libs comme `n3_tracker`) — raisonne toujours sur l'état ACTUEL de master ; les
  numéros de version cités dans les docs sont ceux du moment du merge.

## Phase 1 — Exécution des tranches suivantes

Exécute les tranches **dans l'ordre**, une par une, en respectant strictement le §4
(règles) et le §5 (spécifications) du cahier des charges :

- **T1** (reste de L2) : `n3DataSendHeartbeat` → `n3_data` (corps verbatim
  n3pp/msp vérifié identique) ; `n3TimeSyncBrokenDown` → `n3_time` (resync 6 globals
  dupliqué 3×) ; adoption de `n3PrintWakeupReason` (déjà dans shared) par n3pp/msp.
- **T2** (L3) : `sensor_failure_manager` (retirer `config.h`, injecter la macro de
  log) + `sensor_reading_fallback` (renommer l'API `waterLevel` en neutre) →
  `n3_analog_sensors` ; puis câblage ffp5cs, puis adoption n3pp/msp (amélioration
  documentée, pas refacto neutre).
- **T3** (L4) : nouvelles libs `n3_upload` (multipart streaming, callback `onStats`)
  et `n3_store_forward` (peek→commit, drop-oldest, pacing, budget temps, 429,
  `nowMs`/`sleepMs` injectés → tests natifs complets) ; brancher `N3NetStats` dans
  ffp5cs **sous `s_httpMutex`**. Les esquisses d'API sont au §5.T3.
- **T4** (L5) : `n3MailNotify`, `n3_ota_ui` (harnais OTA triplé), `ota_artifact_select`
  (valider parité ArduinoJson v6/v7 d'abord), rollback OTA 1er boot (opt-in).
- **T5** (L6) : `n3_log` ; **T6** (L7) : `n3_app` — uniquement après les préalables
  d'harmonisation listés (msp_globals.cpp, A6/A7/A8/A10).

## Méthode de travail imposée

- **Une tranche = additive d'abord** (brique + tests natifs + CI, zéro consommateur),
  push, CI verte ; **puis câblage** (sans changement observable), push, CI verte.
  Ne jamais empiler une tranche sur une CI non verte.
- PlatformIO n'est pas installé dans la session : **la CI est ton vérificateur**.
  Ajoute chaque nouvelle suite de test à la boucle du job « Tests natifs (Unity) »
  de `.github/workflows/firmware-ci.yml`.
- Travaille sur une branche dédiée `claude/…`, pousse avec `git push -u origin`,
  ouvre une **PR draft** vers `master`, abonne-toi (`subscribe_pr_activity`) et
  corrige tout rouge CI. La branche `claude/shared-refactor-proposal-hpv048` de la
  PR #86 est mergée : ne la réutilise pas telle quelle (repars de master).
- À chaque tranche : bump semver des libs shared touchées (`library.json` + table
  `shared/README.md`), bump du/des firmware(s) touché(s) (skill
  `bump-firmware-version` ; source de version dans `firmwares.manifest.json`) +
  entrée `VERSION.md`, et annote la tranche ✅ dans
  `docs/PROPOSITION_REFACTORISATION_SHARED.md`.
- Ne commite JAMAIS `credentials.h` ni `ffp5cs/include/secrets*.h`. Ne touche pas à
  `archive/` ni `à voir/`. Ne « remplace » pas les modules ffp5cs listés « ne pas
  toucher » (web_client, net_request_pool, ota_manager, power, nvs_manager,
  wifi_manager, ffp3_post_body).
- En cas de doute sur un point de contrat (ex. format de nonce côté serveur, langue
  des logs de `print_wakeup_reason`), pose la question à l'utilisateur plutôt que de
  trancher silencieusement — sauf si le cahier des charges tranche déjà.
- Pour les analyses larges (inventaire de sites d'appel, vérification adversariale
  d'une tranche), utilise des agents parallèles en lecture seule avant d'éditer.

Commence par la Phase 0 et rends compte de son résultat avant d'attaquer T1.

---

*Fin du prompt. Documents de référence : `docs/CHANTIER_CORE_SHARED_TRANCHES.md`
(cahier des charges) et `docs/PROPOSITION_REFACTORISATION_SHARED.md` (analyse).*
