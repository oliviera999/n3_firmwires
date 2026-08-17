# Tuto — comprendre et maîtriser nos PCB

Ce guide explique **comment sont construites les trois cartes** du dossier
`hardware/` et **ce que signifie chaque paramètre** — du sandwich de matière
jusqu'aux réglages du formulaire JLCPCB. Il est pensé pour être lu une fois en
entier, puis servir de référence. Tous les exemples viennent de **nos** cartes :

| Carte | Rôle | Taille | Particularité |
|---|---|---|---|
| [`n3pp-msp-commun`](n3pp-msp-commun/) | serre n3pp **ou** météo msp (un seul PCB, 2 profils d'assemblage) | 210×105 mm | 6 relais 12/24 V, union disjointe de 2 firmwares |
| [`ffp5cs-wroom-prod`](ffp5cs-wroom-prod/) | aquaponie ffp5cs | 150×100 mm | 4 relais 12/24 V, 3 capteurs ultrason |
| [`ffp5cs-wroom-prod-230v`](ffp5cs-wroom-prod-230v/) | aquaponie, charges secteur | 234×110 mm | **zone 230 V isolée**, 6 relais, cuivre 2 oz |

---

## 1. Un PCB, physiquement, c'est quoi ?

Un circuit imprimé 2 couches est un **sandwich** :

```
   ── sérigraphie avant (encre blanche : textes, repères)      « F.Silkscreen »
   ── vernis épargne avant (le vert)                           « F.Mask »
   ══ CUIVRE AVANT (pistes + plan de masse)                    « F.Cu »
   ▓▓ substrat FR-4 (fibre de verre + époxy, isolant)          1,6 mm
   ══ CUIVRE ARRIÈRE                                           « B.Cu »
   ── vernis épargne arrière                                   « B.Mask »
   ── sérigraphie arrière                                      « B.Silkscreen »
```

- **FR-4** : le matériau standard, rigide et isolant. Son épaisseur (1,6 mm chez
  nous) donne la tenue mécanique — indispensable avec des borniers à vis qu'on
  serre et un DevKit qu'on enfiche/retire.
- **Cuivre** : une feuille laminée sur chaque face, dont la gravure chimique ne
  laisse que les pistes et les plans. Son épaisseur se donne en onces par pied
  carré : **1 oz ≈ 35 µm** (standard), **2 oz ≈ 70 µm** (carte 230 V, voir §4).
- **Vernis épargne** (soldermask) : la couche colorée qui recouvre tout le
  cuivre **sauf** les pastilles. Il évite les ponts de soudure et l'oxydation.
  Sa couleur est purement cosmétique — on prend vert : le moins cher, le plus
  rapide, et le meilleur contraste pour lire la sérigraphie.
- **Sérigraphie** (silkscreen) : l'encre imprimée par-dessus. Chez nous elle
  porte deux familles de textes (voir §5) : les **repères** (`J3`, `K1`, `R21`…)
  et les **étiquettes de câblage** (`NO COM NC`, `ADC A 33`, `ZONE 230V`…).
- **Finition de surface** : ce qui protège le cuivre nu des pastilles jusqu'à la
  soudure. **HASL** = trempage dans l'étain (au plomb : le plus facile à souder
  à la main, notre choix). **ENIG** = nickel-or chimique, plat et inoxydable,
  plus cher — pertinent seulement en milieu très humide.

Les fichiers envoyés au fabricant (« **Gerbers** ») décrivent chacune de ces
couches, plus deux fichiers de **perçage** (Excellon) : `PTH` pour les trous
**métallisés** (composants, vias — le cuivre tapisse le trou et relie les deux
faces) et `NPTH` pour les trous **nus** (nos 4 trous de fixation M3 : pas de
cuivre, la vis ne doit rien court-circuiter).

## 2. Lire nos cartes : pistes, plans, vias

### Largeur de piste = courant admissible

Une piste, c'est une résistance : trop fine pour son courant, elle chauffe.
La largeur nécessaire croît avec le courant **et** décroît avec l'épaisseur de
cuivre. Nos règles (inscrites dans les *netclasses* du projet KiCad, §3) :

