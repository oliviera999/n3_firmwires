---
name: ffp5cs-web-api-local
description: Encadre la conception et l’évolution de l’API web locale (HTTP + WebSocket) du projet FFP5CS pour la garder simple, stable et cohérente avec l’UI embarquée. À utiliser lors de modifications de web_server, web_routes_status, web_routes_ui ou des fichiers front web embarqués (data/web/*).
---

# FFP5CS – API Web locale

## Quand appliquer cette skill

Appliquer ces règles quand tu :

- modifies `web_server`, `web_routes_status`, `web_routes_ui` ou tout autre fichier lié aux routes HTTP/WebSocket locales ;
- touches aux fichiers front embarqués (HTML/JS/CSS dans `data/web/*`) qui consomment l’API locale ;
- ajoutes/modifies des endpoints HTTP locaux ou le protocole WebSocket ;
- fais évoluer le format JSON échangé avec l’UI locale.

Toujours combiner cette skill avec :
- les règles cœur de projet (`FFP5CS-R-gles-c-ur-de-projet.mdc`) ;
- la skill `ffp5cs-esp32-offline-first` pour tout ce qui touche au réseau.

## Endpoints canoniques – ne pas casser le contrat

L’API locale doit rester **prévisible et stable**. Les endpoints suivants sont **canoniques** :

- `GET /` : sert l’UI principale (page web locale).
- `GET /api/status` : renvoie l’état courant (capteurs, automatismes, erreurs, etc.).
- `POST /api/config` : reçoit la configuration (horaires, seuils, paramètres divers) depuis l’UI.
- `POST /api/feed` : déclenche un nourrissage manuel ou une action ponctuelle.
- `GET /ws` : WebSocket temps réel (état, événements, notifications UI).

Règles :

- Ne **supprime** ni ne **renomme** ces endpoints sans raison forte.
- Si tu dois changer la forme du JSON, garder au maximum la **compatibilité ascendante** (ajouter des champs plutôt qu’en supprimer/renommer).
- Documenter dans le code (commentaires courts) ce que chaque endpoint renvoie/reçoit.

## JSON minimaliste et stable

La structure JSON doit rester **simple**, sans surmodélisation :

- Utiliser des objets plats ou faiblement imbriqués (1–2 niveaux max).
- Préférer les types simples : booléens, nombres, chaînes, tableaux simples.
- Éviter d’exposer directement des structures C++ complexes ou des états internes trop fins.
- Ne pas multiplier les états redondants : si une info peut se déduire d’une autre, la calculer côté UI plutôt que dupliquer.

Rappels :

- Toute nouvelle clé JSON doit avoir un **nom clair et stable** (en anglais si possible, snake_case ou camelCase cohérent avec l’existant).
- Toujours prévoir une **valeur par défaut sûre** si le champ n’est pas présent dans la réponse/entrée (côté firmware comme côté UI).

## WebSocket `/ws` – taille de messages et gestion des connexions

Le canal WebSocket doit rester **léger** et **robuste** :

- **Taille max des messages** :
  - Garder les payloads WebSocket aussi petits que raisonnable.
  - Éviter d’envoyer des dumps complets de config ou des logs longs par WS.
  - Si un gros contenu est nécessaire, préférer une requête HTTP ponctuelle (GET/POST) plutôt que WS.
- **Fréquence d’envoi** :
  - Ne pas spammer l’UI avec des messages trop fréquents.
  - Grouper les mises à jour quand c’est possible (par ex. état agrégé toutes les X secondes ou sur changement significatif).

### Gestion de `ws.count()` / connexions simultanées

Lors de l’envoi côté firmware :

- Toujours vérifier qu’il y a au moins un client connecté avant d’émettre :
  - si l’API WebSocket fournit `ws.count()` (ou équivalent), ne rien envoyer si `count == 0` ;
  - éviter les boucles d’envoi coûteuses quand il n’y a aucun client.
- Dans les callbacks WebSocket, ne jamais supposer qu’un client reste connecté longtemps :
  - gérer proprement les déconnexions ;
  - éviter de garder des pointeurs/références vers des connexions au-delà de la durée de vie prévue par la lib WS.

## UI locale indépendante du serveur distant

La **UI locale doit fonctionner même sans serveur distant** :

- La page servie sur `GET /` doit s’afficher et être utilisable **offline** (tant que l’ESP32 est joignable en IP locale).
- Les actions de base (lecture d’état via `/api/status`, mise à jour de config via `/api/config`, déclenchement `/api/feed`) doivent fonctionner **sans** appel au serveur distant.
- Ne jamais rendre un écran ou une action de l’UI locale **bloquée** parce que le serveur distant ne répond pas.

Conséquences pratiques :

- Le front ne doit pas dépendre de domaines externes pour fonctionner (pas de dépendance critique à un JS/CSS hébergé à l’extérieur).
- Si certaines données viennent habituellement du serveur distant, prévoir :
  - un **fallback local** (valeurs de NVS, dernières valeurs connues),
  - un message d’erreur simple dans l’UI, sans casser tout le flux.

## Workflow recommandé lors d’une modification d’API locale

Avant de finaliser une modification sur l’API web locale :

1. **Vérifier le contrat des endpoints**
   - As-tu modifié `/`, `/api/status`, `/api/config`, `/api/feed` ou `/ws` ?
   - Si oui, le front existant continue-t-il à fonctionner avec des valeurs par défaut raisonnables ?
2. **Relire le JSON**
   - Le JSON reste-t-il simple, lisible et stable ?
   - As-tu évité les structures trop profondes ou verbeuses ?
3. **Contrôler WebSocket**
   - Les messages sont-ils de taille raisonnable ?
   - Tu utilises bien un check de type `ws.count()` (ou équivalent) avant d’envoyer ?
4. **Tester le mode offline**
   - Est-ce que l’UI locale reste utilisable si le serveur distant est coupé (ou non joignable) ?
   - Les erreurs réseau côté distant ne bloquent-elles pas l’API locale ?

Si un de ces points n’est pas respecté, simplifier le design avant d’ajouter de la complexité.

