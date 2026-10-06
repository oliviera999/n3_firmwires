# n3-universal — étude de faisabilité (étape 0)

Projet : **une carte porteuse commune** aux trois systèmes (msp station météo,
n3pp serre/élevage, ffp5cs aquaponie), bi-module (site A1 WROOM DevKit V1 /
site A2 ESP32-S3-DevKitC-1), 12/24 V, avec :
- rail capteurs commuté `+3V3_SW` (rev 0.2 : **LDO dédié U1 LD1117V33** derrière le
  P-FET Q7 IRF4905, **jumper bypass JP1 fermé par défaut** — ffp5cs, toujours alimenté,
  ne pilote pas le gate) et pont diviseur VBAT **permanent** (R38 + JP12 22k/100k) ;
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
  arbitre par env de build (futur env `wroom-sd`, à créer au besoin : SD contre US_POTA+AUX), RTC **DS3231** et **3 INA226**
  sur I2C (INA alimentés par `+3V3_SW`) ;
- le **230 V reste hors périmètre** : la carte `ffp5cs-wroom-prod-230v` existante
  demeure la variante secteur (sécurité, 2 oz, distances de fuite).

> 🟢 **Rev 0.2 (2026-10-07) = version à commander** — régénérée de bout en bout (schéma,
> placement, routage, gerbers 2 oz) après la contre-vérification de la 0.1.2 (audit §13 :
> repères de sérigraphie décalés, J29/J30 sous le DevKit, Q7 NDP6020P introuvable). Ce qui
> change et pourquoi : tableau « 0.1.2 → 0.2 » ci-dessous ; contrôles : audit §14 ;
> dossier de fabrication : `exports/gerbers-n3-universal-v0.2.zip`.
>
> 📋 **Avant de commander** : lire **[`COMMANDE.md`](COMMANDE.md)** (options exactes du
> formulaire JLCPCB — dont le **2 oz obligatoire**, non porté par les gerbers — et procédure
> de devis PCB Maroc) et **[`AUDIT-2026-08-28.md`](AUDIT-2026-08-28.md)** (contre-vérification
> pré-commande : verdict, constats retenus, check-list rev 0.2). Fichiers d'assemblage
> éventuel : [`exports/pcba/`](exports/pcba/).
>
> 🧭 [`EVOLUTIONS_PROPOSEES.md`](EVOLUTIONS_PROPOSEES.md) : l'étude du sélecteur AUTO / ON,
> du retour d'état et de la connectique — **décisions actées le 2026-10-06 et appliquées
> en 0.2** (encadré en tête du document).

## Rev 0.2 — ce qui change par rapport à la 0.1.2 (2026-10-07)

Périmètre : **GPIO et firmwares strictement inchangés** (la garde
`tools/check_pinmap_vs_firmware.py` passe sans modification des trois `PINMAP_UNIVERSAL`),
zone 230 V conservée (relais, Hi-Link, fentes), tout en **traversant** (un seul pad CMS
optionnel : la variante SOD-123 de D11). Objectif : une carte qui se monte sans piège et
se répare avec ce qu'on trouve au Maroc / en Afrique (tournevis plat, résistances 1/4 W,
borniers KF301, TO-220 courants).

