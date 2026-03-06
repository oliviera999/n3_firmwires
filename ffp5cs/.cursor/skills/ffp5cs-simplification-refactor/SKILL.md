---
name: ffp5cs-simplification-refactor
description: Empêche la sur‑ingénierie et pousse à fusionner/supprimer du code. À utiliser quand on touche à la structure du projet (src/, include/), qu’on ajoute des fichiers ou des modules, ou qu’on refactorise.
---

# FFP5CS – Simplification et refactor

## Quand appliquer cette skill

Utiliser ces règles dès que le code ou la structure du projet est modifié pour :

- **Structure** : réorganiser `src/`, `include/`, sous-dossiers, namespaces.
- **Ajout de fichiers** : nouveaux `.cpp` / `.h`, nouveaux modules ou dossiers.
- **Refactor** : extraire du code dans un nouveau fichier, introduire des couches d’abstraction, des patterns génériques.

Toujours combiner cette skill avec les règles cœur de projet (`FFP5CS-R-gles-c-ur-de-projet.mdc`).

## Objectifs

- **Éviter la sur‑ingénierie** : pas de patterns “enterprise”, pas de DI complexe, pas d’abstractions “au cas où”.
- **Privilégier fusion et suppression** : avant tout ajout, se demander si on peut fusionner, simplifier ou supprimer du code existant.
- **Garder une structure simple** : un fichier par fonctionnalité majeure ; pas de nouveaux modules sans suppression d’équivalents.

## Règles de structure

1. **Un fichier par fonctionnalité majeure**
   - Une fonctionnalité claire = un couple `.cpp` / `.h` (ou un seul fichier si trivial).
   - Ne pas multiplier les petits fichiers “utils”, “helpers”, “common” sans nécessité réelle.
   - Éviter les sous-dossiers profonds et les micro-modules (ex. `automatism/alert/`, `automatism/display/`…) sauf si la fonctionnalité est vraiment volumineuse et autonome.

2. **Pas de nouveau module sans suppression d’équivalent**
   - Avant d’ajouter un fichier ou un dossier : **existe-t-il déjà du code qui fait la même chose ?**
   - Si oui : **fusionner** dans l’existant ou **supprimer** l’ancien avant d’ajouter le nouveau.
   - Règle pratique : **tout ajout de module doit s’accompagner d’une suppression** (fusion ou suppression de code mort).

3. **Pas de patterns enterprise**
   - **Interdit** : injection de dépendances (DI) complexe, factories génériques, couches d’abstraction “au cas où”, services à tout faire.
   - **Préférer** : fonctions simples, structures de données minimales, appels directs.
   - Si une abstraction est proposée : **est-elle indispensable *maintenant* ?** Sinon, ne pas l’ajouter.

4. **Pas d’abstractions “au cas où”**
   - Ne pas créer d’interfaces, de wrappers ou de modules “pour plus tard” ou “pour permettre une évolution”.
   - Ajouter de la complexité **uniquement** pour un besoin actuel, explicite et vérifiable.

## Workflow avant tout ajout

Avant d’ajouter un fichier, un module ou une couche d’abstraction :

1. **Question obligatoire** : *« Peut-on fusionner / simplifier / supprimer ? »*
   - Y a-t-il du code existant qui couvre déjà ce besoin ?
   - Peut-on étendre un fichier existant au lieu d’en créer un nouveau ?
   - Du code mort ou redondant peut-il être supprimé en même temps ?

2. **Vérifier les équivalents**
   - Rechercher dans `src/` et `include/` les fichiers ou fonctions qui pourraient faire doublon.
   - Si doublon : **fusionner ou supprimer** avant d’ajouter quoi que ce soit.

3. **Justifier l’ajout**
   - Un nouveau fichier doit correspondre à **une fonctionnalité majeure** bien identifiée.
   - Un nouveau module (dossier) doit être **indispensable** et ne pas pouvoir vivre dans un fichier existant.

4. **Contrôle final**
   - Aucun nouveau pattern enterprise (DI, factories, couches génériques).
   - Aucune abstraction “au cas où”.
   - Tout ajout compensé par une fusion ou une suppression quand c’est possible.

## Rappel en début de tâche

Lors de modifications de structure ou d’ajout de fichiers :

- **1 fichier par fonctionnalité majeure** ; pas de micro-modules inutiles.
- **Avant tout ajout** : « Peut-on fusionner / simplifier / supprimer ? »
- **Pas de patterns enterprise** : pas de DI complexe, pas d’abstractions “au cas où”.
