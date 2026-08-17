# Commander les cartes chez JLCPCB — réglages et explications

Guide commun aux trois cartes du dossier `hardware/`. Il explique **ce que fait
chaque paramètre** du formulaire JLCPCB, **lequel choisir** pour nos cartes, et
**pourquoi**. Les valeurs par défaut de JLCPCB conviennent presque partout : les
seules décisions qui comptent vraiment sont signalées 🔸.

> Réf. capacités fabricant : <https://jlcpcb.com/capabilities/pcb-capabilities>
> Les prix cités sont des ordres de grandeur 2026 hors port et taxes.

## 0. Produire le zip à envoyer

```bash
cd hardware/<carte>
python3 generator/export_fab.py        # -> exports/gerbers-<carte>-v<REV>.zip
```

Le zip contient exactement **10 fichiers** : 7 couches Gerber
(`F_Cu, B_Cu, F_Mask, B_Mask, F_Silkscreen, B_Silkscreen, Edge_Cuts`), les deux
fichiers de perçage (`PTH` / `NPTH` séparés) et le fichier de job.

> ⚠️ Ne jamais envoyer un export « toutes couches » : `F.Fab`, `*.Courtyard`,
> `User.*` et `Margin` sont des couches de **documentation**. Au mieux le
> fabricant les ignore, au pire sa vérification automatique prend `Margin` pour
> un contour de découpe. `export_fab.py` existe précisément pour éviter ça.

Sur le site : **Add gerber file** → glisser le zip. JLCPCB détecte seul les
dimensions, le nombre de couches et affiche un aperçu — **le vérifier**
(contour correct, sérigraphie lisible, pas de couche fantôme).

## 1. Les paramètres, un par un

### Ce qui est détecté automatiquement (à vérifier, pas à saisir)

| Paramètre | Ce que c'est | Attendu chez nous |
|---|---|---|
| **Base Material** | famille de substrat | **FR-4** (standard verre/époxy) |
| **Layers** | nombre de couches de cuivre | **2** |
| **Dimension** | taille lue dans `Edge.Cuts` | commune 210×105 / 12-24 V 150×100 / 230 V 234×110 mm |

### Ce qu'on choisit

| Paramètre | Ce que ça change | Notre choix | Pourquoi |
|---|---|---|---|
| **PCB Qty** | nombre de cartes | **5** (minimum) | le minimum coûte quasiment le prix d'une seule |
| **Product Type** | déclaration douanière | *Industrial/Consumer electronics* | déclaratif, sans effet sur la fabrication |
| **Different Design** | nb de designs distincts dans le zip | **1** | un seul contour par commande |
| **Delivery Format** | cartes séparées ou en panneau | **Single PCB** | nos cartes sont grandes, aucun intérêt à panéliser |
| **PCB Thickness** | épaisseur du stratifié | 🔸 **1,6 mm** | rigidité indispensable : borniers à vis, relais et un DevKit qu'on enfiche/retire. 1,2 mm plierait |
| **PCB Color** | couleur du vernis | **vert** | le moins cher et le plus rapide (autres couleurs = délai +) ; meilleur contraste avec la sérigraphie blanche — or nos étiquettes de câblage servent à ça. Le noir mat rend les repères difficiles à lire |
| **Silkscreen** | couleur des marquages | **blanc** (imposé avec le vert) | — |
| **Surface Finish** | traitement des pastilles | **HASL with lead** | tout est traversant et soudé à la main : l'étain-plomb est le plus facile à souder et le moins cher. *Alternative :* **ENIG** (+10-20 €) — surface plate et or, meilleure tenue à l'humidité ; défendable pour la serre/aquaponie, inutile côté soudabilité |
| **Outer Copper Weight** | épaisseur du cuivre | 🔸 **1 oz** — sauf **carte 230 V : 2 oz** | voir §2 : à 10 A, le cuivre 1 oz s'échauffe de ~55-60 °C sur les pistes secteur, contre ~20 °C en 2 oz |
| **Via Covering** | traitement des vias | **Tented** (recouverts de vernis) | par défaut, évite les courts-circuits accidentels sur les vias de couture de masse |
| **Min via hole size** | perçage mini des vias | **0,3 mm (0,4/0,45 mm)** | nos vias : 0,35 mm de perçage / 0,7 mm de diamètre (0,3/0,5 sur la carte 230 V) — tout est au-dessus du standard |
| **Board Outline Tolerance** | précision du détourage | **±0,2 mm (Regular)** | aucune contrainte mécanique serrée ; la version précision est payante |
| **Confirm Production File** | JLCPCB vous envoie le fichier retravaillé pour accord | **No** — 🔸 **Yes pour la carte 230 V** | ajoute ~1 jour, mais la carte 230 V comporte des **fentes d'isolement fraisées** : autant confirmer qu'elles sont bien comprises comme voulues |
| **Remove Order Number** | le numéro de commande imprimé par JLCPCB | 🔸 **Specify a location** (gratuit) | nos cartes portent déjà le marqueur `JLCJLCJLCJLC` au **dos** : JLCPCB imprime son numéro **là** au lieu de le poser au hasard, parfois en travers d'une étiquette. « Remove » est payant, « No » laisse le hasard décider |
| **Flying Probe Test** | test électrique | **Fully test** (gratuit) | aucune raison de refuser |
| **Gold fingers / Castellated / Impedance** | options spéciales | **non** | sans objet ici |
| **Package Box** | emballage neutre ou logo JLC | indifférent | — |