| Classe | Largeur | Sert à | Courant typique |
|---|---|---|---|
| `Default` | 0,4 mm | signaux logiques (GPIO, I2C, data capteurs) | < 50 mA |
| `Alim` | 1,2 mm | +5V, VIN, GND, +3V3 | ~1-2 A (pointes relais + servos + WiFi) |
| `Relais` | 2,0 mm | contacts NO/COM/NC des relais 12/24 V | quelques A |
| `Relais` (230 V) | **2,5 mm, tracées à la main** | contacts 230 V | jusqu'à 10 A |

Ordre de grandeur à retenir (IPC-2152) : en 1 oz, une piste de 2,5 mm à 10 A
s'échauffe de **~55-60 °C** ; en 2 oz on retombe vers **~20 °C**. C'est *la*
raison du cuivre 2 oz sur la carte 230 V — et de son inutilité ailleurs.

### Plan de masse et vias de couture

Sur nos cartes, tout le cuivre « libre » des deux faces est rempli en **plan de
masse** (GND). Avantages : retours de courant courts, blindage, et moins de
gravure. Deux subtilités que notre pipeline gère automatiquement :

- **Vias de couture** (*stitching*) : ~90-110 petits vias GND répartis en grille
  relient les plans avant/arrière, pour que la masse soit partout à basse
  impédance et qu'aucune « poche » ne reste isolée. Les vias orphelins (dans une
  poche coupée du reste) sont détectés et supprimés.
- **Reliefs thermiques** : une pastille GND noyée dans le plan serait une
  éponge à chaleur impossible à souder. Elle est donc reliée par 4 petits rayons
  (*thermal relief*). Quand le DRC signale un pad « affamé » (rayons coupés par
  des pistes voisines), le pipeline le passe en connexion pleine.
- **Îlots supprimés** : un morceau de plan qui ne touche plus GND est retiré
  (sinon il flotte électriquement et le DRC le signale).

### Vias

Un via = un trou métallisé qui fait passer une piste d'une face à l'autre.
Les nôtres : **perçage 0,35 mm, pastille 0,7 mm** (0,3/0,5 sur la carte 230 V),
soit une couronne de cuivre confortable au-dessus des minima JLCPCB (0,3/0,4).

### La zone antenne — le seul endroit SANS cuivre

L'ESP32 DevKit a son antenne WiFi imprimée en bout de module. **Tout cuivre
sous une antenne la détune** (recommandation Espressif). Chaque carte a donc
une zone interdite (*keepout*) sous le débord d'antenne : pas de piste, pas de
via, pas de plan — et une sérigraphie le rappelle. À l'assemblage : l'antenne
du DevKit doit pointer vers cette zone dégagée (sérigraphies `ANTENNE` / `USB`,
il n'y a **pas** de détrompeur mécanique).

## 3. Où vivent les paramètres dans le projet KiCad

Ouvre `kicad/<carte>.kicad_pro` (ou Fichier → Réglages de la carte dans KiCad) :

- **Netclasses** (`net_settings`) : c'est là que « toute piste du net `REL3_NO`
  fait 2 mm » est déclaré. Une *netclass* associe largeur de piste, isolement
  (*clearance*) et taille de via à une famille de nets (motifs `REL*_COM`,
  `+5V`…). C'est le bon endroit pour changer une règle — pas piste par piste.
- **Règles de conception** (`design_settings.rules`) : les minima globaux que le
  DRC fait respecter — isolement 0,2 mm, piste mini 0,25 mm, via mini 0,5/0,3.
  Nos valeurs sont volontairement **au-dessus** des capacités JLCPCB
  (0,127 mm) : marge = fabrication sans stress.
- **Règles personnalisées** (`.kicad_dru`, carte 230 V uniquement) : un fichier
  de règles avancées. Le nôtre impose **≥ 3 mm entre tout cuivre de classe
  `Mains` et tout autre cuivre** — la distance d'isolement pour du 230 V en
  environnement humide. Le DRC la vérifie, et un contrôle géométrique Python
  **indépendant** la revérifie (deux ceintures).

### ERC et DRC — les deux filets de sécurité

- **ERC** (*Electrical Rules Check*, sur le **schéma**) : broches non
  connectées, sorties en conflit, alims non pilotées… Nos schémas : **0 erreur**.
- **DRC** (*Design Rules Check*, sur le **PCB**) : isolements violés, pastilles
  non connectées au réseau, courtyards qui se chevauchent, textes hors carte…
  Nos trois cartes : **0 violation, 0 pastille non connectée**.

À ces deux contrôles standard s'ajoute notre garde maison
`tools/check_pinmap_vs_firmware.py` : il relit **le code des firmwares**
(`#define RELAIS 13`…) et vérifie que chaque GPIO arrive à la bonne broche
physique du DevKit dans le PCB généré, avec la bonne topologie de bloc
(bornier relais câblé NO/COM/NC, pull-up sur la bonne ligne…). Le plan ne peut
pas dériver du code sans casser la CI locale.

## 4. L'électronique des blocs, expliquée

Chaque bloc répond à une contrainte précise de l'ESP32 ou du périphérique.

### Canal relais (×6 ou ×4 selon carte)

```
GPIO ──[R 1k]──┬── base BC337 ── collecteur ──> bobine relais ──> +5V
               │        │                        │ (1N4007 en roue libre)
            [R 10k]   émetteur ── GND        LED témoin + R 1k
             pull-down
```

- Un GPIO ESP32 fournit ~12 mA max ; la bobine du relais SRD-05 en demande
  ~72 mA → le **transistor BC337** fait l'amplification (le 1 kΩ limite le
  courant de base).
