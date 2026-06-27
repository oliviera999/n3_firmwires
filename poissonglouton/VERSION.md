# Poissonglouton - Historique versions

## 0.4.1 - 2026-06-26

- **Affichage de l'état PIR** sur l'écran LVGL, en miroir de l'indicateur IR :
  une pastille `PIR` dans le header et une ligne `PIR: absent/libre/mouvement`
  (couleur : orange si absent, vert si mouvement, neutre au repos) dans la carte
  « Capteurs ». Nouvelle méthode `PglDisplay::setPirStatus(pirPresent, pirMotion)`,
  alimentée chaque tour via `PglDetection::readPirMotion()` (lecture du niveau PIR
  actif-HAUT). Les widgets PIR ne sont créés **que si `PGL_ENABLE_PIR`** (guards
  dans `pgl_ui`) : aucun changement d'UI sur les boards sans PIR.
- Validation : `pgl-s3-headless` (défaut + `-DPGL_ENABLE_PIR=1`) compilés ; suites
  natives inchangées (vertes). Le rendu LVGL `pgl-s3-display` est couvert par la CI.

## 0.4.0 - 2026-06-26

Ajout d'un 3ᵉ capteur **PIR** (détecteur de mouvement, sortie numérique active-HAUT)
aux deux existants (IR obstacle GPIO7, US ultrason GPIO6) + **stratégie de fusion**
robuste et testable.

- **Rôles capteurs** : IR et US = capteurs de **comptage** (précis, créneau bouteille) ;
  PIR = capteur de **présence** (quelqu'un à la machine), pas un compteur direct par défaut.
- **Anti-double-comptage** : l'anti-rebond global unique (`PGL_DEBOUNCE_MS`) reste appliqué
  en amont dans `poll()`, avant toute décision — un passage de bouteille = un seul comptage
  même si plusieurs capteurs voient le même objet.
- **Fonction de fusion pure** (`pgl_sensor_fusion.{h,cpp}`, sans dépendance Arduino, testable
  en natif) : décide du comptage à partir des triggers **déjà débouncés** de chaque capteur.
  Corroboration (`corroborated`) = ≥2 capteurs concordants (IR+US dans la fenêtre
  `PGL_SENSOR_CORROBORATION_WINDOW_MS`, ou présence PIR confirmant un comptage IR/US dans
  `PGL_PIR_PRESENCE_WINDOW_MS`).
- **Robustesse sous-ensemble** : la logique fonctionne pour tout sous-ensemble de
  {IR, US, PIR} (1, 2 ou 3 capteurs). Un capteur absent ne perturbe rien ; aucun capteur
  de comptage présent ⇒ pas de comptage (sauf option PIR-seul dégradée).
- **Modèle de données rétro-compatible** : `PglStoredEvent.sensorMode` devient un **bitmask**
  `PGL_SENS_IR=1 / PGL_SENS_US=2 / PGL_SENS_PIR=4` (IR+US=3 = ancien TANDEM, identique) ;
  `PglDetectionEvent` expose `sensorsMask` + `corroborated`. Taille de `PglStoredEvent` et
  format du record journal SD (20 octets) **inchangés** (`sensorMode`/`tandemValidated`
  restent des `uint8_t`).
- **Flags de board** : `PGL_ENABLE_PIR` (0 par défaut — la présence PIR est pilotée par flag,
  l'autodétection PIR n'étant pas fiable : repos = LOW), `PGL_PIR_PIN` (GPIO5 par défaut,
  **à adapter par board**), `PGL_PIR_GATES_COUNT` (0 = le PIR ne bloque jamais un comptage,
  pas de faux négatif), `PGL_PIR_COUNTS_WHEN_ALONE` (1 = PIR seul compte ses fronts, dégradé).
- **Veille** (toujours off par défaut, `PGL_ENABLE_SLEEP=0`) : réveil EXT0 = IR si présent,
  sinon PIR (niveau HAUT), sinon timer seul. Le réveil multi-broches (ext1 IR+PIR) reste une
  amélioration future.
- **Tests** : nouvelle suite native `test_detection_fusion` (15 cas couvrant la matrice
  1/2/3 capteurs, corroboration, gate, PIR-seul) ajoutée à `platformio-native.ini` et à la CI.
- **Suivi** : l'UI `PglDisplay` n'expose pas encore l'état PIR (env display non compilable
  ici — couvert par la CI) ; à ajouter dans une passe ultérieure.

