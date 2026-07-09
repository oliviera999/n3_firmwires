# Audit — Tracker solaire msp1 (gestion des angles du panneau selon la luminosité)

> ✅ **Mise en œuvre (v2.54, 2026-07-08)** : C1, C3, C4, C5 (pauses OLED), C6 et E1 corrigés/implémentés —
> asservissement différentiel par défaut, balayage optimisé (grossier+fin, fusion pondérée C2) sélectionnable
> via la clé serveur `113` (112 = veille infinie depuis la v2.53), logique extraite dans `shared/n3_tracker` (E5, tests `test_tracker`).
> Restent ouverts : C7/E4 (seuil configurable + hystérésis), C8 (parking nuit/pluie), E2 (hybride astronomique).
> **v2.55** : calibration des photorésistances (égalisation des sensibilités, clé serveur `114`, gains NVS).

**Date** : 2026-07-08 · **Firmware audité** : `msp` v2.52 · **Périmètre** : logique servo/luminosité
(`src/msp_sensors.cpp` — `Light_val()`, `src/msp_network.cpp` — clés 104/105/111,
`src/main.cpp` — init servos & cycle, `include/msp_config.h` — bornes et pins).

---

## 1. Fonctionnement actuel

### Matériel
| Élément | Pin | Rôle |
|---|---|---|
| LDR A / B | GPIO 33 / 34 (ADC1) | Axe gauche-droite (azimut) |
| LDR C / D | GPIO 35 / 39 (ADC1) | Axe haut-bas (élévation) |
| Servo GD | GPIO 25 | Azimut, plage logicielle **1–179°** |
| Servo HB | GPIO 14 | Élévation, plage logicielle **40–145°** |

Les 4 LDR sont sur **ADC1** : pas de conflit WiFi/ADC2 — bon choix.

### Logique (un cycle = un réveil deep sleep, sauf `WakeUp=1`)
1. `setup()` : servos attachés, position de repli **milieu de plage** (90° / 92°).
2. `variablestoesp()` (GET `/api/outputs/state?board=2`) : clé `111` → `servoModeAuto`,
   clés `104`/`105` → `AngleServoHB`/`AngleServoGD` (consignes manuelles).
3. `Light_val()` :
   - Lecture systématique des 4 LDR + moyenne (télémétrie, fix v2.41).
   - **Mode MANUEL** : clamp des consignes aux bornes, écriture servos, log `[SERVO][APPLY]`.
   - **Mode AUTO** : si `LuminositeMoy > 50` (`LIGHT_SCAN_MIN_THRESHOLD`), **balayage complet**
     des deux axes (pas de 1°, 15 ms de stabilisation servo + 5 ms LDR par pas), moyenne
     glissante sur 10 lectures, détection du pic par LDR, puis
     `angle = (posMaxLdr1 + posMaxLdr2) / 2` par axe, borné à la plage. Sinon : aucun
     mouvement (`scan=SKIP`).
4. POST `post-data` : `ServoGD`, `ServoHB`, `LuminositeA–D`, `LuminositeMoy`.
5. Deep sleep (`FreqWakeUp` s) — les angles ne sont **pas persistés**.

Durée d'un scan complet : 179×20 ms (GD) + 106×20 ms (HB) + 4×750 ms de pauses OLED ≈ **9 s**
(déjà réduit de ~23 s par l'audit algo 2026-06).

---

## 2. Constats

### C1 — Télémétrie d'angle mensongère la nuit en mode AUTO *(bug, priorité haute)*
`variablestoesp()` importe les clés `104`/`105` dans `AngleServoGD/HB` **même en mode AUTO**
(`msp_network.cpp:255-256`). La nuit (`lum ≤ 50`), la branche `scan=SKIP` n'écrit pas les servos :
la position physique reste celle du repli de boot (90°/92°), mais le POST envoie les valeurs
des sliders serveur. **La BDD enregistre des angles jamais appliqués.**
→ Corriger : n'importer 104/105 qu'en mode MANUEL, ou tracer séparément « consigne » et
« angle appliqué » (seul ce dernier étant posté).

### C2 — Fusion `(pos1 + pos2) / 2` fragile *(bug potentiel, priorité haute)*
Si une seule LDR d'un axe détecte un pic (l'autre ombragée, sale ou HS), son `posLumMax`
reste à 0 et l'angle final est **divisé par deux** (soleil à 120° → consigne 60°). Aucune
validation du pic (amplitude minimale), aucune pondération.
→ Ignorer un pic dont le max est sous un seuil et se rabattre sur l'autre LDR ; ou pondérer
par l'amplitude ; au minimum logguer un `[SERVO][SCAN][WARN]` en cas d'asymétrie forte.