- La **1N4007 en roue libre** absorbe la pointe de tension inverse générée par
  la bobine à la coupure — sans elle, le transistor meurt vite.
- Le **pull-down 10 kΩ** maintient le transistor bloqué pendant le boot, quand
  le GPIO n'est pas encore piloté : le relais ne claque jamais tout seul à la
  mise sous tension. C'est vital sur GPIO12 (broche de *strapping*, voir §6).
- Le **bornier NO/COM/NC** expose le contact : NO = fermé quand le relais est
  activé, NC = fermé au repos. Contrôle à l'ohmmètre avant tout branchement.

### Entrées analogiques (pont diviseur)

L'ADC de l'ESP32 lit 0-3,3 V. Deux montages selon le capteur :

- **LDR** (photorésistance) : LDR entre 3V3 et l'entrée + **10 kΩ vers GND** =
  pont diviseur, la tension varie avec la lumière. Le 10 kΩ est **sur la
  carte**, peuplé selon le profil.
- **Capteur à sortie AO** (humidité sol capacitif…) : il fournit déjà une
  tension → pas de résistance (elle fausserait la mesure, DNP).
- **Batterie** : pont **2,2 kΩ / 2,2 kΩ** (valeurs exactes de `n3_defaults.h`,
  utilisées par `n3_battery`) qui divise la tension par 2 → GPIO36.

### Pull-ups de bus

- **I2C (4,7 kΩ sur SDA et SCL)** : le bus I2C fonctionne en collecteur ouvert,
  personne ne « pousse » le niveau haut — les pull-ups le font. Une seule paire
  pour tout le bus (OLED + 3 ports libres), bus < ~50 cm.
- **1-Wire (4,7 kΩ)** : même principe pour le DS18B20.
- **DHT (10 kΩ)** : ligne data à l'état haut au repos.

### Découplage et alimentation

- **1000 µF** en réservoir sur le rail 5 V : quand un relais claque ou que le
  WiFi émet (pointes de 300-500 mA), c'est le condensateur qui fournit,
  pas le câble d'alim. **470 µF** dédié aux servos (msp) pour la même raison.
- **100 nF céramique** près des capteurs et du bus I2C : court-circuite les
  parasites haute fréquence localement.
- **Schottky 1N5822** entre le +5 V externe et VIN du DevKit : si l'USB du
  DevKit et l'alim 5 V sont branchés **en même temps** (flash/debug), la diode
  empêche les deux sources de se refouler l'une dans l'autre.

## 5. La sérigraphie : des règles à connaître

Deux familles de textes, aux rôles complémentaires :