| Sujet | 0.1.2 | 0.2 | Pourquoi |
|---|---|---|---|
| Rail capteurs `+3V3_SW` | NDP6020P (logic-level, épuisé) commuté par le 3V3 du module | **Q7 IRF4905** (VGS = −5 V) + **U1 LD1117V33** (LDO TO-220, 800 mA) ; `JP1` = bypass | pièce courante, rail capteurs indépendant de l'AMS1117 du DevKit (DS18B20 + INA226 + RTC + SD) |
| Coupure du pont VBAT (Q9/Q10, BS250) | GPIO `ADC_VBAT_EN` | **supprimée** : pont permanent R38 100k + `JP12` (1-2 = 22k bus 12 V, 2-3 = 100k 1S) + R49/C13 + clamp **D11** | 60 µA en permanence < consommation de veille du module ; 4 pièces de moins |
| Entrée 5 V (J1/J2) | diode D5 seule | **Q12 IRF4905 anti-inversion** (chute ≈ 0,05 V) + **D12 TVS 1.5KE6.8A** | inversion de polarité et surtensions de blocs secteur bas de gamme |
| TVS 12 V D8 | P6KE18A | **1.5KE18A** (1500 W) | bus solaire 12 V exposé |
| Forçage manuel des relais | — | **JP5-JP10** (1×03 : 1 = ON, 2 = commun, 3 = AUTO) + R61-R66 ; **JP7 (chauffage) sans position ON** | dépannage sur site sans PC ; chauffage jamais forcé |
| Retour d'état commandes | — (PCF8574 DNP envisagé) | **J38 « CMD SENSE »** 1×08 : REL1-6_SW (0/5 V) + 5V + GND | lisible au multimètre, extensible, rien à poser |
| Connectique signal | JST-XH (HC-SR04, DHT) + borniers 5,08 | **borniers à vis 5,08 partout** (bornier-4 pour les HC-SR04, ordre `5V TRIG ECHO GND`) | un seul type, approvisionnement local, fils 0,2-2,5 mm² |
| Repères de sérigraphie | décalés d'une rangée (K1-K3) | chaque repère **dans le corps de son composant**, garde `tools/check_silk_refs.py` en CI | montage par des élèves, pas d'erreur de canal |
| J29/J30 | sous le DevKit V1 | rangée basse, couloirs USB des modules libres (`check_pcb_clearance.py`, `offset`) | — |
| Transistors TO-92 | pas 1,27 (perçage 0,75) | **pas 2,54 en ligne**, perçage 1,0, brochage `C B E` sérigraphié | soudure facile, BC337/2N2222/S8050 |
| Fusible F1 | clips 5×20 seuls | **empreinte universelle** : clips (pas 22,5-22,6) **ou** porte-fusible à capot BLX-A (pas 22,0) sur fentes 3 mm | capot = pas de 230 V accessible |
| Isolement 230 V | plan GND à 6,5 mm, clearance 3 mm | idem + **règle `mains_creepage` 6,4 mm** (DRU, moteur creepage KiCad 10) + mini-fentes COM allongées à y83 | ligne de fuite mesurée, plus estimée |
| Fabrication | 2 oz à cocher à la main | 2 oz **porté par l'empilage** (`gbrjob`) ; HAL sans plomb | le fabricant lit l'épaisseur dans le job |
| Mécanique | 5 trous M3, 278 × 120 | **7 trous M3** (H1/H5 nylon), **278 × 135 mm**, points de test TP1-TP4 (3V3, 3V3_SW, 5V, GND) | montage sur plaque, mesure au multimètre |
| Carte D11 | BAT85 DO-35 | **empreinte double** DO-35 **ou** SOD-123 (BAT43W C19167) | BAT85 traversant épuisé chez LCSC |

### Cavaliers et profils (livraison : tout en position par défaut)

| Cavalier | Fonction | Défaut | Autre position |
|---|---|---|---|
| JP1 | bypass du gate rail capteurs (header 1×03, broche 3 = parking) | **1-2 fermé** (ffp5cs : rail permanent) | parqué en 2-3 : rail coupé par GPIO (msp/n3pp) |
| JP2 / JP3 / JP4 | SD CS / CLK / MOSI | **1-2 = S3** | 2-3 = WROOM (env `wroom-sd`) |
| JP11 | SD MISO | **1-2 = S3** | 2-3 = WROOM |
| JP5…JP10 | forçage relais K1…K6 | **2-3 = AUTO** | 1-2 = ON (sauf JP7 : pas de ON) |
| JP12 | diviseur VBAT | **1-2 = 22k** (bus 12 V) | 2-3 = 100k (1S Li-ion) |
| JP13 / JP14 / JP15 | diviseurs ECHO HC-SR04 (5 V → 3,3 V) | **fermé** (HC-SR04 5 V) | ôté : capteur 3,3 V / sortie open-collector |
| JP16…JP19 | diviseurs ADC A…D | **ôté** (signal ≤ 3,3 V direct) | fermé : signal 0-5 V |
| JP20 | diviseur ADC_E | **ôté** | fermé : 0-5 V |
| JP21 | pull-up DHT externe | **ôté** | fermé : DHT sur bornier J30 sans pull-up |

