# Commander n3-universal rev 0.2 — check-list fabricant et colis

> Rev **0.2 (2026-10-07)** : carte régénérée de bout en bout après la contre-vérification de
> la 0.1.2 ([`AUDIT-2026-08-28.md`](AUDIT-2026-08-28.md) §13 ; résultats des contrôles 0.2 en
> §14). **Fichier à envoyer : `exports/gerbers-n3-universal-v0.2.zip`** (7 couches + PTH/NPTH
> + `gbrjob` portant l'empilage 2 oz ; DRC KiCad 10 : 0 erreur, 0 écart de parité, règle de
> ligne de fuite 6,4 mm comprise). Les zips 0.1.x sont périmés et retirés de `exports/`.
>
> Il n'y aura **qu'une seule commande** : les vérifications locales du §0 sont obligatoires.

## 0. AVANT de payer — ce que le dépôt ne peut pas faire à ta place

1. **JLCDFM** (`dfm.jlcdfm.com`, gratuit) sur `gerbers-n3-universal-v0.2.zip`. Attendu :
   aucune ligne *Danger*. Restent acceptables en *Warning* : « silkscreen line width »
   (petits textes 0,12-0,15 mm) et « slot » sur les 19 fentes d'isolement (1,0 mm = minimum
   JLCPCB exact). Toute autre ligne → ne pas commander, ouvrir une issue avec le rapport.
