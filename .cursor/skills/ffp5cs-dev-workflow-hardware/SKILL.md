---
name: ffp5cs-dev-workflow-hardware
description: Enforce a hardware-oriented validation checklist for the FFP5CS ESP32 firmware before large changes, commits to main, release tags, or deployment to real aquaponics hardware, focusing on warning-free builds, tests on a real ESP32 (offline, sensor failure, network down), memory/watchdog verification, and 24/7 behaviour using existing monitoring scripts.
---

# FFP5CS - Workflow validation hardware

## Objectif du skill

Ce skill encadre une **mini check-list orientée hardware réel** pour le projet FFP5CS.

L’agent doit appliquer ce workflow **avant** :
- **gros changements de firmware** (refactor majeur, nouvelle fonctionnalité critique, changement de logique d’automatisme),
- **commit sur `main`** ou merge important,
- **création de tag de version / release**,
- **flash sur un système de production ou proche-production** (bac réel).

L’objectif est de réduire au maximum le risque de régression sur le hardware réel, en respectant les règles cœur de projet (offline-first, simplicité, robustesse, watchdog/mémoire).

---

## Workflow global

Quand ce skill est utilisé, l’agent doit guider l’utilisateur à travers les étapes suivantes :

1. **Compilation sans warnings**
2. **Tests sur ESP32 réel en conditions dégradées**
3. **Vérification mémoire et watchdog**
4. **Vérification comportement longue durée (24/7) via scripts de monitoring**
5. **Synthèse rapide et décision “OK pour commit/tag” ou “À corriger avant”**

L’agent peut adapter le niveau de détail selon le contexte, mais **ne doit pas sauter une étape** sans justification explicite.

---

## 1. Compilation sans warnings

- **But**: s’assurer que le firmware compile proprement, sans warnings ni erreurs, pour la/les cible(s) PlatformIO configurées.

- **Instruction à l’agent** :
  - Demander/rapeller à l’utilisateur de lancer une compilation PlatformIO (ex. tâche intégrée ou commande CLI).
  - Vérifier, dans la mesure des informations disponibles (logs, fichiers de sortie, messages fournis par l’utilisateur), que:
    - **aucun warning** de compilation n’est présent, ou
    - à défaut, que les warnings restants sont **explicitement justifiés** (commentaire dans le code ou dans un document de suivi).
  - Si des warnings visibles sont liés à une modification récente, proposer des corrections de code pour les faire disparaître.

- **Point de sortie** :
  - Si compilation échoue ou contient des warnings non compris/non justifiés → **bloquer la suite du workflow** et proposer des corrections.

---

## 2. Tests sur ESP32 réel (offline, capteur HS, réseau down)

- **But**: vérifier le comportement du firmware sur un ESP32 physique, dans les scénarios critiques définis par les règles du projet :
  - fonctionnement **offline-first** (sans réseau),
  - capteur en panne / retour invalide,
  - réseau indisponible ou serveur distant down.

- **Instruction à l’agent** :
  - Rappeler que les tests doivent être faits **sur un ESP32 réel**, pas en simple simulation.
  - Guider l’utilisateur pour:
    - **Mode offline** :
      - Demander à l’utilisateur de déconnecter le WiFi ou d’empêcher l’ESP32 d’accéder au réseau.
      - Vérifier (via logs ou retour utilisateur) que:
        - le firmware **continue à fonctionner localement** (automatisme, horaires, seuils depuis NVS),
        - aucune boucle de retry réseau agressive ne bloque le système,
        - le watchdog ne se déclenche pas à cause du réseau.
    - **Capteur HS / valeurs invalides** :
      - Demander à l’utilisateur de simuler un capteur en panne (débrancher, court-circuit contrôlé, valeur hors plage, etc.) selon ce qui est le plus simple/acceptable.
      - Vérifier que:
        - les lectures de capteurs sont **validées** (plage, `isnan`, etc.),
        - une **valeur par défaut sûre** est utilisée,
        - le système ne crash pas et reste cohérent (pas de NaN qui propage, pas d’overflow).
    - **Réseau down / serveur distant indisponible** :
      - Demander à l’utilisateur de rendre le serveur distant injoignable (pare-feu, adresse invalide, service éteint, etc.).
      - Vérifier que:
        - le firmware ne se bloque pas sur les appels réseau,
        - les timeouts sont raisonnables (≲ 5s),
        - les erreurs réseau sont gérées avec **fallback local** sur la NVS.

- **Point de sortie** :
  - En cas de comportement anormal (crash, blocage, boucle infinie de retry, valeurs absurdes) → demander à l’agent de:
    - analyser les morceaux de code concernés,
    - proposer des correctifs concrets,
    - **recommander de ne pas tagger/merger** tant que ce n’est pas corrigé.

---

## 3. Vérification mémoire et watchdog

