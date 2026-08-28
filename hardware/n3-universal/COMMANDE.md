# Commander n3-universal rev 0.1 — check-list fabricant

> Issue de l'audit pré-commande du 2026-08-28 (7 dimensions : netlist, pinmap/firmwares,
> sécurité 230 V, gerbers, générateurs/empreintes, BOM/assemblage, options de commande).
> Fichier à envoyer : `exports/gerbers-n3-universal-v0.1.zip` (7 couches + PTH/NPTH,
> déjà au format accepté tel quel par JLCPCB).

## 0. AVANT de payer — actions obligatoires

1. **Sécuriser les NDP6020P** (Q7 + Q11 = 2/carte) : onsemi **épuisé** chez LCSC
   (C68561) ; seul stock = clone VBsemi **NDP6020P-VB C878814 (~29 pièces, ~0,31 $)**.
   Acheter 12-15 pièces **immédiatement**. Q7 exige un P-FET *logic-level*
   (Vgs ≈ −3,3 V) ; pour Q11 (Vgs ≈ −12 V) un IRF9540N/IRF4905 standard convient en
   secours (même brochage G-D-S).
2. **Acheter un HLK-20M05 et MESURER ses entraxes** avant commande si le profil
   secteur est prévu : l'empreinte (lib KiCad officielle : colonnes AC/DC à 51 mm,
   AC au pas 9 mm, DC au pas 27 mm — cohérente avec le corps 56×32×22,5 confirmé
   par Hi-Link/LCSC C465406) est **contredite par le dessin mécanique p.14 du
   datasheet** (colonnes 44,7 mm). Perçages 1,3 mm : aucun rattrapage possible si
   le module livré suit le dessin.
3. **Corrections recommandées avant export** (nécessitent KiCad, ~1 h, cf. audit) :
   - **J2 (jack 5 V)** : ouverture orientée vers l'**intérieur** de la carte (face à
     la colonne R1/R5/R9 du canal K1, à ~3 mm) → pivoter vers le bord gauche ou
     supprimer (le bornier J1 assure l'entrée 5 V, et le jack DC-005 n'est de toute
     façon coté que ~2,5 A pour un « 5V 3A » sérigraphié) ;
   - **via GND (48,110)** superposé à la fente plaquée du pad 2 de J2 → à supprimer
     (même net, redondant) sinon la DFM JLCPCB s'arrête sur « holes overlap » ;
   - pads TO-92 (27 trous) : anneau 0,15 mm < reco JLCPCB 2 oz 0,25 mm → perçage
     0,6 mm ou pads élargis ; sinon accepter la remarque DFM ;
   - sérigraphie : traits 0,12 mm → 0,15 mm ; ajouter les polarités +/− sur
     J1/J26/J36/J37 ; resserrer les pistes 230 V à l'approche de RV1/J27 (écart
     L↔N mesuré 2,50 mm, l'annonce « ≥ 3 mm » ne tient qu'hors approche de pads).
   La carte est **commandable sans ces retouches** (aucun défaut bloquant) : dans ce
   cas, répondre « accept » aux remarques DFM ci-dessus.

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
60-100 € livré. Remarques DFM probables à accepter si pas de re-export : anneau
TO-92 0,15 mm, via superposé à la fente J2, traits sérigraphie 0,12 mm.

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
   - 4 trous **non métallisés** 3,2 mm ; fentes **plaquées** 1,0 / 1,3 mm (jack + clips fusible) ;
   - finition **sans plomb** (LeadFree HASL), tolérance contour ±0,2 mm ;
   - retirer le texte `JLCJLCJLCJLC` du dos (spécifique JLCPCB) ou l'ignorer.
2. Exiger une **confirmation écrite point par point** avant paiement.
3. Sans confirmation du 2 oz et des fentes internes → commander chez JLCPCB.

## 3. Achats composants (rappels issus de l'audit)

- LCSC vérifiés (2026-08-28) : relais **C35449**, HLK-20M05 **C465406**, BC337-40
  **C713611**, BS250P **C151450**, 1N5822 **C2476**, P6KE18A **C1975053**, 10D471K
  **C111188**, JST B3B-XH-A **C144394**, NDP6020P-VB **C878814** (stock critique).
- **Manquent à la BOM d'origine** : 2× clips porte-fusible 5×20 (entraxe 22,5 mm),
  5× cavaliers 2,54 mm, vis nylon M3 pour H1.
- RV1 (10D471K) : pas réel 7,5 mm sur empreinte 5,0 mm → replier les pattes (pose main).
- Relais SRD **Form C : 7 A / 240 VAC réels** (10 A seulement en 125 VAC / Form A) —
  ne pas dépasser ~1,5 kW/230 V par canal ; 3 A inductif.