- le **repère** (`J3`, `K4`, `R21`) fait le lien avec `BOM.csv` et les feuilles
  d'assemblage : c'est le texte de celui qui **soude** ;
- l'**étiquette** (`NO COM NC`, `DHT n3pp`, `ZONE 230V - DANGER`) est placée
  près du connecteur qu'elle décrit : c'est le texte de celui qui **câble**.

Trois règles, apprises en préparant la commande (et maintenant automatisées) :

1. **Minima fabricant** : JLCPCB exige ≥ 1 mm de hauteur et ≥ 0,15 mm de trait,
   sinon leur contrôle floute ou **supprime** le texte. Nos générateurs
   imposent 1 mm / 0,16 mm (`SILK_MIN_H` / `SILK_RATIO`). Les fins contours de
   composants à 0,12 mm sont le standard de la bibliothèque KiCad, acceptés.
2. **Jamais de texte sur une pastille** : le fabricant efface l'encre qui
   déborde sur le cuivre nu — le texte devient illisible.
3. **Jamais deux textes superposés** : `generator/tidy_silkscreen.py` écarte le
   *repère* (court, recasable) et ne touche jamais l'*étiquette* (sa position
   porte le sens). Il est déterministe et idempotent, et audite ce qui reste.

Dernier détail : le marqueur **`JLCJLCJLCJLC`** au dos de chaque carte. JLCPCB
imprime un numéro de commande sur chaque PCB ; avec l'option gratuite
« Remove Order Number → Specify a location », il s'imprime **sur ce marqueur**
— sinon il atterrit n'importe où, parfois en travers d'une étiquette.

## 6. Les pièges spécifiques ESP32 (pourquoi le plan est comme il est)

- **Broches de strapping (0, 2, 5, 12, 15)** : l'ESP32 lit leur niveau au boot
  pour choisir son mode. Y accrocher un périphérique demande des précautions :
  - GPIO12 (MTDI) doit être **bas** au boot → pull-down 10 kΩ sur le canal
    relais qui l'utilise (pompe n3pp) ;
  - GPIO2 porte le 1-Wire msp : son pull-up 4,7 kΩ peut gêner le passage en
    mode flash → si le flash USB échoue, débrancher la sonde DS18B20 ;
  - les canaux relais d'**extension** (AUX) sont volontairement sur des GPIO
    **non-strapping** (16/17/19/23) pour ne pas rejouer ce problème.
- **ADC2 inutilisable quand le WiFi émet** : le WiFi réquisitionne l'ADC2.
  GPIO27 (pluie msp) est sur ADC2 → on câble la sortie **DO numérique** du
  module, pas AO. Toutes nos vraies entrées analogiques sont sur **ADC1**
  (32, 33, 34, 35, 36, 39) — il y en a exactement 6, toutes utilisées.
- **34, 35, 36, 39 = entrées seules** : pas de sortie, pas de pull-up interne —
  parfaites pour l'ADC, inutilisables pour piloter quoi que ce soit.
- **RX0/TX0** : c'est le port série du flash USB. Exposés sur le breakout, mais
  à laisser débranchés pendant un flash.

## 7. La zone 230 V (carte `ffp5cs-wroom-prod-230v` uniquement)

Le 230 V ne se gère pas avec des largeurs de piste mais avec des **distances
d'isolement** :

- **≥ 3 mm** entre tout cuivre secteur et tout cuivre logique (règle
  `.kicad_dru` + contrôle Python indépendant — mesuré : min 3,45 mm) ;
- **fentes fraisées** dans le FR-4 entre canaux et au point faible du boîtier
  relais : l'air + la fente isolent mieux que la surface du substrat (lignes de
  fuite) ;
- pistes secteur **tracées à la main** (déterministes), jamais par
  l'autorouteur ;
- les plans de masse s'**arrêtent à la frontière** de la zone.

Et côté installation : protection amont obligatoire (différentiel 30 mA +
fusible), boîtier fermé, jamais d'intervention sous tension. Le PCB seul ne
rend pas le 230 V « sûr » — il rend l'isolement **vérifiable**.

## 8. Le pipeline du dépôt — qui fait quoi