Profils d'alimentation (par peuplement, inchangés) : (a) 5 V jack/bornier via Q12 ;
(b) 1S solaire : boost externe sur J1/J2 ; (c) bus 12 V : J26 → Q11/D8/D9 → buck externe
J36/J37 → J1 ; (d) secteur : PS1 Hi-Link + F1 + RV1.

## Réalisation (rev 0.2 — KiCad 10, chaîne régénérée)

```
generator/generate.py        Source de vérité : composants, nets, placement, schéma, BOM,
                             empreintes locales, règles (.kicad_dru), fp-lib-table, libellés
                             (SCH_TEXTS / PCB_TEXTS), approvisionnement (SOURCING).
                             --regen-pcb réécrit le PCB (non routé)
generator/route_universal.py Routage : secteur EN DUR (relais + PSU), freerouting 2.1 pour la
                             logique, ponts A/A' du DevKit, zones GND, vias de couture,
                             contrôles check_mains_gap (3 mm) et check_mains_plane_gap (6,5 mm)
generator/tidy_silkscreen.py Écarte libellés puis repères des pads, fentes, contours ET des
                             courtyards des autres composants
generator/export_fab.py      DRC bloquant (parité schéma comprise) puis gerbers 7 couches +
                             perçages + gbrjob (empilage 2 oz) + PDF/SVG
generator/export_assembly_preview.py  BOM/CPL JLCPCB + NextPCB (prévisualisation PCBA)
generator/finalize_board.py  ⛔ obsolète (retouches 0.1 → 0.1.2), conservé pour l'historique
tools/check_pinmap_vs_firmware.py  Garde anti-dérive : 3 firmwares x 2 sites + topologies
tools/check_pcb_clearance.py       Corps 3D (courtyards) + couloirs d'insertion (USB, borniers)
tools/check_silk_refs.py           Chaque repère/valeur dans le corps de SON composant
tools/annotate_pcb_roles.py        Rôle par firmware dans le champ Description des empreintes
kicad/n3-universal.*         Projet KiCad 10 (+ .kicad_dru : clearance 3 mm, creepage 6,4 mm,
                             plans à 6,5 mm du secteur ; fp-lib-table : lib locale n3u)
```

Chaîne complète (Linux : `python3` + Python de KiCad `/usr/bin/python3.12` ; Windows :
`"C:\Program Files\KiCad\10.0\bin\python.exe"`), `freerouting-2.1.0.jar` requis :

```
cd generator
python3 generate.py --regen-pcb                  # schéma + BOM + PCB non routé + .kicad_dru
<python KiCad> route_universal.py --jar ~/freerouting.jar   # routage + zones + contrôles 3 / 6,5 mm
<python KiCad> tidy_silkscreen.py                # libellés / repères hors pads, fentes, corps voisins
python3 ../tools/check_pinmap_vs_firmware.py ; python3 ../tools/check_pcb_clearance.py
python3 ../tools/check_silk_refs.py ; python3 ../tools/annotate_pcb_roles.py --check
python3 export_fab.py --rev 0.2                  # DRC (0 erreur exigée) puis gerbers v0.2
python3 export_assembly_preview.py
```

État rev 0.2 : __ETAT_02__

