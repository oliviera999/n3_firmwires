# Commander n3-universal rev 0.1.2 — check-list fabricant

> Issue de l'audit pré-commande du 2026-08-28 (7 dimensions : netlist, pinmap/firmwares,
> sécurité 230 V, gerbers, générateurs/empreintes, BOM/assemblage, options de commande),
> finalisée sous KiCad 10 le 2026-10-06 (audit §11 et §12).
> Fichier à envoyer : `exports/gerbers-n3-universal-v0.1.2.zip` (7 couches + PTH/NPTH,
> déjà au format accepté tel quel par JLCPCB ; DRC KiCad 0 erreur avant export).
> Les zips v0.1 (via de J2 dans une fente, plan GND à y84) et v0.1.1 (Q11 en NDP6020P,
> VGS hors limites en profil 12 V) sont **périmés** et ont été retirés de `exports/`.

## 0. AVANT de payer — actions obligatoires

1. **Sécuriser les P-MOSFET** (2 références différentes depuis la 0.1.2) :
   - **Q7 = NDP6020P** (rail capteurs, VGS ≈ −3,3 V : *logic-level* obligatoire) :
     onsemi **épuisé** chez LCSC (C68561) ; seul stock = clone VBsemi
     **NDP6020P-VB C878814** (stock faible). Acheter 6-8 pièces **immédiatement**.
   - **Q11 = IRF4905PBF C2564** (anti-inversion 12 V, VGS ±20 V) + **D9 = 1N4744A
     C238928** (zener 15 V grille-source). Le NDP6020P (VGS max ±8 V) ne doit **plus**
     être posé en Q11 : sous 12-14,7 V il serait hors limites dès la mise sous tension.
2. **Acheter un HLK-20M05 et MESURER ses entraxes** avant commande si le profil
   secteur est prévu : l'empreinte (lib KiCad officielle : colonnes AC/DC à 51 mm,
   AC au pas 9 mm, DC au pas 27 mm — cohérente avec le corps 56×32×22,5 confirmé
   par Hi-Link/LCSC C465406) est **contredite par le dessin mécanique p.14 du
   datasheet** (colonnes 44,7 mm). Perçages 1,3 mm : aucun rattrapage possible si
   le module livré suit le dessin.
3. **Corrections recommandées avant export — FAITES en rev 0.1.1** (audit §11) :
   - [x] **J2 (jack 5 V)** pivoté à 0°, ouverture au bord gauche ;
   - [x] plus de **via dans la fente** de J2 (« holes overlap » JLCDFM levé) ;
   - [x] plan GND remonté à **y86** : ligne de fuite secteur ≥ 6,5 mm (`SEC-CRP-01`) ;
   - [x] sérigraphie : traits ≥ 0,15 mm, libellés et repères **dégagés des pads et des
     trous**, polarités « + / GND » sur J1/J26/J36/J37, « COM = PHASE », alerte HC-SR04,
     « UN SEUL MODULE » ; sérigraphie retirée des ouvertures de masque à l'export ;
   - [x] pistes 230 V à 2,0 mm à l'approche de RV1 ;
   - pads TO-92 (27 trous) : **perçage conservé à 0,75 mm** (pattes ~0,6 mm en diagonale,
     tolérance ±0,08 mm) → accepter la remarque DFM « annular ring ».
   - [x] **rev 0.1.2** : Q11 IRF4905 + zener D9, RV1 sur sa vraie empreinte (disque,
     pas 7,5 mm), trou **H5 vis nylon** au coin secteur, sérigraphie « JACK 5V » (sans
     annonce de courant), « I2C J21/J22 », polarités « + / GND » sur J18/J19/J25.
4. **Ré-analyser `gerbers-n3-universal-v0.1.2.zip` dans JLCDFM** (`dfm.jlcdfm.com`) avant
   de payer. Attendu : plus aucune ligne *Danger* ; restent en *Warning* l'anneau TO-92
   et éventuellement les fentes à 1,0 mm (`GBR-05`).

## 1. JLCPCB (https://jlcpcb.com/) — valeurs du formulaire

| Option | Valeur | Note |
|--------|--------|------|
| Base Material | **FR-4** | |
| Layers | **2** | |
| Dimensions | **278 × 120 mm** | lues des gerbers à l'upload |
| PCB Qty | **5** | |
| Different Design | 1 | |
| Delivery Format | **Single PCB** | pas de panélisation |
| PCB Thickness | **1.6 mm** | |
| PCB Color | Green | le moins cher / délai mini ; libre |
| Silkscreen | White | |
| Surface Finish | **LeadFree HASL** ⚠️ | défaut = HASL **au plomb** — carte manipulée par des élèves |
| **Outer Copper Weight** | **2 oz** ⚠️⚠️ | **LE piège n°1 : le défaut est 1 oz.** Toute la zone 230 V (pistes 2,5 mm) et le budget résistif sont calculés en 2 oz. Vérifier sur le récapitulatif. Surcoût assumé. |
| Via Covering | Tented | défaut |
| Min via hole size | 0.3 mm | défaut sans surcoût (plus petit foret réel : 0,35 mm) |
| Board Outline Tolerance | ±0.2 mm | fraisage, défaut |
| **Confirm Production file** | **Yes** | conseillé : grande carte, 19 fentes internes (+~1 j de délai) |
| **Remove Order Number** | **Specify a location** ⚠️ | gratuit — le texte `JLCJLCJLCJLC` est déjà placé au dos (80 ; 155,5). Sans cette option, le texte reste imprimé tel quel ET le n° de commande est ajouté ailleurs. |
| Flying Probe Test | Fully Test | défaut, inclus |
| Gold Fingers / Castellated / Edge Plating | No | |

