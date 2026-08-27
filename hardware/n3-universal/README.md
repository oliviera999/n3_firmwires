# n3-universal — étude de faisabilité (étape 0)

Projet : **une carte porteuse commune** aux trois systèmes (msp station météo,
n3pp serre/élevage, ffp5cs aquaponie), bi-module (site A1 WROOM DevKit V1 /
site A2 ESP32-S3-DevKitC-1), 12/24 V, avec :
- rail capteurs commuté `+3V3_SW` + pont diviseur commuté (repris de
  `n3pp-msp-commun` rev 0.2), **jumper bypass fermé par défaut** (ffp5cs, toujours
  alimenté, ne pilote pas le gate) ;
- bloc solaire TP4056 + 18650 en option de peuplement (msp/n3pp) ;
- slot **microSD unique** câblé aux deux sites : natif côté S3, et côté WROOM sur des
  nets inutilisés par msp/n3pp (CS=14/US3, CLK=23/AUX1, MOSI=25/AUX2, MISO=12 sans
  pull-up) — **msp et n3pp ont la SD sans rien sacrifier** ; seul ffp5cs-sur-WROOM
  arbitre par env de build (`wroom-sd` : SD contre US_POTA+AUX), RTC **DS3231** et **2-3 INA219/226**
  sur I2C (INA alimentés par `+3V3_SW`) ;
- le **230 V reste hors périmètre** : la carte `ffp5cs-wroom-prod-230v` existante
  demeure la variante secteur (sécurité, 2 oz, distances de fuite).

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
- Ordre de grandeur : électronique 35–95 Wh/j ; une pompe 5 W continue = 120 Wh/j
  (dimensionnante) → typ. batterie 12 V 40–60 Ah + panneau 150–200 Wc pour l'hiver.

Le bloc alim de la carte universelle a donc **trois profils de peuplement** sur les
mêmes empreintes : (a) 5 V direct, (b) solaire 1S TP4056+18650 (msp/n3pp),
(c) bus 12 V (ffp5cs solaire). Aucun impact sur le budget GPIO.