- **Carte 278 × 135 mm, 2 oz** — zone secteur en bande haute (6 relais + coin PSU
  Hi-Link, fentes fraisées), logique en dessous, rangée de borniers en bande basse.
  La frontière fraisée **y71-73 n'existe qu'au coin PSU** : dans la bande relais les
  broches de contact 230 V descendent jusqu'à y79,5, et c'est le plan GND repoussé à
  **y86** (rev 0.1.1, `SEC-CRP-01`) + les mini-fentes autour de chaque COM qui tiennent
  l'isolement (écart cuivre mains ↔ logique mesuré : 3,45 mm ; mains ↔ bord des plans
  coulés ≥ 6,50 mm, même couche — règle `.kicad_dru` + `check_mains_plane_gap`).
- **Sites A1 (WROOM 2×15) / A2 (S3-DevKitC-1 2×22)** — un seul module peuplé ;
  antennes dégagées (keepouts) ; **entraxes ET ordre des broches à VERIFIER sur
  l'exemplaire réel** avant de souder les supports (certains clones « DevKit V1
  30 broches » sont en 27,9 mm au lieu de 25,4, ou intervertissent des broches :
  comparer les 30 étiquettes du module à celles de la carte).
- **Firmwares** : sections `PINMAP_UNIVERSAL` (msp 2.75, n3pp 4.72, ffp5cs 15.29),
  envs `esp32dev_universal_test` / `wroom-universal-test` / `wroom-s3-universal-test`
  (tous en CI). Garde anti-dérive machine sur les 4 combinaisons.
  **OTA** : ces envs lisent leurs **propres** métadonnées (ffp5cs 15.31, n3pp 4.74,
  msp 2.77), jamais le canal des cartes historiques — cf. `docs/WIFI_OTA_REFERENCE.md`,
  « OTA par câblage ». Bancs flashés avant ces versions : reflasher en USB.
- **microSD** : slot unique — S3 natif par défaut (JP2/3/4 **et JP11** en 1-2), WROOM via
  le futur env `wroom-sd` (JP en 2-3 ; constantes SD déjà en place dans `pins.h`) ; depuis la
  0.2 le MISO passe aussi par un cavalier (JP11) : plus de ligne partagée entre les deux sites.
- **Profils d'alim par peuplement** : (a) 5 V jack/bornier ; (b) solaire 1S
  (TP4056+18650 hors carte, gate JP1 ôté, diviseur 100k/100k) ; (c) bus 12 V
  (J26 + P-FET + TVS + buck externe via J36/J37, diviseur 100k/22k) ;
  (d) secteur Hi-Link 20M05 (J27 + fusible T1A + varistance embarqués).
  ⚠️ **Profil (b) : injecter le 5 V par J1/J2 via un module boost — JAMAIS la
  batterie 1S brute** (3,0-4,2 V) : la carte n'a pas d'entrée 3V3 (J19 est en aval
  de U1), et l'AMS1117 du DevKit comme le LD1117 passeraient en dropout → brownouts.
  Mesure de tension batterie sur J25, pas sur l'entrée d'alim. Depuis la 0.2 l'entrée
  5 V traverse Q12 (IRF4905, chute ≈ 50 mV sous 1 A) : une inversion de polarité
  sur J1/J2 ne fait rien, une surtension est écrêtée par D12 (1.5KE6.8A, 10,5 V).

## Contenu

- `etude_pinmap.py` — **source de vérité** : union des connecteurs, nets partagés
  entre rôles (firmwares disjoints), affectations proposées WROOM + S3, et
  vérification machine (ADC1, entrée-seule, strapping, doublons, complétude).
- `ETUDE_PINMAP.md` — rapport généré (verdict + précautions par broche).
- `pinmap_universel_propose.json` — affectations proposées, consommables par un
  futur générateur et par les sections `PINMAP_UNIVERSAL` des trois firmwares.
- `COMPOSANTS_FFP5CS.csv` — tableau annoté du **rôle ffp5cs** (connecteurs,
  GPIO WROOM/S3, constantes `Pins::`, clés serveur, pose / DNP).

### Connaître le rôle d'un composant depuis KiCad