```
pinmap.json  ←──(vérifié contre les .h des firmwares)── tools/check_pinmap_vs_firmware.py
    │
    ▼
generate.py        schéma + PCB placé (NON routé) + BOM + feuilles d'assemblage
    │                 ⚠️ écrase le PCB routé : toujours enchaîner ↓
    ▼
route_*.py         autoroutage (freerouting) + [230V : pistes secteur en dur]
                   + vias de couture GND + îlots + reliefs thermiques + contrôles
    ▼
tidy_silkscreen.py écarte les repères qui masquent une étiquette (idempotent)
    ▼
export_fab.py      zip Gerbers (7 couches + PTH/NPTH) + rendus SVG/PDF de revue
```

Tout est **généré depuis le code** : la position d'un composant, une étiquette,
une règle de sérigraphie se corrigent dans `generator/generate.py` (tables
`build_components()` / `PCB_TEXTS`), jamais dans les fichiers produits.

### Recettes de modification courantes

- **Changer un GPIO** : modifier le `#define` du firmware **et** `pinmap.json`
  (nets + `a1_nets` dans `generate.py`) → régénérer → le checker confirme.
- **Ajouter un capteur I2C** : rien à faire — 3 ports libres avec pull-ups.
- **Activer un canal relais AUX** : côté carte, souder le kit du canal (voir
  `ACHATS.md` §2d) ; côté firmware, dupliquer le pattern AUX1/AUX2 de ffp5cs.
- **Nouvelle révision** : bumper `REV` dans `generate.py`, dérouler le pipeline,
  le zip est nommé avec la révision.

## 9. Checklist avant chaque commande

1. `generate.py` → `route_*.py` → `tidy_silkscreen.py` → `export_fab.py` ;
2. `tools/check_pinmap_vs_firmware.py` → **OK** ;
3. `kicad-cli sch erc` → **0 erreur** ; `kicad-cli pcb drc` → **0 violation,
   0 pastille non connectée** ;
4. carte 230 V : contrôle 3 mm → **0 écart** ;
5. ouvrir l'aperçu JLCPCB après upload : contour, sérigraphie, pas de couche
   en trop ;
6. paramètres du formulaire : voir **[COMMANDE_JLCPCB.md](COMMANDE_JLCPCB.md)**
   (1,6 mm, vert, HASL, 1 oz — sauf 230 V : 2 oz + confirmation du fichier —
   numéro de commande sur le marqueur).

L'état exact des vérifications au moment de la dernière commande est consigné
dans **[VERIFICATION.md](VERIFICATION.md)**.

## 10. Glossaire

| Terme | Sens |
|---|---|
| **Gerber** | format standard décrivant une couche du PCB pour le fabricant |
| **Excellon** (`.drl`) | fichier des perçages (position + diamètre) |
| **PTH / NPTH** | trou métallisé (*plated*) / non métallisé (vis M3) |
| **Pad / pastille** | zone de cuivre où l'on soude une broche |
| **Via** | trou métallisé reliant les couches de cuivre |
| **Netclass** | famille de nets partageant largeur/isolement/via |
| **Clearance** | distance minimale entre deux cuivres de nets différents |
| **Soldermask** | vernis épargne (le « vert ») |
| **Silkscreen** | sérigraphie (l'encre blanche) |
| **Courtyard** | emprise réservée d'un composant (les chevauchements = DRC) |
| **Keepout** | zone où pistes/vias/plans sont interdits (ex. antenne) |
| **Thermal relief** | connexion en rayons d'un pad à un plan, pour rester soudable |
| **Stitching** | grille de vias reliant les plans de masse des deux faces |
| **DNP** | *Do Not Populate* — emplacement présent, composant non soudé (nos profils) |
| **Strapping pin** | broche lue par l'ESP32 au boot pour choisir son mode |
| **HASL / ENIG** | finitions de surface : étain refondu / nickel-or |
| **1 oz / 2 oz** | épaisseur de cuivre ≈ 35 µm / 70 µm |
| **DRC / ERC** | vérification des règles du PCB / du schéma |
| **DFM** | *Design For Manufacturing* — la vérification côté fabricant |