- **But**: s’assurer que les changements récents ne dégradent pas significativement:
  - l’utilisation mémoire (heap, fragmentation),
  - la stabilité face au watchdog,
  - les allocations dynamiques dans les chemins critiques.

- **Instruction à l’agent** :
  - Se baser sur les règles projet et, si disponibles, sur:
    - les fichiers `diagnostics`, `memory`, `task_monitor`, etc. du projet,
    - les scripts d’analyse ou d’export de logs existants.
  - Vérifier dans les modifications récentes:
    - qu’il n’y a pas de nouvelles **allocations dynamiques dans des boucles chaudes**,
    - qu’aucun usage abusif de `String` Arduino n’a été ajouté dans des callbacks fréquents,
    - que les buffers sont statiques quand c’est possible.
  - Si l’utilisateur fournit des logs de diagnostic:
    - Les analyser pour détecter:
      - fuites apparentes (heap qui diminue régulièrement),
      - redémarrages inattendus ou resets watchdog,
      - tâches qui dépassent systématiquement leur budget temps.
  - Au besoin, proposer:
    - de simplifier certaines structures,
    - de regrouper les buffers,
    - d’ajouter un monitoring plus explicite (compteurs, logs synthétiques).

- **Point de sortie** :
  - Si des signes de fuite mémoire, fragmentation ou resets watchdog sont visibles → **recommander de corriger avant commit/tag** et suggérer des refactors ciblés.

---

## 4. Vérification 24/7 via scripts de monitoring

- **But**: vérifier que le firmware se comporte correctement en mode “24/7”, au moins sur une période significative (par exemple plusieurs heures), en s’appuyant sur les **scripts de monitoring déjà présents dans le repo** (ex.: scripts PowerShell de monitoring, extraction de logs, analyse automatique).

- **Instruction à l’agent** :
  - Pour un **workflow complet** (erase flash, flash firmware + LittleFS, monitor 5 min, analyse du log), utiliser **préférentiellement** le script racine `erase_flash_fs_monitor_5min_analyze.ps1` (aligné avec les règles projet).
  - Identifier les scripts de monitoring pertinents dans le projet (par nom ou description, ex.: scripts contenant `monitor`, `diagnostic`, etc.).
  - Guider l’utilisateur pour:
    - lancer un ou plusieurs scripts de monitoring (durée raisonnable, par ex. 5–15 minutes pour un check rapide, ou plus si le contexte l’exige),
    - récupérer les fichiers de sortie (logs, rapports `.txt` ou `.md`, etc.),
    - fournir au besoin les extraits les plus représentatifs.
  - Analyser ces résultats pour:
    - détecter des erreurs récurrentes,
    - repérer des patterns de redémarrage ou de crash,
    - vérifier que les tâches critiques tournent normalement dans le temps.
  - Si certains scripts fournissent déjà une synthèse ou un score, en tenir compte dans la conclusion.

- **Point de sortie** :
  - Si les logs montrent une instabilité, des erreurs répétées ou un comportement non conforme aux règles du projet → **conclure que la version ne doit pas être taggée/mergée** tant que ces problèmes ne sont pas adressés.

---

## 5. Synthèse et recommandation

À la fin du workflow, l’agent doit produire une **synthèse courte** avec :

- **Rappel de la version / branche** évaluée (si connu).
- **Statut par étape** :
  - Compilation sans warnings: OK / KO / À vérifier
  - Tests ESP32 réel (offline/capteur HS/réseau down): OK / KO / Partiel
  - Mémoire & watchdog: OK / KO / Incertain
  - Monitoring 24/7: OK / KO / Non réalisé
- **Recommandation globale** :
  - “**OK pour commit/tag**” si toutes les étapes critiques sont au vert ou disposant de justifications acceptables,
  - ou “**À corriger avant commit/tag**” avec une courte liste d’actions recommandées (bugfix, simplifications, retest).

La synthèse doit rester **très concise** (quelques lignes) pour rester compatible avec les autres contextes de l’agent.

---

## Exemple de check-list à utiliser

L’agent peut utiliser/adapter la check-list suivante quand il accompagne l’utilisateur :

```markdown
# Check-list hardware FFP5CS avant commit/tag

- [ ] Compilation PlatformIO sans warnings bloquants
- [ ] Test sur ESP32 réel en mode offline (sans réseau) OK
- [ ] Test capteur HS / valeurs invalides → comportement sûr, pas de crash
- [ ] Test réseau/serveur distant down → timeouts raisonnables, fallback NVS OK
- [ ] Revue rapide mémoire & watchdog (pas de nouvelles allocations dynamiques critiques, pas d’usage abusif de String)
- [ ] Monitoring via scripts existants (logs stables, pas de reset répété)
- [ ] Synthèse et décision : OK pour commit/tag ? (oui/non)
```

