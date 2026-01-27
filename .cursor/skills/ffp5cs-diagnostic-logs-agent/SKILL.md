---
name: ffp5cs-diagnostic-logs-agent
description: Analyse les logs et rapports de tests FFP5CS (fichiers .log.errors, .txt, RAPPORT_*, ANALYSE_*) en s’appuyant sur les scripts PowerShell du repo pour proposer des pistes de bug et des points suspects. À utiliser en mode post-mortem après une session de tests, en lecture seule ou quasi (focus diagnostic, pas de refactor massif).
---

# FFP5CS – Agent Diagnostic & Analyse de logs

## Quand utiliser cette skill

Utiliser cet agent **après une session de tests** ou de monitoring, quand de nouveaux fichiers sont apparus, par exemple :
- fichiers `*.log.errors` générés par les scripts de monitoring,
- fichiers texte de diagnostic (ex. `diagnostic_*.txt`),
- rapports d’analyse ou de post-mortem (ex. `RAPPORT_*.md`, `ANALYSE_*.md`, `*_analysis.txt`),
- nouveaux scripts ou sorties PowerShell liés au debug.

Cas typiques :
- comprendre pourquoi un test hardware a échoué ou un comportement étrange est apparu,
- analyser une séquence de crash, de watchdog ou de blocage,
- identifier des **erreurs réseau récurrentes** ou des timeouts dans les logs,
- préparer un **rapport de bug** ou une liste de pistes à creuser dans le code.

Toujours combiner ce skill avec les règles cœur de projet FFP5CS (offline-first, simplicité, robustesse) et, si nécessaire, avec :
- `ffp5cs-logging-and-diagnostics` pour la cohérence des logs,
- `ffp5cs-dev-workflow-hardware` pour la validation sur hardware réel.

## Objectifs de l’agent

- Exploiter **au maximum les artefacts de debug existants** (scripts PowerShell, rapports, fichiers de logs) avant de modifier le code.
- Fournir une **analyse structurée** :
  - contexte des tests,
  - symptômes observés dans les logs,
  - corrélation avec les modules / tâches FreeRTOS / fonctions du firmware,
  - liste priorisée de pistes de bug.
- Rester en **mode diagnostic** :
  - lecture seule ou quasi,
  - pas de refactor massif ou de réorganisation de projet,
  - proposer d’abord des hypothèses et de petites corrections ciblées.

## Sources d’information à privilégier

Lorsque cet agent est utilisé, prioriser la lecture des fichiers et scripts suivants (patterns indicatifs) :

- **Logs bruts et erreurs**
  - `*.log.errors` (ex. `monitor_wroom_test_*.log.errors`)
  - fichiers de log ou d’export texte produits par les scripts (ex. `*_diagnostic*.txt`, `*_monitoring*.txt`)

- **Rapports et analyses**
  - `RAPPORT_*.md` (rapports détaillés, post-mortem, synthèses)
  - `ANALYSE_*.md` ou `*_analysis.txt` (analyses structurées d’une session de tests)

- **Scripts PowerShell de diagnostic**
  - scripts comme `monitor_*.ps1`, `flash_monitor_*.ps1`, `generate_diagnostic_report.ps1`, `extract_debug_log.ps1`, `diagnostic_serveur_distant.ps1`, etc.
  - lire ces scripts pour comprendre :
    - comment les logs sont générés,
    - quelles commandes sont exécutées,
    - quelles parties du système sont sollicitées pendant le test.

## Workflow recommandé (mode post-mortem)

1. **Identifier les artefacts récents**
   - Lister les fichiers récents dans le repo concernés par :
     - `*.log.errors`
     - `diagnostic_*.txt`
     - `RAPPORT_*.md`, `ANALYSE_*.md`
     - `*_analysis.txt`
   - Se concentrer en priorité sur **les plus récents** (par date ou par nom horodaté).

2. **Comprendre le contexte de test**
   - Lire rapidement le ou les derniers fichiers `RAPPORT_*.md` / `ANALYSE_*.md` associés :
     - objectif du test,
     - configuration utilisée (firmware, NVS, réseau, serveur distant),
     - durée et conditions particulières (offline, stress, reboot, etc.).
   - Si un script PowerShell dédié est mentionné, le lire pour comprendre exactement :
     - la commande PlatformIO utilisée (flash, monitor, tests),
     - les options (baud, filters, timeouts),
     - les étapes automatisées (ex. extraction de logs après le test).

