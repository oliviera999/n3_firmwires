# Poissonglouton - Historique versions

## 0.5.22 - 2026-07-27

- **Correctif OTA (audit `docs/AUDIT_BUGS_2026-07.md`, constat F3)** — `n3_common` 1.8.4. `compareVersions` ignorait le retour de `sscanf` : une version distante illisible etait parsee en `0.0.0`, donc toujours <= la version locale -> « Deja a jour ». Une metadata malformee immobilisait silencieusement la flotte. Nouveau `parseVersion()` (echec sous deux composantes lisibles) et garde explicite dans `n3OtaCheck` : la cause est desormais journalisee en ERREUR et remontee a `onUpdateEnd`.

## 0.5.21 - 2026-07-27

- **Durcissement (audit `docs/AUDIT_BUGS_2026-07.md`, constats F4 et F5)**. (F4, `n3_common` 1.8.3) `n3OtaCheck` initialise `integrityDetails[192]`, tampon journalise et transmis a `onUpdateEnd` en cas d'echec OTA : tous les chemins d'echec l'ecrivent aujourd'hui, mais un futur oubli de `snprintf` aurait affiche de la memoire de pile non initialisee. (F5, `n3_hmac` 1.1.1) `n3HmacSha256` verifie `key`/`message`/`hexOutput` avant `strlen`, alignant le wrapper plat sur `computeHmacHex` qui validait deja ses parametres ; `n3HmacSignRequest` ne pose plus aucun header si le calcul echoue. Deux cas ajoutes a `test_hmac`. **Latent** : aucun appelant actuel ne passe de pointeur nul — aucun changement de comportement observable.

## 0.5.20 - 2026-07-27

- **Correctif OTA (audit `docs/AUDIT_BUGS_2026-07.md`, constat F1) : chien de garde de stagnation dans la boucle de telechargement** (`shared/n3_common` 1.8.2). `downloadAndFlashFirmware` scrutait `stream->available()` avec `delay(1)` sans borne de temps : `http.setTimeout()` ne couvre pas cette scrutation manuelle et `http.connected()` reste vrai tant que le socket n'est pas ferme. Un serveur qui garde la connexion ouverte sans plus rien emettre figeait l'appareil **indefiniment**, partition OTA en cours d'ecriture — sans meme un reset watchdog (`delay(1)` rend la main, donc la tache IDLE tourne). PGL est particulierement expose : `n3OtaCheck()` est **synchrone** et bloque deja `poll()` pendant le telechargement (cf. 0.5.16, P3) — un blocage sans fin y arretait tout le comptage de bouteilles. Ajout de `N3_OTA_STALL_TIMEOUT_MS` (15 s par defaut) : au-dela de ce delai sans aucun octet recu, l'OTA est abandonnee proprement avec un message d'echec explicite. Un telechargement lent mais qui **progresse** n'est pas affecte.

## 0.5.19 - 2026-07-21