2. **Vue 3D dans KiCad** (`kicad/n3-universal.kicad_pro`, Alt+3) : vérifier à l'œil les deux
   sites A1/A2 (un module enfiché à la fois, antennes vers l'extérieur), la rangée de
   borniers en bas (ouvertures vers le bord), le coin PSU (F1, RV1, PS1, H5).
3. **Mesurer un HLK-20M05 réel** si le profil secteur (d) est prévu : l'empreinte suit la
   lib KiCad (colonnes AC/DC à 51 mm) que le dessin p.14 du datasheet contredit (44,7 mm).
   Perçages 1,3 mm : pas de rattrapage possible.
4. Vérifier au récapitulatif de commande : **2 oz**, **LeadFree HASL**, **278 × 135 mm**,
   « Remove Order Number : Specify a location ».

## 1. JLCPCB (https://jlcpcb.com/) — valeurs du formulaire

| Option | Valeur | Note |
|--------|--------|------|
| Base Material | **FR-4** | |
| Layers | **2** | |
| Dimensions | **278 × 135 mm** | lues des gerbers à l'upload |
| PCB Qty | **5** | (10 si le budget le permet : la carte ne sera pas refaite) |
| Different Design | 1 | |
| Delivery Format | **Single PCB** | pas de panélisation |
| PCB Thickness | **1.6 mm** | |
| PCB Color | Green | le moins cher / délai mini ; libre |
| Silkscreen | White | |
| Surface Finish | **LeadFree HASL** ⚠️ | défaut = HASL **au plomb** — carte manipulée par des élèves |
| **Outer Copper Weight** | **2 oz** ⚠️⚠️ | **LE piège n°1 : le défaut est 1 oz.** Le `gbrjob` du zip déclare 2 oz, mais le formulaire ne le lit pas : cocher à la main. Zone 230 V (pistes 2,5 mm) et budget résistif calculés en 2 oz. |
| Via Covering | Tented | défaut |
| Min via hole size | 0.3 mm | défaut sans surcoût |
| Board Outline Tolerance | ±0.2 mm | fraisage, défaut |
| **Confirm Production file** | **Yes** | grande carte, 19 fentes internes (+~1 j) |
| **Remove Order Number** | **Specify a location** ⚠️ | gratuit — `JLCJLCJLCJLC` est déjà placé au dos. |
| Flying Probe Test | Fully Test | défaut, inclus |
| Gold Fingers / Castellated / Edge Plating | No | |

Surcoûts : 2 oz (principal), surface > 100 mm (hors promo), LeadFree HASL (léger).
Ordre de grandeur : **~55-85 $ les 5 + port** (~1,2 kg) ≈ 70-110 € livré.

Conformité aux capacités JLCPCB (page *Capabilities*, 2026-08-28) : pistes ≥ 0,4 mm
(min 2 oz : 0,16), fentes non plaquées 1,0 mm (= min exact), fentes plaquées 1,0 / 1,3 / 1,6 mm
(min 0,5), perçages 0,35-3,2 mm, NPTH M3 (3,2 mm) séparés, cuivre-bord ≥ 0,5 mm.

### Assemblage (PCBA) — non recommandé

Carte 100 % traversante, ~35 références *Extended* → loading fees qui dominent le coût pour
5 cartes ; pièces conditionnelles par rôle impossibles à mutualiser. Fichiers de
prévisualisation dans `exports/pcba/` (BOM socle + CPL ; ne JAMAIS faire poser les
conditionnelles ni PS1/A1/A2). Détails : `exports/pcba/README.md`.

## 2. Le colis d'import (LCSC, expédition combinée avec le PCB)

Principe : **un seul colis** LCSC + JLCPCB (option « combiner avec ma commande JLCPCB » dans
le panier LCSC) ; le Maroc n'a pas de franchise douanière → droits + TVA ≈ 23 % de la valeur
+ frais de dédouanement DHL. Les modules ESP32 **ne s'importent pas** (homologation ANRT) :
achat local (§3). Codes LCSC lus sur la page produit le **2026-10-06** (stock à ce jour) :

| Pièce (repères) | Qté / 5 cartes | LCSC | Note |
|---|---|---|---|
| Relais SRD-05VDC-SL-C (K1-K6) | 30 (+2) | **C35449** | Songle, Form C 7 A / 240 VAC réels |
| IRF4905PBF TO-220 (Q7, Q11, Q12) | 15 (+3) | **C2564** | 24 900 en stock ; repli IRF9540N C2575 (RDS plus élevé) |
| LD1117V33 TO-220 (U1) | 5 (+2) | **C283467** | ST, 1 = GND, 2 = OUT, 3 = IN, languette = OUT ; repli LM1117T-3.3 C498321 (même brochage) |
| BC337-40 TO-92 (Q1-Q6, Q8) | 35 (+10) | **C713611** | ou Moussasoft (brochage C-B-E) |
| 1N4007 (D1-D7) | 35 (+5) | **C2457** | l'ancien code C727 n'existe plus |
| 1N5822 (D5) | 5 (+2) | **C2476** | |
| 1N4744A zener 15 V (D9) | 5 (+2) | **C238928** | |
| TVS 1.5KE18A (D8) | 5 (+2) | **C1666858** | Littelfuse 1.5KE18A-B (1 000 en stock) ; équivalents DOWO C18198473, Liown C3034405 |
| TVS 1.5KE6.8A (D12) | 5 (+2) | **C412500** | ⚠️ 480 en stock : commander tôt ; repli P6KE6.8A DO-15 **C409413** (pattes 0,8 mm, mêmes trous) |
| Schottky clamp (D11) | 5 (+5) | **C19167** | BAT43W **SOD-123 (CMS)** sur les pads centraux de l'empreinte double — le BAT85 traversant (C549292) est **épuisé** chez LCSC ; un BAT85/BAT42 DO-35 d'une autre source se pose sur les trous |
| HLK-20M05 (PS1, profil secteur seulement) | selon profil | **C465406** | 175 en stock, mesurer les entraxes (§0.3) |
| Varistance 14D471K / 10D471K (RV1) | 5 | **C111188** | ou MicroPlanet 14D471K |
| Porte-fusible 5×20 à capot (F1) | 5 | **C3131** | Xucheng BLX-A : **pas 22,0 ±0,5 mm** (fiche LCSC), broches plates 1,5 × 0,35 → fentes 3 mm de l'empreinte universelle ; ou clips Schurter/Multicomp (22,5-22,6) sur les mêmes fentes |
| Cartouche T1A 5×20 | 10 | — | locale (quincaillerie) ou LCSC au choix |
| Barrette mâle 2,54 1×40 (JP1-JP21, J38, TP) | 10 | **C2337** | sécable |
| Barrette femelle 2,54 1×40 (supports A1/A2) | 10 | **C9811** | 2 × 15 + 2 × 22 par carte ; ou Moussasoft |
| Cavaliers 2,54 | 100 | **C2689176** | 18 par carte + rechange |
| Condensateurs 10 µF / 100 µF radiaux (C11, C12, C2, C5) | 5 + 15 | — | locaux ou LCSC (Basic) |

Tout le reste (résistances 1/4 W, 100 nF, LED 5 mm, borniers KF301/KF128 5,08 à 2, 3 et
4 pôles, jack DC-005, visserie M3 dont **nylon pour H1/H5**, fil) : **achat local** (§3).

## 3. Achats au Maroc (référence) — et ailleurs en Afrique

| Fournisseur | Quoi | Remarque |
|---|---|---|
| **Moussasoft** (Casablanca, moussasoft.com) | relais SRD, BC337, 1N4007, 1N5822, borniers KF301, barrettes, résistances, condensateurs, ESP32 | catalogue le plus large, prix unitaires bas |
| **Shop4Makers** (Casablanca) | ESP32 DevKit V1 Type-C, ESP32-S3 N16R8, HLK-20M05, borniers | modules ESP32 : **acheter ici**, pas d'import |
| **A2itronic** | IRF4905, TO-220 courants, LD1117 parfois | vérifier le stock avant |
| **MicroPlanet** | varistances 14D471K, porte-fusibles, fusibles | |

Ailleurs en Afrique : la même carte se monte avec le colis LCSC du §2 + n'importe quel
revendeur local pour les passifs et les borniers à vis 5,08 (KF301 = standard mondial).
Aucune pièce n'exige un distributeur occidental ; les seuls « spéciaux » (IRF4905,
LD1117V33, 1.5KE, relais Songle) sont tous dans le colis.