Validation : `pgl-s3-headless` (défaut + `-DPGL_ENABLE_PIR=1`) compilés ; suites natives
`test_detection_fusion` (15) + `test_counter` + `test_journal_logic` vertes.

## 0.3.0 - 2026-06-25

Lot d'améliorations (robustesse, sécurité, observabilité, énergie, tests) :

- **Tests natifs Unity** : nouvelle suite (`test/`, `platformio-native.ini`) — 31 tests
  verrouillant la logique compteur/journal offline (CRC, décodage record, dayKey,
  comptage d'ack, rollover de jour, réconciliation, persistance NVS). Lancés par la CI.
- **Horloge provisoire** : amorçage au power-on à froid depuis un `lastKnownEpoch`
  persisté en NVS (throttlé), pour supprimer les `epoch=0` avant la 1ʳᵉ synchro NTP.
- **OTA** : câblage de la mise à jour OTA (lib partagée `n3_common/n3_ota`, vérif
  sha256 + ECDSA P-256, clé publique partagée), check périodique 2 h calqué sur msp/n3pp.
- **Énergie** : WiFi fast-reconnect (BSSID/canal mémorisés en RTC + fallback scan) ;
  timeout `pulseIn` ultrason ramené de ~25 ms à ~8 ms (dérivé de la portée max).
- **Affichage** : extinction automatique du rétroéclairage après inactivité
  (`PGL_BACKLIGHT_TIMEOUT_MS`, jusqu'ici inutilisée) + rallumage à la détection.
- **Télémétrie** : heartbeat enrichi (file pending/journal/nvs, santé SD, batterie,
  mode capteur) — rétro-compatible ; exploitation côté serveur à faire séparément.
- **Sécurité TLS** : épinglage du root CA en **opt-in** dans `n3_data` (via
  `n3_data_ca_cert.h`), **inerte par défaut** (comportement `setInsecure()` inchangé
  pour toute la flotte tant qu'aucun CA n'est fourni).
- **Nettoyage** : suppression du code mort `adminUnlocked` ; normalisation cosmétique
  des interlignes de `config.h` / `pgl_counter.h`.

Validation : `pgl-s3-headless` + 31 tests natifs verts à chaque étape ; env `pgl-s3-display`
validé par la CI. La veille profonde reste désactivée par défaut (alpha).

## 0.2.6 - 2026-06-24

- Veille : re-calibrage du couple timer/idle pour la rendre réellement efficace
  le jour où elle sera réactivée (`-DPGL_ENABLE_SLEEP=1`). La veille reste
  **désactivée par défaut** (`PGL_ENABLE_SLEEP=0`) pour l'alpha.
  - Distinction du timer de réveil selon le mode de détection :
    - **IR présent (EXT0)** : nouvelle constante `PGL_TIMER_WAKEUP_IR_S = 300 s`.
      L'IR réveille instantanément à la détection ; le timer ne sert plus qu'au
      housekeeping (vidage de la file, heartbeat), d'où une base longue alignée
      sur `PGL_UPLOAD_EVERY_MS`/`PGL_HEARTBEAT_INTERVAL_MS`.
    - **Pas d'IR (US seul / aucun capteur)** : base `PGL_TIMER_WAKEUP_S` passée
      de 2 s à 30 s. La détection ultrason en deep sleep est intrinsèquement par
      échantillonnage donc **lossy** (un passage plus court que l'intervalle peut
      être manqué) — documenté dans `config.h`. 2 s rendait l'appareil éveillé
      ~86 % du temps (boot + ≥12 s d'idle par cycle) : deep sleep inutile.
  - `computeSleepTimerS()` devient `computeSleepTimerS(bool irPresent)` et choisit
    la base selon l'IR (valeur déjà calculée `useIrWakeup`, plus de double
    interrogation de `gDetection`).
  - Surcharges **nuit** (1800 s) et **batterie faible** (`PGL_TIMER_WAKEUP_LOWBATT_S`
    porté à 600 s) désormais appliquées uniquement si elles **allongent** le timer
    courant, pour ne pas raccourcir une base IR déjà longue.
  - `PGL_IDLE_SLEEP_MS` conservé à 12 s : avec un timer IR de 300 s, le duty-cycle
    reste largement dominé par le sommeil (~4 %), et 12 s laisse le temps de finir
    un upload/heartbeat avant de dormir (commenté dans `config.h`).
- Validation : builds `pgl-s3-headless` (veille off, défaut) et `pgl-s3-headless`
  avec `-DPGL_ENABLE_SLEEP=1` (chemin veille) compilés avec succès.

## 0.2.5 - 2026-06-24

- Journal SD : backfill des horodatages invalides. Les événements captés avant la synchro
  NTP étaient écrits dans le journal SD avec `epoch=0` (ou `< 1700000000`) et envoyés tels
  quels au serveur. `PglEventJournal::fixInvalidEpochs(nowEpoch)` corrige désormais en place
  les records non encore acquittés `[ackOffset_, writeOffset_)` : ouverture unique du fichier
  en `r+`, réécriture des 20 octets (struct entière + CRC16 recalculé) pour chaque epoch
  invalide, records corrompus sautés sans casser l'alignement. Déclenché une seule fois via
  `PglCounter::fixInvalidEpochs` (qui corrige la FIFO NVS puis délègue au journal), appelée
  depuis `main.cpp` lorsque NTP devient valide (flag `gEpochBackfillDone`).

## 0.2.4 - 2026-06-24

- Audit firmware : corrections de cohérence, performance et nettoyage des dépendances.
- Veille : deep sleep désactivée par défaut via flag `PGL_ENABLE_SLEEP` (0). Le couple
  `PGL_TIMER_WAKEUP_S=2` / `PGL_IDLE_SLEEP_MS=12000` rendait la veille peu rentable et
  perturbait écran + son à chaque réveil. Réactiver via `-DPGL_ENABLE_SLEEP=1`.
- Audio : le jingle de démarrage n'est joué qu'au vrai power-on
  (`esp_sleep_get_wakeup_cause() == UNDEFINED`), plus à chaque sortie de deep sleep.
- Compteur : la réconciliation des totaux qui scanne le journal SD est limitée à
  `PGL_RECONCILE_INTERVAL_MS` (30 s) au lieu d'être exécutée à chaque tour de `loop()`
  quand des événements sont en attente (gros gain CPU/SD en mode offline).
- Journal SD : lectures séquentielles (`readPending`, `findAckOffset`, `sumDeltasInRange`)
  ouvrent désormais le fichier une seule fois au lieu d'une ouverture par record.
- Affichage : push QSPI du framebuffer uniquement lorsque LVGL a réellement redessiné
  (flush conditionnel) au lieu d'un transfert plein écran à chaque tour.
- Nettoyage : suppression de la dépendance `ESP32Time` (incluse dans `main.cpp` mais jamais
  utilisée — `n3_time` n'est pas compilé pour ce firmware) et des build flags morts
  `PGL_ENABLE_IR` / `PGL_ENABLE_ULTRASON` / `PGL_ENABLE_HMAC`. `WiFi.mode(STA)` n'est plus
  appelé deux fois au boot. (`Arduino_JSON` est conservé : requis par la lib partagée
  `n3_common/n3_outputs_json`.)
- Validation : builds `pgl-s3-headless` et `pgl-s3-display` compilés avec succès.

## 0.2.3 - 2026-06-16

- Production : deep sleep reactivee (`PGL_DEBUG_NO_SLEEP=0` par defaut) ; env `pgl-s3-debug` pour bench.
- Sync : mode NVS aligne sur `last_acked_event_id` ; `commitJournalAck` ne somme que les evenements acquittes.
- Journal SD : reprise upload apres panne d'ecriture (`canRead` vs `isSdOk`) ; pending = journal + NVS ; compaction atomique avec backup.
- Reseau : HTTPS ; parse JSON ArduinoJson pour l'ack serveur.
- Temps : NTP obligatoire avant upload ; timezone `Africa/Casablanca` ; epoch 0 si NTP absent.
- NVS : persistance lazy de `nextEventId` ; log ecrasement FIFO pleine.

## 0.2.2 - 2026-06-16

- Sync compteurs : montage SD avant le journal offline (le journal etait inactif si la SD n'etait pas encore montee).
- Persistance immediate des totaux NVS a chaque detection (plus de decalage total affiche vs evenements journalises).
- Reconciliation au boot et apres ack : `total = sync_acquitte + pending_journal` ; migration automatique des evenements deja ack sur SD.

## 0.2.1 - 2026-06-16

- WiFi non bloquant : boot immédiat offline-first, connexion en arrière-plan depuis `loop()` (budget 40 ms/tour).
- Affichage : états WiFi distincts (recherche, connexion, hors ligne, connecté).
- Upload : essai court (2 s) avant POST ; événements conservés en file si pas de réseau.
- Lib partagée `n3_wifi` : API session `n3WifiSessionBegin` / `n3WifiSessionPoll` (scan async).

## 0.2.0 - 2026-06-16

- Offline-first : journal d'événements append-only sur carte SD (`pgl_event_journal`) avec CRC16 par record et compaction automatique (seuil 1 Mo acquitté).
- Identifiant monotone `event_id` par événement (persisté en NVS) pour garantir l'idempotence côté serveur.
- Sync par rattrapage : boucle de lots dans un budget temps (8 s) au retour du WiFi ; ack `last_acked_event_id` renvoyé par le serveur.
- Mode dégradé (SD absente) : retour automatique à la NVS FIFO existante avec indicateur dans les logs.
- Serveur : champ `device_event_id` + contrainte `UNIQUE(board, device_event_id)` + `INSERT IGNORE` idempotent ; réponse JSON enrichie `last_acked_event_id`.

## 0.1.23 - 2026-06-16

- Ecran : correction rendu texte LVGL (flush BeRGB aligne LVGL_Widgets quand LV_COLOR_16_SWAP=1) — suppression pixels blancs dans les glyphes anti-aliasés.

## 0.1.22 - 2026-06-16

- Ecran : lisibilite UI — pastilles header en flex (plus de chevauchement), textes donnees en opacite pleine 14 pt, colonne capteurs elargie.

## 0.1.21 - 2026-06-16

- Ecran : refonte UI LVGL haute qualité en cartes (LED d’état, jauge ultrason, barre file serveur, compteurs et smiley).

## 0.1.20 - 2026-06-16

- Nouvel environnement `pgl-s3-jc3248` : module Guition JC3248W535 (320x480, AXS15231B, tactile I2C integre, flash 16 Mo).
- Affichage multi-board : `pgl_display_board.h`, UI portrait adaptee, flush BeRGB pour AXS15231B.
- `PGL_IR_PIN` surchargeable via build flag ; doc `docs/JC3248W535_REFERENCE.md`.

## 0.1.19 - 2026-06-16

- Reseau : logs HTTP enrichis (corps reponse serveur, verdicts 401/400/500 explicites) pour diagnostic post-data / heartbeat.

## 0.1.18 - 2026-06-16

- Ecran : tableau de bord en 2 colonnes (compteurs / capteurs-reseau), polices 12-16 pt, retour a la ligne pour eviter les superpositions.

## 0.1.17 - 2026-06-16

- Detection : mode tandem ne bloque plus le comptage US seul (IR + US câblés) ; promotion US en runtime si absent au boot ; log `US GPIO6: declenchement` ; lectures US ~10 Hz.

## 0.1.16 - 2026-06-16

- Logs + ecran : statut IR (test boot, heartbeat, front obstacle) et ecran (gfx/LVGL FONCTIONNEL/ECHEC, ligne `Ecran: OK | IR: libre`).

## 0.1.15 - 2026-06-16

- Capteur IR unifie sur **GPIO 7** (display + headless) ; `pgl-s3-display` env principal, headless secondaire.

## 0.1.14 - 2026-06-16

- Serveur : logs detailles POST/heartbeat (`Serveur POST/HB`) + ligne ecran `Srv: data=HTTP hb=HTTP | file=N`.

## 0.1.13 - 2026-06-16

- Audio : migration I2S JC4827W543 (ampli NS4168, sortie speak) ; MP3 depuis micro-SD du module (`/mp3/001.mp3`, …) via ESP32-audioI2S.
- Suppression DFPlayer Mini (UART + lib DFRobot).
- Display : IR sur GPIO 5 (GPIO 4 = tactile GT911) ; ADC batterie desactive (GPIO 2 = I2S_LRCK).
- Ecran : distance ultrason en temps reel (`US: N cm`, surligne en zone de declenchement).
- Headless : mode silencieux (pas d'ampli embarque).

## 0.1.12 - 2026-06-16

- Ecran : tableau de bord (compteur total/aujourd'hui, WiFi, son en cours) ; init LVGL amelioree ; defaut `pgl-s3-display`.
- Audio : notification ecran au demarrage/fin de lecture.

## 0.1.11 - 2026-06-16

- Audio : logs explicites `>>> LECTURE` / `<<< FIN` avec motif (demarrage, remerciement) et chemin `mp3/NNN.mp3`.

## 0.1.10 - 2026-06-16

- WiFi : liste complete des AP visibles au scan (`[PGL] WiFi scan:` au boot + `[WiFi] scan/rescan` dans n3_wifi, reseaux configures marques *).

## 0.1.9 - 2026-06-16

- WiFi : 3e reseau secrets.h, connexion apres init, timeout 10s ; n3_wifi rescan si SSID invisible.

## 0.1.8 - 2026-06-16

- Lecture aleatoire d'un MP3 du dossier `mp3/` au demarrage (`playStartup`).

## 0.1.7 - 2026-06-16

- Logs detailles audio (scan SD, lecture/erreurs DFPlayer, fin de piste) et WiFi (SSID, IP, scan n3_wifi).

## 0.1.6 - 2026-06-16

- Persistance NVS lazy (batch) ; timer veille adaptatif (nuit / batterie faible).

## 0.1.5 - 2026-06-16

- Detection DFPlayer fiable (reset + verification volume) ; config flash 4 Mo headless.

## 0.1.4 - 2026-06-16

- Audio DFPlayer : detection dynamique du nombre de pistes dans `mp3/` au boot (plus de limite fixe a 10).

## 0.1.3 - 2026-06-16

- Ultrason HC-SR04 sur une seule broche (`PGL_US_PIN`, GPIO6) : trig et echo partages, comme ffp5cs.

## 0.1.2 - 2026-06-02

- Heartbeat serveur `POST /pgl/heartbeat` (flag `PGL_ENABLE_SERVER_HEARTBEAT`, intervalle 2 min + piggyback apres upload).

## 0.1.0 - 2026-05-19

- Initialisation du firmware poissonglouton (display + headless).
- Comptage IR / ultrason / tandem avec anti-double-compte.
- Audio DFPlayer, UI LVGL ludique, upload batch vers serveur.
- Deep sleep optimise pour alimentation solaire.
