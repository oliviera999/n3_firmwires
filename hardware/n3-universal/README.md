# n3-universal — étude de faisabilité (étape 0)

Projet : **une carte porteuse commune** aux trois systèmes (msp station météo,
n3pp serre/élevage, ffp5cs aquaponie), bi-module (site A1 WROOM DevKit V1 /
site A2 ESP32-S3-DevKitC-1), 12/24 V, avec :
- rail capteurs commuté `+3V3_SW` + pont diviseur commuté (repris de
  `n3pp-msp-commun` rev 0.2), **jumper bypass fermé par défaut** (ffp5cs, toujours
  alimenté, ne pilote pas le gate) ;
- bloc solaire TP4056 + 18650 en option de peuplement (msp/n3pp) ;
- RTC **DS3231** : VCC du module sur **`+3V3_SW`** (coupé en veille — PAS de rail
  permanent nécessaire) : le chip bascule nativement sur sa **pile bouton** et
  continue de compter (~3 µA, compensation température maintenue, CR2032 ≈ 8 ans) ;
  l'I2C est inaccessible sous VPF par conception (pas de fuite vers le rail éteint,
  pull-ups déjà sur `+3V3_SW`). Au réveil le firmware relit l'heure et resynchronise
  l'horloge interne (dérive deep sleep de l'ESP32 : minutes/jour → DS3231 ±2 ppm).
  Peuplement modules ZS-042 : **neutraliser le circuit de charge** (diode+200 Ω prévus
  LIR2032 — dessouder, monter une CR2032) et retirer la LED d'alim du module.
- slot **microSD unique** câblé aux deux sites : natif côté S3, et côté WROOM sur des
  nets inutilisés par msp/n3pp (CS=14/US3, CLK=23/AUX1, MOSI=25/AUX2, MISO=12 sans
  pull-up) — **msp et n3pp ont la SD sans rien sacrifier** ; seul ffp5cs-sur-WROOM
  arbitre par env de build (futur env `wroom-sd`, à créer au besoin : SD contre US_POTA+AUX), RTC **DS3231** et **2-3 INA219/226**
  sur I2C (INA alimentés par `+3V3_SW`) ;
- le **230 V reste hors périmètre** : la carte `ffp5cs-wroom-prod-230v` existante
  demeure la variante secteur (sécurité, 2 oz, distances de fuite).

## Réalisation (rev 0.1 — générée)

```
generator/generate.py        Régénère schéma + PCB (non routé) + BOM + empreintes locales
generator/route_universal.py Pipeline de routage : secteur EN DUR (relais + PSU Hi-Link),
                             freerouting pour la logique, vias de couture GND, thermiques
generator/tidy_silkscreen.py / export_fab.py   Sérigraphie + gerbers normalisés
tools/check_pinmap_vs_firmware.py  Garde anti-dérive : 3 firmwares x 2 sites + topologies
kicad/n3-universal.*         Projet KiCad 8 (+ .kicad_dru : cuivre Mains >= 3 mm du reste)
```

- **Carte 278 × 120 mm, 2 oz** — zone secteur en bande haute (6 relais + coin PSU
  Hi-Link, fentes fraisées, frontière y71-73), logique en dessous, rangée de
  borniers en bande basse.
- **Sites A1 (WROOM 2×15) / A2 (S3-DevKitC-1 2×22)** — un seul module peuplé ;
  antennes dégagées (keepouts) ; entraxes à VERIFIER sur l'exemplaire réel.
- **Firmwares** : sections `PINMAP_UNIVERSAL` (msp 2.75, n3pp 4.71, ffp5cs 15.28),
  envs `esp32dev_universal_test` / `wroom-universal-test` / `wroom-s3-universal-test`
  (tous en CI). Garde anti-dérive machine sur les 4 combinaisons.
- **microSD** : slot unique — S3 natif par défaut (JP2/3/4 en 1-2), WROOM via
  le futur env `wroom-sd` (JP en 2-3 ; constantes SD déjà en place dans `pins.h`) ; MISO câblé en direct aux deux sites.