### C3 — Moyenne glissante inter-positions : biais et retard *(bug d'algo, priorité moyenne)*
`average = total / numReadings` avec un tampon de 10 échantillons rempli **au fil des positions** :
- les 9 premières positions du balayage sont diluées par des zéros → les angles proches de
  `minAngle` sont structurellement défavorisés ;
- la moyenne traîne ~5 positions derrière le servo → `posLumMax` est décalé d'environ **+5°**
  après le vrai pic, et le pic est lissé/écrêté.
→ Filtrer **par position** (médiane/moyenne de N lectures au même angle — réutiliser
`n3_analog_sensors` déjà employé pour `HumiditeSol`) au lieu de moyenner à travers les positions.

### C4 — Sémantique incohérente de `LuminositeA–D` postées *(qualité de données, priorité moyenne)*
Après un scan, les variables contiennent les **maxima vus pendant le balayage** ; en mode manuel
ou sous le seuil, ce sont des lectures **instantanées**. La même colonne BDD mélange deux
grandeurs différentes.
→ Re-lire les LDR après positionnement final (valeur instantanée partout), et éventuellement
poster les pics du scan dans des champs dédiés.

### C5 — Coût énergétique et usure du balayage systématique *(conception, priorité moyenne)*
Station sur batterie ; chaque réveil lumineux = 285 pas de servo (~9 s éveillé, servos sous
couple + WiFi actif). À `FreqWakeUp` = 10 min, ~40 000 micro-mouvements/jour : usure notable
des pignons, consommation évitable. Les 4×`delay(750)` s'exécutent d'ailleurs **même sans OLED**
(`displayOk` ne conditionne que l'affichage, pas la pause) : 3 s d'éveil perdues par scan.
→ Voir pistes E1/E2 ; à court terme, conditionner les pauses à `displayOk`.

### C6 — Perte de position à chaque réveil *(conception, priorité moyenne)*
Les angles ne sont ni en `RTC_DATA_ATTR` ni en NVS : chaque boot repart au milieu de plage puis
refait un scan complet. Le panneau fait un aller-retour inutile même si le soleil n'a presque
pas bougé depuis le dernier réveil.
→ Persister `AngleServoGD/HB` (RTC RAM suffit entre réveils timer ; NVS pour survivre au reset)
et repartir de la dernière position.

### C7 — Seuil de scan bas, sans hystérésis *(robustesse, priorité basse)*
`LIGHT_SCAN_MIN_THRESHOLD = 50` sur 4095 (~1,2 %) : au crépuscule le tracker peut scanner pour
un gain nul, et osciller autour du seuil d'un réveil à l'autre. Seuil non configurable serveur
(contrairement à `SeuilSec`, `FreqWakeUp`…).
→ Hystérésis (ON > X, OFF < Y) + exposition via une clé GPIO serveur.

### C8 — Pas de position de repli nocturne / météo *(fonctionnel, priorité basse)*
Sous le seuil, le panneau reste où il est. Ni « parking » nocturne (à plat = moindre prise au
vent, ou orienté est pour le lever), ni mise à plat sur pluie forte alors que le capteur
`Pluie` (GPIO 27) est déjà lu dans le même cycle.

### C9 — Aucune couverture de test *(process, priorité basse)*
La logique (clamp, fusion des pics, seuil/hystérésis) est enchevêtrée avec `Servo`, `analogRead`
et l'OLED : rien n'est testable en natif, alors que le dépôt a une infrastructure Unity
(`shared/tests_native`, `ffp5cs`). Les régressions passées (v2.28 : dérive des accumulateurs)
l'illustrent.

### Points sains notés
- Clamp systématique des consignes manuelles avec log `[WARN]` (v2.36) — bon.
- Bornage post-scan des deux axes (évite l'écriture hors plage si scan vide) — bon.
- Lecture LDR systématique avant la logique (v2.41, plus de zéros en BDD) — bon.
- Logs structurés `[SERVO][MODE]/[TARGET]/[AUTO]/[APPLY]/[SCAN]` avec dédup — bon.
- LDR sur ADC1 uniquement, mode/consignes conservés si le serveur est injoignable — bon.

---

## 3. Pistes d'évolution

### E1 — Asservissement différentiel plutôt que balayage *(recommandé, gain majeur)*
Le montage 4-LDR en croix est celui du tracker différentiel classique : à position courante,
comparer A/B (resp. C/D) et faire des petits pas vers l'équilibre, avec zone morte
(`|A−B| < ε` → immobile). Quelques dizaines de ms par réveil au lieu de 9 s, usure divisée
par ~100. Garder le balayage complet comme **recalage** occasionnel (1×/jour ou si l'équilibrage
diverge). Nécessite que les LDR d'un même axe soient séparées par une cloison — à vérifier sur
le montage physique avant de basculer.

### E2 — Hybride astronomique (option long terme)
L'heure NTP et la position GPS (fixe) permettent de calculer azimut/élévation solaires (algo NOAA,
~1 kB de code, zéro capteur). Les LDR ne serviraient plus qu'à un ajustement fin et à la
télémétrie. Fonctionne par ciel couvert (où le scan actuel pointe n'importe où) et de nuit
(parking est prévisible). Contrepartie : calibration mécanique angle-servo ↔ angle réel.

### E3 — Corrections court terme (indépendantes de E1/E2)
1. **C1** : ne plus importer 104/105 en AUTO ; poster l'angle réellement appliqué.
2. **C2** : validation/pondération des pics par axe.
3. **C3** : filtrage par position via `n3_analog_sensors` (médiane puis moyenne).
4. **C5** : `delay(750)` conditionnés à `displayOk` ; envisager `servo.detach()` après
   positionnement (économie + suppression du jitter PWM au repos).
5. **C6** : angles en `RTC_DATA_ATTR`, restauration au boot avant la config distante.
6. **C4** : re-lecture instantanée post-positionnement pour la télémétrie.

### E4 — Configuration & observabilité
- Seuil de scan (avec hystérésis) et zone morte exposés en clés GPIO serveur, comme `107`.
- Poster aussi la durée du scan et l'écart consigne/mesure pour suivre la santé mécanique.

### E5 — Testabilité
Extraire la logique pure (clamp, fusion des pics, différentiel, hystérésis) dans un module
sans dépendance Arduino (p. ex. `shared/n3_tracker` ou `msp/lib/`), couvert par des tests
Unity natifs — même modèle que `n3_hmac`/`test_nvs`. C'est le prérequis pour refactorer E1
sans casser le comportement terrain.

---

## 4. Ordre de mise en œuvre suggéré

| Étape | Contenu | Risque terrain |
|---|---|---|
| 1 | E3.1 + E3.4 (C1, delays OLED) | Nul |
| 2 | E3.2 + E3.3 (fusion + filtrage par position) | Faible — à valider sur cible |
| 3 | E3.5 + E3.6 (persistance RTC, télémétrie propre) | Faible |
| 4 | E5 (extraction testable) puis E1 (différentiel) | Moyen — vérifier la cloison inter-LDR |
| 5 | E4, puis E2 si besoin de précision par ciel couvert | Moyen |

Chaque étape suit le process du dépôt : bump `FIRMWARE_VERSION` + `VERSION.md`, build local
(`pio run` dans `msp/`), validation des logs `[SERVO]` sur cible avant push.