Le champ **Description** de chaque composant porte sa description générique **+ son rôle
dans les trois firmwares** (`ROLES` de `generator/generate.py`, recopié de
`COMPOSANTS_FFP5CS.csv` et des blocs `PINMAP_UNIVERSAL` de msp/n3pp) :

```
J7 : HC-SR04 AQUA (1=5V 2=TRIG 3=ECHO 4=GND)
     — ROLE ffp5cs : HC-SR04 niveau aquarium (ULTRASON_AQUA)
       (msp : net US1 = PLUIE côté msp — poser J29, pas J7 ; n3pp : non utilisé)
```

- **Schéma (eeschema)** : double-clic sur le symbole → le champ apparaît dans la grille ;
  vue d'ensemble par *Édition → Modifier les champs des symboles*.
- **PCB (pcbnew)** : double-clic sur l'empreinte → onglet des champs. Les 131 empreintes
  sont renseignées ; avant, seule la description générique de la bibliothèque KiCad
  (« Resistor, Axial_DIN0207 series… ») était visible.
- Après avoir modifié `ROLES` : régénérer le schéma (`gen_schematic()` seul) puis
  `python3 tools/annotate_pcb_roles.py` — le PCB **routé** n'est pas régénéré, seule la
  ligne `Description` de chaque empreinte est réécrite. `--check` (en CI) refuse la dérive.

## Verdict (2026-08-27)

