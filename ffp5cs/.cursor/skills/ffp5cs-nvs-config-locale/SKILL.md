---
name: ffp5cs-nvs-config-locale
description: S’assure que la NVS reste la source de vérité cohérente et peu usée pour la configuration locale dans FFP5CS. À utiliser lors de modifications de nvs_manager, config, bootstrap_storage ou des web_routes_* qui lisent/écrivent la configuration.
---

# FFP5CS – NVS et configuration locale

## Quand utiliser cette skill

Appliquer cette skill lorsque vous :

- Modifiez `nvs_manager` ou tout code qui lit/écrit directement en NVS.
- Modifiez `config` (fichiers `config.*`, structures de configuration ou valeurs par défaut).
- Touchez à `bootstrap_storage` ou à l’initialisation de la configuration persistée.
- Modifiez des `web_routes_*` qui exposent ou écrivent la configuration (endpoints HTTP ou WebSocket).

Par défaut, utilisez un mode **explore** (audit du code et cartographie) ; basculez en **generalPurpose** si vous devez proposer un refactor/unification des clés ou du code d’accès NVS.

## Objectifs principaux

1. **NVS comme source de vérité cohérente**
   - La configuration effective (horaires, seuils, durées, états) doit toujours provenir de la NVS si elle est disponible.
   - Le serveur distant ne sert qu’à **mettre à jour** cette NVS, jamais à piloter directement l’automatisme.
   - Toute configuration reçue du serveur et jugée valide doit être **persistée en NVS** avant d’être utilisée comme base de décision.

2. **Limiter l’usure de la flash**
   - Ne jamais écrire en NVS en boucle ou à haute fréquence.
   - Écrire en NVS **uniquement si la valeur change réellement** (comparaison avec la valeur déjà stockée ou en cache).
   - Regrouper les écritures (batch) quand c’est raisonnable, plutôt que plusieurs `set`/`commit` dispersés.

3. **Cohérence des clés et namespaces**
   - Garder une **cartographie claire** des namespaces et clés utilisés pour la configuration.
   - Éviter les doublons ou clés quasi-identiques qui stockent le même concept sous des noms différents.
   - Préférer des noms de clés explicites et stables (par ex. `schedule_morning_start`, `water_temp_min`) plutôt que des abréviations opaques.

## Workflow d’audit (mode explore)

Lorsque vous analysez une modification ou un bug lié à la config :

1. **Cartographier les accès NVS**
   - Rechercher dans le projet :
     - les appels à l’API NVS (open, get, set, commit, erase),
     - les fonctions utilitaires dans `nvs_manager` (par ex. lecture/écriture de structures de config),
     - les conversions entre types (bool, int, float, string) pour la même clé.
   - Noter pour chaque namespace :
     - quelles clés existent,
     - où elles sont lues,
     - où elles sont écrites/modifiées.

2. **Vérifier la logique “write-only-if-changed”**
   - Pour chaque écriture en NVS :
     - Identifier la valeur actuelle (en cache ou relue avant écriture).
     - Vérifier qu’une comparaison est faite avant d’écrire.
     - Si ce n’est pas le cas, proposer :
       - soit de tester l’égalité avant `set`/`commit`,
       - soit d’utiliser une fonction utilitaire centralisée qui encapsule ce test.

3. **Suivre le trajet de la configuration serveur → firmware**
   - Identifier les endpoints `web_routes_*` ou websockets qui reçoivent de la configuration.
   - Vérifier que :
     - la configuration reçue est **validée** (types, plages, cohérence métier),
     - en cas de succès, les valeurs sont **persistées en NVS**,
     - l’automatisme et les tâches de fond lisent ensuite ces valeurs **depuis la NVS** (ou un cache issu de la NVS), pas directement depuis la requête réseau.
   - Confirmer qu’en **mode offline** (serveur non accessible), le firmware continue de fonctionner avec :
     - la dernière configuration connue en NVS,
     - ou des valeurs par défaut sûres si la NVS est vide ou corrompue.

4. **Contrôler la cohérence des valeurs par défaut**
   - Vérifier que les valeurs par défaut (dans `config.*` ou ailleurs) sont :
     - raisonnables et sûres pour les poissons/plantes,
     - cohérentes avec ce qui est écrit initialement en NVS lors du premier boot / factory reset.
   - S’assurer qu’on ne mélange pas plusieurs jeux de valeurs par défaut contradictoires.

## Workflow de refactor/unification (mode generalPurpose)

Quand le code d’accès NVS ou des clés de config est éparpillé ou incohérent :

1. **Identifier un point central**
   - Proposer de centraliser les accès NVS dans un petit ensemble de fonctions (`nvs_manager`, helpers dédiés).
   - Limiter le nombre de namespaces/logiques différentes pour la configuration de l’automatisme.

2. **Unifier les noms de clés**
   - Si plusieurs clés représentent le même concept, proposer une clé unique et un plan de migration simple (lecture ancienne clé, écriture sous la nouvelle, puis nettoyage progressif).
   - Documenter (dans les commentaires ou un mapping simple) la signification de chaque clé sensible.

3. **Encapsuler la logique “NVS source de vérité”**
   - Préférer des fonctions de haut niveau du style :
     - `loadConfigFromNvsOrDefaults(...)`,
     - `updateConfigFromServerAndPersist(...)`,
     - `writeConfigIfChanged(...)`.
   - Ces fonctions doivent :
     - relire les valeurs courantes,
     - appliquer les validations,
     - n’écrire que si nécessaire,
     - toujours laisser le système avec un état cohérent en NVS.

4. **Vérification finale**
   - Après tout refactor lié à la configuration/NVS, vérifier mentalement ou par tests que :
     - le firmware démarre correctement en **offline complet**,
     - la configuration reste cohérente après plusieurs cycles de mise à jour par le serveur,
     - aucune boucle ou tâche ne spamme la NVS avec des écritures répétées.