### À laisser tranquille

Les onglets **Assembly (SMT)** ne nous concernent pas : nos trois cartes sont
**100 % traversantes**, soudées à la main. Activer l'assemblage imposerait des
composants du catalogue JLCPCB et un fichier de placement.

## 2. Ce qui change d'une carte à l'autre

| | **n3pp + msp commune** | **ffp5cs 12/24 V** | **ffp5cs 230 V** |
|---|---|---|---|
| Zip | `gerbers-n3pp-msp-commun-v0.1.zip` | `gerbers-ffp5cs-wroom-prod-v0.5.zip` | `gerbers-ffp5cs-wroom-prod-230v-v0.3.zip` |
| Dimensions | 210 × 105 mm | 150 × 100 mm | 234 × 110 mm |
| Cuivre | 1 oz | 1 oz | 🔸 **2 oz** |
| Confirm production file | No | No | 🔸 **Yes** (fentes fraisées) |
| Remarque à la commande | — | — | 🔸 « Internal slots in Edge.Cuts are intentional isolation slots between mains channels, please mill as drawn » |
| Prix indicatif (5 ex.) | ~15-20 € | ~10-15 € | ~25-35 € (2 oz) |

**Pourquoi 2 oz sur la carte 230 V et pas ailleurs :** l'échauffement d'une piste
dépend de sa section. Les pistes de contacts relais de la carte 230 V font
2,5 mm de large et peuvent porter 10 A ; en cuivre standard (35 µm) la section
est trop faible et la piste monte de ~55-60 °C au-dessus de l'ambiante. Doubler
l'épaisseur (70 µm) ramène cet échauffement autour de 20 °C. Sur les deux autres
cartes, les relais commutent du 12/24 V à quelques ampères : 1 oz suffit
largement.

## 3. Vérifications faites en amont (rien à corriger côté fabricant)

Ces points sont garantis par nos scripts — utile à savoir si la vérification
automatique de JLCPCB pose une question :

- **DRC KiCad : 0 violation, 0 pastille non connectée** sur les trois cartes ;
- **sérigraphie conforme** : tous les textes font ≥ 1 mm de haut et ≥ 0,15 mm de
  trait (minimum fabricant), et aucun texte n'en recouvre un autre
  (`generator/tidy_silkscreen.py` écarte les repères qui gênent une étiquette).
  Les contours de composants restent à 0,12 mm : c'est la valeur standard de la
  bibliothèque officielle KiCad, universellement acceptée ;
- **trous non métallisés séparés** : les 4 trous de fixation M3 sont dans le
  fichier `NPTH`, ils ne seront pas cuivrés ;
- **écarts respectés** : piste la plus fine 0,4 mm, isolement 0,2 mm, perçage
  composant mini 0,75 mm — très au-dessus des minima JLCPCB (0,127 / 0,127 / 0,2) ;
- carte 230 V : règle dédiée `.kicad_dru` **≥ 3 mm entre cuivre secteur et
  cuivre logique**, vérifiée en plus par un contrôle géométrique indépendant.

## 4. Après la commande

- **Délai** : 2-3 jours de fabrication en vert/1,6 mm/HASL, + le transport choisi.
- **À la réception** : contrôler à l'ohmmètre la continuité NO/COM/NC des relais
  **avant** de brancher la moindre charge (rappel présent dans les `ACHATS.md`).
- **Si une révision suit** : bumper `REV` dans `generator/generate.py`, relancer
  `generate.py` → `route_*.py` → `tidy_silkscreen.py` → `export_fab.py`. Le nom
  du zip porte la révision, ce qui évite de renvoyer un ancien fichier.