- **Profils d'alim par peuplement** : (a) 5 V jack/bornier ; (b) solaire 1S
  (TP4056+18650 hors carte, gate JP1 ôté, diviseur 100k/100k) ; (c) bus 12 V
  (J26 + P-FET + TVS + buck externe via J36/J37, diviseur 100k/27k) ;
  (d) secteur Hi-Link 20M05 (J27 + fusible T1A + varistance embarqués).

## Contenu

- `etude_pinmap.py` — **source de vérité** : union des connecteurs, nets partagés
  entre rôles (firmwares disjoints), affectations proposées WROOM + S3, et
  vérification machine (ADC1, entrée-seule, strapping, doublons, complétude).
- `ETUDE_PINMAP.md` — rapport généré (verdict + précautions par broche).
- `pinmap_universel_propose.json` — affectations proposées, consommables par un
  futur générateur et par les sections `PINMAP_UNIVERSAL` des trois firmwares.

## Verdict (2026-08-27)

- **WROOM (A1) : ✅ tient**, 22 nets, 1 broche libre (GPIO12), 2 précautions
  bénignes (GPIO2 OneWire, GPIO15 DHT — situations déjà pratiquées aujourd'hui).
- **S3 (A2) : ✅ tient en politique pragmatique**, 26 nets (dont microSD), 0 libre,
  4 broches à précaution : GPIO3 (gate, pull-up admissible), GPIO38/48 (LED RGB,
  scintillement cosmétique), **GPIO45 (AUX2 breakout : ne jamais y raccorder un
  module qui tire haut au boot)**. Variante « zéro précaution risquée » : ne pas
  embarquer AUX2 côté S3 (breakout seulement) → le caveat GPIO45 disparaît.
- La politique S3 **stricte** échoue exactement sur ces 4 broches : c'est la
  frontière du compromis, connue et documentée.

## Suite proposée (si go)

1. Sections `PINMAP_UNIVERSAL` dans les trois firmwares (mécanisme
   `PINMAP_S3_CARRIER` déjà en place ; envs `*-universal-test` en CI).
2. Générateur `generate.py` fusionné (blocs des générateurs existants),
   ~240×110 mm 1 oz, checker 3 firmwares × 2 sites.
3. Routage/validation/livrables via le pipeline habituel (seed tracks,
   via-blocks, tidy, export_fab).

## Profil d'alimentation « bus 12 V solaire » (ffp5cs)

Cas analysé : source solaire 12 V nominal (**10,5–15,5 V** réels, 14,5 V en charge)
alimentant pompes/chauffage/lumière, l'ESP et tous les périphériques.

- **Charges de puissance sur le bus 12 V en direct**, commutées par les *contacts*
  des relais (SRD : 10 A / 28 VDC — pompes 12 V OK, borniers NO/COM/NC inchangés).
- **Buck 12→5 V socketé (XL4015 5 A conseillé, LM2596 3 A mini)** pour l'électronique
  (~0,7 A continu, pics servos ~2,5 A). Option : bobines **SRD-12VDC** (même
  empreinte, variante BOM) sur le bus → le buck ne porte plus que logique+servos.
- **Bloc d'entrée obligatoire** : fusible lame 7,5–10 A, anti-inversion P-MOSFET,
  TVS 18 V, réservoir. (~4 composants, ~25×40 mm de surface.)
- **Instrumentation** : diviseur `ADC_VBAT` au ratio 12 V (~100k/27k — même net que
  msp/n3pp, seules les valeurs changent) ; INA219/226 OK à 14,5 V (limites 26/36 V),
  points de mesure panneau / batterie / pompes (shunt 0,01 Ω si branche > 3,2 A).
- **Délestage firmware** (profil 12 V uniquement) : ~12,0 V alerte, ~11,5 V coupe le
  chauffage, ~11,2 V la lumière, **pompe aquarium coupée en dernier** (support de vie).
- **Dimensionnement réel (batterie gel 12 V / 200 Wh, tout discontinu, light sleep
  ffp5cs — `PowerManager::goToLightSleep` + modem-sleep)** : plancher électronique
  ~0,25 W (dominé par le buck + AMS1117/LED du DevKit, PAS par l'ESP → choisir un
  buck à faible Iq, ex. MP1584 ~0,1 mA, plutôt que LM2596/XL4015 ~5-10 mA) ;
  budget ~22-42 Wh/j sans chauffage → 20-30 % de décharge/jour (durée de vie gel
  optimale), 2-4 j d'autonomie sans soleil, **panneau 50-80 Wc suffisant** (hiver).
  **Chauffage interdit sur batterie** (50 Wh/j pour 25 W×2 h) : autorisé uniquement
  en surplus solaire (bus > ~13,3 V). Délestage gel proposé : 12,4 V pré-alerte,
  12,2 V alerte + chauffage interdit, 11,9 V lumière + duty pompe réduit, 11,5 V
  duty minimal vital (poissons), LVD régulateur ~11 V. Un INA226 batterie permet
  un compteur de coulombs (état de charge réel, mieux que les seuils de tension).
  ⚠️ **Gel : absorption 14,1-14,4 V max, float 13,5-13,8 V** — une consigne 14,5 V
  assèche le gel (mort prématurée) : vérifier le profil GEL du régulateur.

Le bloc alim de la carte universelle a donc **trois profils de peuplement** sur les
mêmes empreintes : (a) 5 V direct, (b) solaire 1S TP4056+18650 (msp/n3pp),
(c) bus 12 V (ffp5cs solaire). Aucun impact sur le budget GPIO.

## Spécification finale gelée (2026-08-27) — attente du feu vert

Décisions actées avec l'utilisateur :
1. **230 V intégré** : les **6 canaux relais** sont construits au **standard de la
   carte 230 V** (zone secteur dédiée, fentes d'isolement fraisées, cuivre Mains
   ≥ 3 mm — règle `.kicad_dru` + contrôle géométrique indépendant —, pistes 2,5 mm
   / **2 oz**, routage secteur tracé en dur, sérigraphie DANGER). Un contact au
   standard 230 V commute aussi bien 12/24 V : les rôles solaires utilisent les
   mêmes relais sans jamais amener le secteur sur la carte.
2. **Alim secteur embarquée en option** : module **Hi-Link HLK-20M05 (5 V/3,6 A)
   socketé** + fusible + varistance sur carte — un seul cordon 220 V. Profils
   d'alim finaux (mêmes empreintes, peuplement par rôle) : (a) 5 V externe,
   (b) solaire 1S TP4056+18650 + `+3V3_SW`, (c) bus 12 V (fusible/P-FET/TVS/buck
   faible Iq), (d) secteur Hi-Link. Pas de fusible sur les circuits COMMUTÉS
   (protection au tableau + différentiel 30 mA), fusible sur l'entrée secteur
   de la carte elle-même.
3. **La carte remplace à terme les 3 cartes existantes** (qui restent gelées et
   commandables) — la maintenance converge sur n3-universal.

Notes de conception issues des décisions :
- Sur S3, K5/K6 reprennent les nets AUX (GPIO 48/45) : la topologie de commande
  relais (1 k base + pull-down 10 k) rend ces broches **sûres au boot** (comme
  GPIO2/15 sur les cartes actuelles) — les caveats LED/strapping s'adoucissent
  (LED RGB = simple recopie d'état du relais K5).
- Option SD-sur-WROOM (futur env ffp5cs `wroom-sd`) : sacrifie désormais US3 + K5/K6.
- Estimation : **~260×130 mm, 2 oz**, ~40-60 $ les 5 chez JLCPCB.
- Contexte élèves : zone secteur assemblée/raccordée sous supervision adulte,
  boîtier + presse-étoupes obligatoires à l'installation.