## 4. PCB Maroc (https://pcbmaroc.com/) — devis email obligatoire

Le configurateur en ligne **ne propose ni poids de cuivre, ni finition, ni fentes internes** :
une commande standard produirait une carte 1 oz non conforme au dossier 230 V. Procédure :

1. Email à `contact@pcbmaroc.com` (+212 602 714-499, Technopark Tanger) avec le zip
   gerbers **et** ce cahier des charges explicite :
   - 278 × 135 mm, 2 couches, FR-4 1,6 mm ;
   - **cuivre extérieur 2 oz (70 µm) impératif** (déclaré dans le `gbrjob`) ;
   - **19 fentes internes fraisées** (1,0 et 2,0 mm de large) — fonction : isolement 230 V ;
   - 7 trous **non métallisés** 3,2 mm ; fentes **plaquées** (jack 1,0 / fusible 1,6 × 3,0) ;
   - finition **sans plomb** (LeadFree HASL), tolérance contour ±0,2 mm ;
   - retirer le texte `JLCJLCJLCJLC` du dos (spécifique JLCPCB) ou l'ignorer.
2. Exiger une **confirmation écrite point par point** avant paiement.
3. Sans confirmation du 2 oz et des fentes internes → commander chez JLCPCB.

## 5. Rappels de sécurité

- Relais SRD **Form C : 7 A / 240 VAC réels** (10 A seulement en 125 VAC / Form A) —
  ne pas dépasser ~1,5 kW / 230 V par canal ; 3 A inductif.
- **H1 et H5 : vis nylon** (coin relais et coin secteur) ; les 5 autres trous en métal.
- **JP7 (chauffage K3) n'a pas de position ON** : c'est voulu.
- Poser le porte-fusible **à capot** (C3131) plutôt que des clips nus si la carte est en
  boîtier ouvert : aucun 230 V accessible au doigt.