- **WROOM (A1) : ✅ tient**, 22 nets, 1 broche libre (GPIO12), 2 précautions
  bénignes (GPIO2 OneWire, GPIO15 DHT — situations déjà pratiquées aujourd'hui).
- **S3 (A2) : ✅ tient en politique pragmatique**, 26 nets (dont microSD), 0 libre,
  4 broches à précaution : GPIO3 (gate du rail capteurs, R35 → rail OFF au boot), GPIO38/48 (LED RGB,
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
  des relais (SRD **Form C : 7 A / 28 VDC résistif et 3 A inductif réels** — le 10 A
  du catalogue ne vaut qu'en 125 VAC ou pour un Form A sans contact NC ; pompes 12 V
  OK, borniers NO/COM/NC inchangés).
- **Buck 12→5 V socketé (XL4015 5 A conseillé, LM2596 3 A mini)** pour l'électronique
  (~0,7 A continu, pics servos ~2,5 A). Option : bobines **SRD-12VDC** (même
  empreinte, variante BOM) sur le bus → le buck ne porte plus que logique+servos.
- **Bloc d'entrée obligatoire** : fusible lame 7,5–10 A, anti-inversion P-MOSFET,
  TVS 18 V, réservoir. (~4 composants, ~25×40 mm de surface.)
- **Instrumentation** : diviseur `ADC_VBAT` au ratio 12 V (**100k/22k** — même net que
  msp/n3pp, seules les valeurs changent ; pleine échelle ~17,2 V : l'ancien 100k/27k
  saturait à ~14,6 V, sous l'absorption AGM 14,4-14,7 V) ; **3 × INA226** (VBUS ≤ 36 V :
  OK panneau à vide ~22-25 V ; INA219 26 V déconseillé), voies panneau (0x40) /
  batterie (0x41) / charges (0x44). Pleine échelle shunt INA226 = **81,92 mV** :
  module R100 (0,1 Ω) = 0,82 A, R010 = 8,2 A, 5 mΩ = 16,4 A → **shunts externes
  5-10 mΩ sur les 3 voies** (panneau 50-80 Wc ≈ 3-5 A : 10 mΩ ; batterie / charges
  derrière fusible 7,5-10 A : 5 mΩ, ≥ 1 W), fils de mesure **Kelvin**, R100 du module
  dessoudé. À valider sur le banc `energie/` (commandes `scan` / `dump` / `cal`).
- **Délestage firmware** (profil 12 V uniquement) : échelle unique ci-dessous
  (« Délestage AGM »), **pompe aquarium coupée en dernier** (support de vie).
- **Dimensionnement réel (batterie AGM 12 V / 12 Ah ≈ 144 Wh — capacité paramétrable,
  cf. `ENERGIE_BAT_CAPACITY_AH` du banc —, tout discontinu, light sleep
  ffp5cs — `PowerManager::goToLightSleep` + modem-sleep)** : plancher électronique
  ~0,25 W (dominé par le buck + AMS1117/LED du DevKit, PAS par l'ESP → choisir un
  buck à faible Iq, ex. MP1584 ~0,1 mA, plutôt que LM2596/XL4015 ~5-10 mA) ;
  budget ~22-42 Wh/j sans chauffage → ~15-30 % de décharge/jour (durée de vie AGM
  correcte), ~1,7-3,3 j sans soleil jusqu'à 50 % de décharge, **panneau 50-80 Wc
  suffisant** (hiver).
  **Chauffage interdit sur batterie** (50 Wh/j pour 25 W×2 h) : autorisé uniquement
  en surplus solaire (bus > ~13,3 V). Délestage AGM proposé (tensions **sous
  charge**, seuils plomb génériques) : 12,4 V pré-alerte, 12,2 V alerte + chauffage
  interdit, 11,9 V lumière + duty pompe réduit, 11,5 V duty minimal vital (poissons),
  LVD régulateur ~11 V (au repos, 11,8 V ≈ 0 % sur la table OCV AGM de `n3_power`). Un INA226 batterie permet
  un compteur de coulombs (état de charge réel, mieux que les seuils de tension).
  ⚠️ **AGM : absorption 14,4-14,7 V (selon fabricant, compensée en température),
  float 13,5-13,8 V** — régler le profil **AGM / SEALED** du régulateur (jamais
  d'égalisation « FLOODED », qui dessèche une AGM). Régulateur pas encore choisi :
  exiger ce profil.

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
  ⚠️ Avant d'utiliser cette option, **dépeupler R28 et R31** (bases de Q5/Q6) ou
  débrancher toute charge des borniers K5/K6 : SD_CLK et SD_MOSI restent câblés aux
  drivers des relais, et le trafic SPI ferait claquer K5/K6 pendant chaque accès SD.
- Estimation : **~260×130 mm, 2 oz**, ~40-60 $ les 5 chez JLCPCB.
- Contexte élèves : zone secteur assemblée/raccordée sous supervision adulte,
  boîtier + presse-étoupes obligatoires à l'installation.

## Audit final rev 0.1 (2026-08-27) — corrections et consignes de pose

Un audit exhaustif (netlist, strapping, géométrie secteur, contrats serveur) a
corrigé la carte et les firmwares AVANT toute commande :

**Corrections carte (intégrées au PCB routé + générateur) :**
- **J2 (jack 5,5/2,1)** : rotation 270° pointait l'ouverture vers J1 (enfichage
  bloqué). Passé à **0°** — ouverture au bord gauche, lèvre ~6 mm hors carte ;
  pad TIP/+5 V inchangé (48, 116). Polarité centre-positif inchangée.
  Le défaut est désormais couvert par `tools/check_pcb_clearance.py` (corps 3D +
  couloirs d'enfichage, en CI) : le garde-fou du générateur ne comparait que les
  **pads** et ne pouvait pas le voir — cf. `GEN-08`.
- **Q11 (anti-inversion 12 V)** était câblé au brochage BS250 (D-G-S) : l'entrée
  12 V arrivait sur la **grille** du NDP6020P (TO-220 : 1=G 2=D 3=S) — profil bus
  12 V inopérant. Recâblé comme Q7 (G/D/S), entrée sur le drain.
  **Rev 0.1.2** : le NDP6020P (VGS max ±8 V) y aurait vu VGS = −VBAT (−12 à −14,7 V) ;
  Q11 devient un **IRF4905PBF** (55 V, VGS ±20 V, même brochage G-D-S) et la zener
  **D9 1N4744A** (15 V, cathode = source, anode = grille, R40 limite son courant) borne
  VGS face aux transitoires du bus. Q7 reste un NDP6020P (logic-level requis à 3,3 V).
- **Rev 0.1.2** : **RV1** passe sur sa vraie empreinte (disque 10 mm, pas 7,5 mm,
  perçage 1,0 mm) au lieu d'un électrolytique au pas 5 mm (`GEN-04`) ; trou **H5**
  (vis nylon) ajouté entre les pistes N et L du coin secteur (`GEN-05`).
- **Coin PSU 230 V re-routé** : l'ancien tracé faisait passer L à 0,75 mm du pad
  neutre de J27 et N ∥ LF à 1,5 mm. Nouveau tracé : écarts L↔N↔LF ≥ 3 mm hors
  pas propre des composants ; **J27 devient 1=N / 2=L** (repères sérigraphiés).
- **Plan GND exclu du coin PSU** (246..318 × 40..73) : il coulait sous les
  pistes/pads 230 V (à 3,0 mm même couche, recouvrement en projection). Le
  contrôle indépendant `check_mains_gap` intègre désormais les **zones remplies**.
- **H1 = VIS NYLON obligatoire** (marquage sérigraphié) : le trou du coin relais
  est enclavé par K1, une tête métal serait à ~4,3 mm du 230 V ; le plan GND est
  écarté sous la tête (keepout).

**Consignes de pose par profil (résistances étoilées `*` de la BOM) :**
- **R17/R18/R19 (2k, ponts écho HC-SR04)** : à poser **uniquement sur les unités
  ffp5cs**. Sur une unité msp, elles verrouillaient PLUIE (US1) à « mouillé » et
  rendaient le DHT externe (US2) illisible (niveau haut ~0,55 V). R19 absent
  aussi si option `wroom-sd` (sinon SD_CS reste tiré bas pendant un reset).
- **R27 (10k, bas de pont ADC_E)** : à poser pour une **LDR** (n3pp/ffp5cs) ;
  **absent** pour un module à sortie AO (msp HumiditeSol — atténuation ~50 %
  sinon). Module AO 3 fils : prendre le GND sur un bornier voisin (J12 n'a que
  3V3_SW/ADC_E).
- **JP1 en 1-2** = rail `+3V3_SW` permanent (unités ffp5cs) ; ôté = rail commuté
  par GPIO13 (msp/n3pp). La broche 3 de JP1 n'est pas câblée.

**microSD et strapping GPIO12/MTDI (WROOM)** — le slot est sans risque sur une
unité **S3** (site A1 vide). Sur une unité **WROOM** : module **3,3 V direct,
sans régulateur ni tampon 74LVC125** (PAS de module « Catalex »), et **griller
d'abord l'efuse** `espefuse.py set_flash_voltage 3.3V` (sinon une carte SD
insérée peut tirer MTDI haut au boot → strap flash 1,8 V, démarrage impossible).
À flasher par USB sur une unité ffp5cs-WROOM, **ôter JP1** (le pull-up OneWire
R24 sur GPIO2, alimenté par +3V3_SW permanent, bloque sinon l'entrée en
bootloader UART).

**Contrats serveur (corrigés côté firmwares)** : clés JSON des actionneurs
découplées des broches physiques — n3pp v4.72 (`POMPE_OUTPUT_KEY="12"`),
ffp5cs v15.29 (clés Ffp3GpioMap 16/18/2/15/23/25 restaurées dans
`gpio_mapping.h`). Sans cela, la carte universelle croisait pompe/UV/chauffage.

**Marges connues (acceptées, documentées)** : TVS D8 P6KE18A (Vrwm 15,3 V) au
ras du haut de plage bus 12 V (15,5 V) — acceptable car la batterie plomb tient
le bus ; en cas d'égalisation > 15 V fréquente, monter une P6KE20A (et C5 35 V).
Le net `+3V3` amont (0,4 mm) est court et ne porte que le courant du rail
capteurs (~0,1 Ω en 2 oz) ; les modules socketés embarquent leur découplage.