- **Mutualisation diagnostic WiFi (chantier shared)** : `pglWifiStatusName` (libellés `wl_status`) délègue au module pur `shared/n3_wifi_diag` (0.1.0), testé en natif (`test_wifi_diag`). **Iso-comportement** (mêmes libellés IDLE/NO_SSID/SCAN_DONE/CONNECTED/CONNECT_FAIL/LOST/DISCONNECTED, défaut UNKNOWN). Les libellés **courts** de raison de déconnexion (`pglWifiDisconnectReasonName`, abrégés pour l'écran LVGL et divergents des tokens ESP-IDF canoniques) restent **locaux** — volontairement non mutualisés. Ajout du chemin d'include partagé à `platformio-native.ini`.

## 0.5.18 - 2026-07-21

- **Mutualisation WiFi #2 (chantier shared)** : le fast-reconnect maison de PGL (`tryFastReconnect` / `rememberLastGoodAp` / record RTC `gLastGoodAp`) délègue au noyau pur `shared/n3_wifi_reconnect` (0.1.0), testé en natif (`test_wifi_reconnect`, 11 cas). Le record RTC devient `N3WifiReconnect::LastGoodAp` (layout identique à l'ancien `PglLastGoodAp` → compatible OTA), la mémorisation passe par `N3WifiReconnect::store`, la gate de validité par `N3WifiReconnect::valid`, et la borne de la tentative ciblée par `N3WifiReconnect::fastTimeoutMs(PGL_WIFI_TIMEOUT_MS)` (= 7500 ms, inchangé). Le magic PGL (`0x50474C57`) et le pinning SSID+BSSID+canal sont conservés ; `findPassForSsid` (lookup du mot de passe) reste local. **Sans changement observable.** Même code qui duplique désormais partagé avec `n3_wifi` (`disableFastReconnect=true` toujours actif : les deux fast-reconnect ne se marchent pas dessus).

## 0.5.17 - 2026-07-21

- **Mutualisation WiFi (chantier shared)** : `n3_wifi` (1.3.0) délègue la sélection au noyau pur `shared/n3_wifi_select` (0.1.0). La construction de l'ordre d'essai (meilleure candidate RSSI + BSSID/canal par credential, réseaux visibles triés par RSSI décroissant, égalités → index d'origine, SSID cachés en fin) est extraite de la boucle locale `buildOrderFromScan` vers le module pur testé en natif (`test_wifi_select`, 10 cas). **Sans changement observable** (mêmes candidats, même ordre, `strcmp` exact, comparaison RSSI stricte) : PGL consomme `n3_wifi` sans modification de son propre code (son fast-reconnect maison `tryFastReconnect` et `disableFastReconnect=true` restent inchangés).

## 0.5.16 - 2026-07-15

- **Fix comptage reveil EXT0 (P1)** : a la sortie du deep sleep sur obstacle IR, la broche est deja LOW ; `begin()` echantillonnait `irPrevState_=LOW` et `poll()` ne voyait jamais le front HIGH->LOW → la bouteille declencheuse n'etait jamais comptee. Comptage unique differe arme sur reveil EXT0 (attribue a l'IR), consomme au 1er `poll()`, sans double-comptage si l'obstacle persiste (`pgl_detection`).
- **Fix debounce US defait par le cache (P2)** : `getUltrasonDistanceCm()` met une mesure en cache ~100 ms alors que `poll()` tourne ~10 ms ; `usBelowCount_` avancait ~10x par ping et `PGL_US_CONSECUTIVE_POLLS=2` etait satisfait par une seule mesure physique. Sequence de mesure (`usMeasureSeq_`) : le compteur "sous seuil" n'avance que sur une NOUVELLE mesure (N polls = N pings distincts).
- **Fix boucle d'upload SD sans progression (P4)** : si un ack renvoie un `last_acked_event_id` hors lot / en-deca du curseur, `commitJournalAck()` n'avancait rien et la boucle de drain re-uploadait le meme lot jusqu'a epuisement du budget 8 s. Detection du non-avancement (pending inchange) → arret du drain pour ce cycle (`main.cpp`).
- **Attenuation OTA synchrone (P3)** : `n3OtaCheck()` bloque `poll()` pendant le telechargement (bouteilles manquees = limitation connue, fix async hors scope). `gCounter.flush()` avant le check pour ne perdre aucun etat de comptage en attente si l'OTA redemarre la carte.
- **Nettoyage** : suppression du stub vide `pgl_display_stub.cpp` ; reformatage single-spacing de `pgl_audio.cpp` et `pgl_counter.h` (blank lines uniquement, logique inchangee).

## 0.5.15 - 2026-06-29

- **JC3248 anti-scintillement** : throttle push QSPI (max ~10 fps), buffer LVGL 40 lignes (sans full_refresh), init type1, retroeclairage toujours ON sur JC3248 (`PGL_BACKLIGHT_TIMEOUT_MS=0`).
- **JC3248** : touch rallume le backlight ; labels US mis a jour seulement si le texte change.

## 0.5.14 - 2026-06-29

- **JC3248 debug** : env `pgl-s3-jc3248-debug` (`PGL_DISPLAY_DEBUG`), logs `[PGL][DISP]`, self-test bandes R/V/B au boot, stats flush/push toutes les 30 s.
- **JC3248 rendu** : buffer LVGL plein ecran + `full_refresh`, blit BeRGB vers canvas (meme chemin LVGL que JC4827).

## 0.5.13 - 2026-06-29

- **JC3248 / AXS15231B** : flush canvas uniquement sur `lv_disp_flush_is_last` — le panneau ne gère pas les mises à jour partielles (CASET/RASET ignorés) ; un push QSPI prématuré provoquait un affichage tronqué ou corrompu.

## 0.5.12 - 2026-06-26

- **n3_wifi 1.2.4** : retry sans BSSID apres pause 500 ms + `disconnect(true)` (handshake inwi).

## 0.5.11 - 2026-06-26

- **WiFi boot** : scan demarre tot (comme v0.5.8) — le differe post-display voyait 0 AP.
- **WiFi fiabilite** : `WiFi.setSleep(WIFI_PS_NONE)` pour handshake sur AP faible (inwi -96 dBm).

## 0.5.10 - 2026-06-26

- **WiFi inwi prioritaire** : ordre de scan AP-Techno/inwi/Android → **inwi d'abord** (SSID_2).
- **WiFi boot** : scan demarre apres init display ; pre-scan 1,5 s ; timeout association 15 s.
- **Upload** : uniquement si WiFi connecte (plus de POST annule en NO_SSID).
- **Diag** : log explicite sur echec 4WAY_HANDSHAKE (mot de passe secrets.h).

## 0.5.9 - 2026-06-26

- **WiFi diag ecran** : correction `snprintf` (argument `»` en trop → idx/stat corrompus).
- **Heartbeat serveur** : intervalle 2 min respecte meme en cas d'echec HTTP (plus de rafale toutes les 5 s).
- **HTTP** : timeout POST porte a 12 s (`N3_HTTP_TIMEOUT_MS`).

## 0.5.8 - 2026-06-26

- **Fix reboot WiFi (critique)** : `N3WifiConfig` persiste dans `PglNetwork` — l'ancien pointeur vers une variable stack provoquait un crash `LoadProhibited` dans `beginTryCurrentNetwork` apres le scan.
- **n3_wifi** : garde SSID null dans `beginTryCurrentNetwork`.
- **Horloge** : creation namespace NVS `pgltime` au premier boot (plus de log E NOT_FOUND).

## 0.5.7 - 2026-06-26

- **Fix reboot WiFi (critique)** : moteur I2S lazy (`new Audio` sur detection, `delete` apres lecture) — plus de tache decodeur PSRAM au boot pendant le scan WiFi.
- **Boot** : WiFi demarre avant init audio/display ; scan sans conflit I2S.

## 0.5.6 - 2026-06-26

- **Audio demarrage** : `PGL_ENABLE_STARTUP_JINGLE=0` par defaut — pas de MP3 au boot ; son uniquement sur detection (`playThanks`). WiFi demarre immediatement au cold boot.

## 0.5.5 - 2026-06-26

- **Fix son en boucle** : ultrason re-arme apres sortie de zone (<25 cm) ; pas de nouveau comptage/MP3 tant que l'objet reste devant.
- **Audio** : `audio.loop()` maintenu tant que `isRunning()` ; ignore une nouvelle lecture si une piste joue deja.

## 0.5.4 - 2026-06-26

- **Fix reboot WiFi** : `audio.loop()` uniquement pendant une lecture ; drain + veille decodeur avant scan WiFi.
- **WiFi** : 12 s de stabilisation apres fin du jingle avant `pollWifi`.

## 0.5.3 - 2026-06-26

- **Fix reboot audio** : suppression des callbacks AudioLib (Serial non thread-safe depuis la tache decodeur).
- **WiFi** : demarrage differe apres le jingle de boot ; pas de `pollWifi` pendant une lecture I2S.
- **WiFi (suite)** : demarrage reporte en loop apres fin audio + 3 s de stabilisation decodeur.
- **Audio** : retour pipeline v0.1.x (sans validation MP3 erronnee, sans `setAudioTaskCore`/`forceMono`/`pumpDecoder`).

## 0.5.2 - 2026-06-26

- **Audio** : validation entete MP3 Layer III avant lecture (evite crash decodeur sur fichiers invalides).
- **Fix** : champ layer MPEG corrige (Layer III = 2, pas 1).
- **WiFi/audio** : jingle de demarrage differe tant que le scan WiFi est actif.
- **I2S** : tache decodeur sur coeur 1, `forceMono(true)` pour ampli NS4168 mono.

## 0.5.1 - 2026-06-26

- **Audio** : init I2S avant le display QSPI ; jingle de demarrage differe en debut de `loop()`.
- **Decodeur** : `pumpDecoder()` apres `connecttoFS` ; verification retour + `SD.exists`.
- **Diagnostic** : callback `audio_info_callback` (logs AudioLib, bitrate, EOF) ; rescan SD si pistes absentes au boot.

## 0.5.0 - 2026-06-26

Lot P2/P3 audit : capteurs, reseau, serveur, config, qualite.

- **IR** : flag build `PGL_IR_PRESENT` (-1 autodetect, 0/1 force) ; autodetect exige au moins un LOW.
- **US runtime** : promotion/demotion avec compteurs consecutifs (plus de reset sur un seul timeout).
- **Tests natifs** : helpers poll (US 2 polls, garde PIR, promotion US, front IR) dans `test_detection_poll`.
- **WiFi** : causes d'echec session n3_wifi (`scan`, `no_ap`, `all_nets`…) + logs PGL enrichis.
- **Upload** : backoff HTTP exponentiel (streak, plafond 5 min) ; payload events en buffer fixe.
- **HMAC** : support optionnel `PGL_API_SIG_SECRET` (defaut auth `api_key` seule, documente).
- **Serveur** : `sensors_present` stocke en `pglHeartbeat` ; POST `mode` = bitmask PGL_SENS_*.
- **Envs** : `pgl-s3-display-pir`, `pgl-s3-headless-sleep` ; `PGL_IR_PRESENT=1` sur display prod.
- **Qualite** : `popBatch` sans buffer stack 2 Ko ; dedup display ; rename `computeSleepTimerS`.

## 0.4.4 - 2026-06-26

- **WiFi diagnostic** : phase de connexion (scan, try, connect, rapide, pause…), SSID cible,
  statut `wl_status`, raison de deconnexion et compte a rebours affiches a l'ecran (wrap LVGL).
- **Moniteur serie** : logs periodiques `WiFi diag:` + SSID configures au boot en `PGL_LOG`.
- **Fix UI** : `isWifiConnecting()` inclut fast-reconnect et backoff (evite « connexion... » fige).

## 0.4.3 - 2026-06-26

- **UI** : version firmware (`vX.Y.Z`) affichee dans le header LVGL sous le titre.
- **Ultrason ESP32-S3** : remplacement de `pulseIn()` par une lecture active de l'echo
  (aligne ffp5cs) — `pulseIn` etait peu fiable sur S3 et bloquait l'affichage/distances US.
- **Affichage US** : distance montree des qu'un echo est recu (meme si absent au boot) ;
  libelle « pas d'echo » au lieu de « capteur absent » quand aucune mesure.

## 0.4.2 - 2026-06-26

Correctifs suite audit firmware (WiFi, HTTPS, detection, contrat serveur).

- **HTTPS/TLS** : URLs serveur en **HTTP par defaut** (`PGL_SERVER_SCHEME`), aligne n3pp/msp.
  Envs `pgl-s3-display-https`, `pgl-s3-headless-https`, `pgl-s3-jc3248-https` avec
  `-DUSE_HTTPS_ENDPOINTS` (compile-check CI).
- **Detection** : debounce global (`PGL_DEBOUNCE_MS`) n'fige plus la MAJ d'etat capteur
  (`irPrevState_`, `usBelowCount_`, `pirPrevState_`) — seule la decision de comptage est
  bloquee. Helper testable `pglDetectionDebounceAllowsCount()` + suite `test_detection_poll`.
- **WiFi** : reconnexion proactive apres perte de lien ; fast-reconnect PGL dedie
  (`disableFastReconnect=true` cote n3_wifi) ; deadline fast-reconnect = `timeout/2`.
  Copie SSID en buffer avant affichage/logs (evite dangling `String::c_str()`).
- **Heartbeat** : champ `sensors_present` (bitmask capteurs presents) remplace
  `sensor_mode` ambigu ; doc serveur `ENDPOINTS_ESP32_SERVEUR.md` mise a jour.
- **Nettoyage** : suppression `PglSleep::configure()`, `pirEdgeGuard_`, `hwLine_` mort,
  branche UI `setWifiSearching()` inaccessible ; +3 tests fusion.

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
