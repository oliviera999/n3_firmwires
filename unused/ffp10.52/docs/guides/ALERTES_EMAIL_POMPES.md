## Alertes email pour les pompes (aquarium et réserve)

Cette page décrit le contenu des emails, les sujets, les raisons d'arrêt et l'origine des commandes manuelles suite aux dernières évolutions logicielles.

### Vue d'ensemble

- Des emails sont envoyés à chaque changement d'état des pompes (ON/OFF) si les notifications sont activées.
- Les emails incluent maintenant la raison de l'arrêt et, en cas d'action manuelle, l'origine de la commande: «Serveur distant» (BDD) ou «Serveur local» (serveur web embarqué).

### Pompe de réserve (remplissage)

Sujets possibles:
- «Pompe réservoir MANUELLE ON» / «Pompe réservoir MANUELLE OFF»
- «Pompe réservoir AUTO ON» / «Pompe réservoir AUTO OFF»
- «Pompe réservoir OFF (sécurité réserve basse)»
- «Pompe réservoir OFF (sécurité aquarium trop plein)»
- «Pompe réservoir OFF (sécurité)» (cas générique)

Corps de message (extraits):
- Démarrage: indique le mode (manuel/auto), niveaux Aqua/Réserve, seuils et durée max configurée.
- Arrêt: ajoute «Raison d'arrêt: …» parmi:
  - «Durée maximale atteinte» (cycle terminé)
  - «Arrêt manuel»
  - «Sécurité réserve basse» (protection anti-marche à sec)
  - «Sécurité aquarium trop plein»
  - «Inconnue» (rare)
- Si l'action est manuelle, ajoute: «Origine: Serveur distant» (commande BDD) ou «Serveur local» (interface web embarquée).
- En cas de verrouillage de sécurité, le message inclut «Raison verrouillage: …» et des instructions de déblocage adaptées:
  - Réserve basse: «Déblocage automatique quand la distance réservoir < (seuil-5) cm (confirmé)».
  - Aquarium trop plein: «Déblocage automatique quand l'aquarium n'est plus en trop-plein (stabilité).»
  - Inefficacité: conseils d'amorçage et vérification tuyaux.

Détection des sécurités:
- Réserve basse: si la distance mesurée au capteur réservoir dépasse le seuil `tankThreshold` (réserve trop éloignée → faible niveau), la pompe est verrouillée pour éviter une marche à sec.
- Aquarium trop plein: si la distance mesurée au capteur aquarium est inférieure à `limFlood`, la pompe est verrouillée pour éviter un débordement côté aquarium.

Origine de la commande manuelle (réserve):
- Serveur distant (BDD): commandes GPIO reçues depuis le serveur de contrôle.
- Serveur local: actions via l'endpoint web local `?relay=pumpTank` (basculé sur `start/stopTankPumpManual()`).

### Pompe aquarium

Sujets possibles:
- «Pompe aquarium ON» / «Pompe aquarium OFF»

Corps de message (extraits):
- Démarrage: message simple, ajoute «Origine: …» si déclenché manuellement.
- Arrêt: «Raison d'arrêt: …» parmi:
  - «Arrêt manuel» (inclut l’origine: Serveur distant/Serveur local)
  - «Mise en veille» (arrêt initié par la logique de sleep)
  - «Inconnue»

Origine de la commande manuelle (aquarium):
- Serveur distant (BDD): commandes GPIO reçues.
- Serveur local: actions via l'endpoint web local `?relay=pumpAqua`, désormais routées par `startAquaPumpManualLocal()` / `stopAquaPumpManualLocal()` pour tracer l’origine.

### Configuration et paramètres

- Email activé: `mailNotif` (case à cocher) et destinataire `mail` côté configuration distante.
- Seuils utilisés:
  - `aqThreshold`: seuil de déclenchement remplissage aquarium
  - `tankThreshold`: seuil de sécurité réserve (distance)
  - `limFlood`: seuil de protection «aquarium trop plein» (distance)
- Durée max de remplissage: `refillDuration` (secondes)

### Journalisation

- Les changements d’état sont tracés sur le port série avec les niveaux (Aqua/Réserve), les seuils et les raisons de verrouillage.
- Les événements importants sont ajoutés dans l’`EventLog`.

### Compatibilité

- Les nouveaux motifs de verrouillage «Réserve trop basse» et «Aquarium trop plein» complètent les cas existants. Aucune action requise côté serveur; les sujets/phrases des emails sont explicites.