3. **Analyser les logs d’exécution**
   - Parcourir les derniers `*.log.errors` / logs extraits en cherchant en priorité :
     - erreurs explicites (`ERROR`, `ASSERT`, `panic`, `Guru Meditation`, `WDT`, `timeout`, codes HTTP 4xx/5xx, erreurs WiFi),
     - motifs répétés (même message d’erreur, même tâche bloquée, mêmes timeouts),
     - gaps temporels anormaux (longues pauses, absence de logs pendant plusieurs secondes/minutes),
     - patterns autour du moment du crash / freeze (quel module logge juste avant/juste après).
   - Garder une liste structurée des **symptômes observés** avec :
     - horodatage relatif (si présent),
     - module ou tag de log (ex. `[WIFI]`, `[NET]`, `[AUTOM]`),
     - message brut.

4. **Croiser les symptômes avec le code**
   - Pour chaque message de log ou motif suspect :
     - rechercher dans le code la source du log (avec `Grep`/`SemanticSearch`),
     - identifier le module / la fonction / la tâche FreeRTOS à l’origine,
     - vérifier les points sensibles :
       - timeouts réseau et respect des règles offline-first,
       - boucles bloquantes ou `vTaskDelay`/`delay` trop longs,
       - accès NVS / mémoire fréquents,
       - transitions d’états de l’automatisme,
       - gestion du watchdog.
   - Se référer aux autres skills FFP5CS pertinentes :
     - `ffp5cs-esp32-offline-first` et `ffp5cs-offline-network-agent` pour tout ce qui touche au réseau,
     - `ffp5cs-memory-watchdog` pour les tâches et buffers,
     - `ffp5cs-simplification-refactor` pour éviter la sur‑ingénierie dans les corrections envisagées.

5. **Produire une liste de points suspects**
   - Regrouper les observations en **pistes de bug** avec structure claire, par exemple :
     - **Module / tâche** : nom de la tâche ou du fichier principal concerné.
     - **Symptômes observés** : extrait de logs + fréquence / moment.
     - **Hypothèse** : ce qui pourrait se passer (blocage sur attente réseau, erreur de capteur, gestion NVS, etc.).
     - **Vérifications proposées** : tests ciblés à effectuer, logs supplémentaires à ajouter, mesures à capturer.
     - **Impact potentiel** : mineur / critique (crash, perte de nourrissage, risque sur écosystème).
   - Donner une **priorisation** (ex. P1/P2/P3) en fonction :
     - de la gravité (crash / freeze / risque matériel),
     - de la récurrence dans les logs.

6. **Limiter les modifications de code**
   - Par défaut, rester en **lecture seule** :
     - analyser,
     - pointer les sections de code suspectes,
     - proposer des améliorations ou des ajouts de logs.
   - N’appliquer des modifications de code que si :
     - l’utilisateur le demande explicitement, ou
     - une correction est **évidente, locale et sans refactor massif** (ex. ajouter un timeout manquant, corriger une condition, ajouter un log clé).
   - Éviter :
     - les refactors structurants,
     - la création de nouveaux modules lourds,
     - toute modification qui va au-delà de la résolution du bug observé.

## Format recommandé pour les sorties de l’agent

Lorsqu’un diagnostic est rendu à l’utilisateur, structurer la réponse en sections courtes, par exemple :

1. **Contexte**
   - fichiers analysés (logs, rapports, scripts),
   - type de test (durée, conditions particulières).

2. **Symptômes clés observés dans les logs**
   - liste courte (3–7 points max) avec extraits de messages si utile.

3. **Pistes de bug prioritaires**
   - pour chaque piste :
     - module / fichier / tâche concernés,
     - hypothèse principale,
     - vérifications/tests recommandés.

4. **Propositions de prochaines étapes**
   - ajouts de logs ciblés,
   - scénarios de tests complémentaires,
   - éventuelles micro‑corrections à envisager (sans entrer dans le refactor massif).

