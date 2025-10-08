# Optimisations de l'affichage OLED - FFP3CS

## Problème identifié

L'affichage OLED pouvait se figer, des artefacts pouvaient apparaître lors des superpositions (diagnostics/OTA vs écran principal), et des flushs inutiles consommaient des ressources.

## Solutions implémentées

### 1. Session de mise à jour optimisée

- `beginUpdate()` / `endUpdate()` pour grouper les dessins et flusher une seule fois.
- `setUpdateMode(bool immediate)` pour contrôler le flush immédiat (par défaut true pour compatibilité).
- `needsRefresh()` pour savoir s'il faut rafraîchir.
- `endUpdate()` ne flushe plus que si un rendu a modifié l'écran (`_needsFlush`).

Avantages:
- Réduction des flushs redondants
- Économie CPU/latences

### 2. Cache intelligent des états

- `drawStatus()` ne redessine que si les valeurs changent (send/recv/RSSI/mail/tide/diff).
- `resetStatusCache()` et `resetMainCache()` forcent un redessin propre lors des bascules d'écran.

### 3. Gestion des superpositions via verrouillage

- `lockScreen(ms)` introduit un verrou temporel pour réserver l'écran à des vues dédiées (diagnostic, OTA, raisons du sleep).
- `isLocked()` auto-expire le verrou dès la fin, évitant les états collants.
- Pendant un verrou, les rendus de fond (`showMain`, `showVariables`, `showServerVars`, `drawStatus`) s'auto-annulent pour éviter toute superposition.

Nouvelle API dédiée:
- `showSleepReason(cause, detail1, detail2, lockMs, mailBlink)`
  - Efface l'écran, affiche un en‑tête « Light-sleep », une cause explicite et jusqu'à 2 détails.
  - Verrouille l'écran pendant `lockMs`.
  - Affiche la barre de statut et peut forcer l'icône enveloppe (si mail en cours).
- `drawStatusEx(..., force)`
  - Variante de `drawStatus` permettant de forcer le dessin de la barre de statut même sous verrou (utilisé par `showSleepReason`).

### 4. UI OTA enrichie

- `showOtaProgressEx(percent, fromLabel, toLabel, phase, currentVersion, newVersion, hostLabel)` :
  - Affiche la phase, l'hôte (SSID/AP), la version courante et la nouvelle, les partitions From/To, une barre de progression et le pourcentage.
  - Utilisée dans les callbacks OTA (statut, erreur, progression) et à 100% avant reboot.

### 5. Clignotement enveloppe (emails)

- L'icône enveloppe (bas gauche) réapparaît et clignote pendant l'envoi des emails (piloté par `armMailBlink()` et un timeout interne).
- Visible également sur l'écran de raison de sleep si un email est en cours.

## Utilisation

### Mode automatique (recommandé)
```cpp
// Mise à jour standard
_disp.showMain(...);
_disp.drawStatus(sendState, recvState, rssi, /*mailBlink*/ false, tideDir, diff);
```

### Mode groupé
```cpp
_disp.beginUpdate();
_disp.showMain(...);
_disp.drawStatus(...);
_disp.endUpdate(); // flush si nécessaire
```

### Écran de light-sleep
```cpp
bool blink = /* mail en cours ? */;
_disp.showSleepReason("Cause: inactivite", "Inactivite depuis 300s", "Maree diff:0", 2500, blink);
```

### OTA enrichi
```cpp
_disp.showOtaProgressEx(progress, fromLbl, toLbl, phase, Config::VERSION, remoteVersion.c_str(), host.c_str());
```

## Impact sur les performances

- Flushs évités quand aucun dessin effectif: baisse de charge et de la consommation.
- Superpositions éliminées grâce au verrou et à l'auto-expiration.
- Barre de statut forçable ponctuellement pour afficher l'enveloppe même sous verrou.

## Bonnes pratiques

- Utiliser `beginUpdate()/endUpdate()` autour des rendus multiples.
- `resetStatusCache()/resetMainCache()` après un écran dédié pour repartir propre.
- Ne pas appeler les vues de fond pendant un verrou, préférer l'écran dédié (`showSleepReason`).

## Version

Ces optimisations et APIs sont incluses à partir de la version **9.98+**. 