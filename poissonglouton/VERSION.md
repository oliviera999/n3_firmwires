# Poissonglouton - Historique versions

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