Surcoûts attendus : 2 oz (principal), surface 278×120 (> 100 mm ⇒ hors promo),
LeadFree HASL (léger). Ordre de grandeur : **~45-75 $ les 5 + port** (~1 kg) ≈
60-100 € livré.

**Remarques DFM — mesurées, plus supposées.** Le dossier **v0.1** a été passé au JLCDFM le
2026-08-29 (`dfm.jlcdfm.com`, rapport `DFM analysis report_JLCDFM_gerbers-n3-universal-v0.1.pdf`,
audit §8). Il ne relevait **aucun défaut de routage** : 4 lignes Danger et 2 Warning, pour
3 causes seulement — les deux premières sont **corrigées en v0.1.1**, seule la troisième
(anneau TO-92) reste à accepter. Historique v0.1 :

| Ligne du DFM | Cause | Décision |
|--------------|-------|----------|
| PTH spacing 0 mm ×1 **+** Via to PTH spacing 0 mm ×1 (*Danger*) | le **même** via GND (48 ; 110) au centre de la fente plaquée de J2 — même net, aucun court-circuit | accept (ou supprimer le via, 5 s sous KiCad) |
| Silkscreen to pad / to hole 0 mm (*Danger* ×50 + ×50) | 26 pads et 30 perçages sous de la sérigraphie : l'encre part à la finition, libellés localement rongés | accept — cosmétique |
| Annular ring 0,15 mm (*Warning* ×54) · Silkscreen line width 0,12 mm (*Warning* ×50) | anneaux TO-92 · petits textes | accept |

⚠️ Ce rapport **ne valide ni la sécurité 230 V ni le fraisage** : il ignore les nets et les
tensions, et le fichier `Edge_Cuts.gm1` n'a été analysé par aucune de ses règles — les 19 fentes
internes n'ont donc **pas** été contrôlées. Voir audit §8.4.

Conformité vérifiée aux capacités JLCPCB (fetch 2026-08-28) : pistes ≥ 0,4 mm
(min 2 oz : 0,16), fentes non plaquées 1,0 mm (= min exact), fentes plaquées
1,0/1,3 mm (min 0,5), perçages 0,35-3,2 mm, NPTH M3 séparés, cuivre-bord ≥ 0,5 mm.

### Assemblage (PCBA)

**Non recommandé pour 5 cartes** (~+150-200 $, dominé par ~90 $ de loading fees
Extended pour ~30 références ; pièces conditionnelles par rôle impossibles à
mutualiser). Si retenu quand même : Standard PCBA (THT), Tooling holes « Added by
JLCPCB », fichiers prêts dans **`exports/pcba/`** (BOM socle + CPL ; ne JAMAIS faire
poser les conditionnelles ni PS1/A1/A2). Détails : `exports/pcba/README.md`.

## 2. PCB Maroc (https://pcbmaroc.com/) — devis email obligatoire

Le configurateur en ligne (1-2 couches, 5-500 pcs) **ne propose ni poids de cuivre,
ni finition, ni fentes internes** : une commande standard produirait une carte 1 oz
non conforme au dossier 230 V. Procédure :

1. Email à `contact@pcbmaroc.com` (+212 602 714-499, Technopark Tanger) avec le zip
   gerbers **et** ce cahier des charges explicite :
   - 278 × 120 mm, 2 couches, FR-4 1,6 mm ;
   - **cuivre extérieur 2 oz (70 µm) impératif** ;
   - **19 fentes internes fraisées** (1,0 et 2,0 mm de large) — fonction : isolement 230 V ;
   - 5 trous **non métallisés** 3,2 mm ; fentes **plaquées** 1,0 / 1,3 mm (jack + clips fusible) ;
   - finition **sans plomb** (LeadFree HASL), tolérance contour ±0,2 mm ;
   - retirer le texte `JLCJLCJLCJLC` du dos (spécifique JLCPCB) ou l'ignorer.
2. Exiger une **confirmation écrite point par point** avant paiement.
3. Sans confirmation du 2 oz et des fentes internes → commander chez JLCPCB.

## 3. Achats composants (rappels issus de l'audit)

- LCSC vérifiés (2026-08-28, recontrôlés le 2026-10-06 sur la page produit) : relais
  **C35449**, HLK-20M05 **C465406**, BC337-40 **C713611**, BS250P **C151450**, 1N5822
  **C2476**, P6KE18A **C1975053**, 10D471K **C111188**, JST B3B-XH-A **C144394**,
  NDP6020P-VB **C878814** (stock critique), IRF4905PBF **C2564**, 1N4744A **C238928**.
  ⚠️ Les codes C72503 / C9135 / C293822 que proposait l'ancien `bom-jlcpcb.csv` étaient
  **faux** (condensateur CMS, connecteur IDC, code inexistant) : ne pas les réutiliser.
- **Manquent à la BOM d'origine** : 2× clips porte-fusible 5×20 (entraxe 22,5 mm),
  5× cavaliers 2,54 mm, **2× vis + écrou nylon M3** (H1 coin relais, H5 coin secteur).
- Relais SRD **Form C : 7 A / 240 VAC réels** (10 A seulement en 125 VAC / Form A) —
  ne pas dépasser ~1,5 kW/230 V par canal ; 3 A inductif.
