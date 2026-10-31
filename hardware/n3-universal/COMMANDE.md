# Commander n3-universal rev 0.2.2 — check-list fabricant et colis

> Rev **0.2.2 (2026-10-17)** : la 0.2 corrigée après simulation SPICE (Q7 inversé — rail capteurs
> jamais coupé —, servos sur le 5 V commuté, D5 en pont de fil, R39 = 18k ; voir README §0.2.2,
> [`simulation/SIMULATION.md`](simulation/SIMULATION.md) et [`ALIMENTATION.md`](ALIMENTATION.md)),
> carte reroutée et revalidée. **Fichier à envoyer : `exports/gerbers-n3-universal-v0.2.2.zip`**
> (7 couches + PTH/NPTH + `gbrjob` portant l'empilage **1 oz** ; DRC KiCad 10 : 0 erreur, 0 écart
> de parité, règle de ligne de fuite 6,4 mm comprise). Les zips 0.1.x et 0.2 sont périmés et
> retirés de `exports/`.
>
> Il n'y aura **qu'une seule commande** : les vérifications locales du §0 sont obligatoires.

## 0. AVANT de payer — ce que le dépôt ne peut pas faire à ta place

1. **JLCDFM** (`jlcdfm.com`, gratuit) sur `gerbers-n3-universal-v0.2.2.zip` — **fait le
   2026-10-30** : 9 fichiers reconnus, tout le cuivre au vert (écarts de pistes, anneaux,
   trou ↔ piste, pastilles, bord de carte, vernis). Lignes *Danger* **attendues** (aucune ne
   touche au cuivre) :
   - 2 × « tht to smd » : empreinte double de **D11** (DO-35 **ou** SOD-123, une seule posée) ;
   - 21 × « silkscreen to hole » (0-0,09 mm) : sérigraphie contre des **vias 0,35 mm tentés**
     (vernis), plus un trou 0,5 mm ;
   - 19 × « silkscreen to pad » (0,08-0,13 mm, aucun chevauchement) : l'usine efface l'encre
     au ras des pastilles.
   *Warnings* attendus : « silkscreen line width » (traits 0,15 mm = minimum exact) et
   2 « short slot » 1,4 mm (fentes du porte-fusible F1). Toute **autre** ligne → ne pas
   commander, ouvrir une issue avec le rapport.
2. **Vue 3D dans KiCad** (`kicad/n3-universal.kicad_pro`, Alt+3) : vérifier à l'œil les deux
   sites A1/A2 (un module enfiché à la fois, antennes vers l'extérieur), la rangée de
   borniers en bas (ouvertures vers le bord), le coin PSU (F1, RV1, PS1, H5).
3. **Mesurer un HLK-20M05 réel** si le profil secteur (d) est prévu : l'empreinte suit la
   lib KiCad (colonnes AC/DC à 51 mm) que le dessin p.14 du datasheet contredit (44,7 mm).
   Perçages 1,3 mm : pas de rattrapage possible.
4. Vérifier au récapitulatif de commande : **1 oz**, **LeadFree HASL**, **278 × 135 mm**,
   « Mark on PCB : Remove Mark ».

## 1. JLCPCB (https://jlcpcb.com/) — valeurs du formulaire

| Option | Valeur | Note |
|--------|--------|------|
| Base Material | **FR-4** | |
| Layers | **2** | |
| Dimensions | **278 × 135 mm** | lues des gerbers à l'upload |
| PCB Qty | **10** | une carte par profil + rechange : la carte ne sera pas refaite (5 = minimum) |
| Different Design | 1 | |
| Delivery Format | **Single PCB** | pas de panélisation |
| PCB Thickness | **1.6 mm** | |
| PCB Color | Green | le moins cher / délai mini ; libre |
| Silkscreen | White | |
| Surface Finish | **LeadFree HASL** ⚠️ | défaut = HASL **au plomb** — carte manipulée par des élèves |
| **Outer Copper Weight** | **1 oz** | défaut du formulaire. **Décision du 2026-10-30, ex-2 oz** (le 2 oz coûtait 58 $ sur 102 $ pour 10 cartes) : pistes contacts 2,5 mm en 1 oz ≈ 4,5 A à +10 °C (IPC-2221, couche externe) — d'où la **règle d'usage du §5 : ≤ 1 kW résistif / 3 A moteur par canal** ; chemin d'alimentation du DevKit 41 mΩ au lieu de 21 mΩ (≈ +10 mV à 0,5 A, négligeable devant la marge de 100-200 mV) ; bus 12 V 26 mΩ au lieu de 13 (≈ 80 mV à 3 A). Bonus : soudure à la main plus facile. Le `gbrjob` déclare 1 oz, mais le formulaire ne le lit pas : vérifier la case. Zone 230 V (pistes 2,5 mm) et budget résistif calculés en 2 oz. |
| Via Covering | Tented | défaut |
| Min via hole size | 0.3 mm | défaut sans surcoût |
| Board Outline Tolerance | ±0.2 mm | fraisage, défaut |
| **Confirm Production file** | **Yes** | grande carte, 19 fentes internes (+~1 j) |
| **Mark on PCB** (ex-« Remove Order Number ») | **Remove Mark** | défaut du formulaire en 2026-10, sans surcoût ; `JLCJLCJLCJLC` reste placé au dos au cas où. |
| Flying Probe Test | Fully Test | défaut, inclus |
| Gold Fingers / Castellated / Edge Plating | No | |

Surcoûts : surface > 100 mm (hors promo), LeadFree HASL (léger) — le 2 oz n'en fait plus partie.

**Devis du 2026-10-30** (compte connecté, FedEx Express 6-11 jours ouvrés, livraison au
Lycée Lyautey, 260 Bd Ziraoui, 20000 Casablanca) :

| | 5 cartes | 10 cartes (retenu) |
|---|---|---|
| 1 oz | 25,05 $ + 38,66 $ = 63,71 $ | **44,05 $ + 49,06 $ = 93,11 $** |
| 2 oz (pour mémoire) | 70,65 $ + 38,66 $ = 109,31 $ | 102,05 $ + 56,84 $ = 158,89 $ |

Port DHL / UPS au même moment : 51,59 / 54,05 $ (5 cartes), 70,48 / 79,48 $ (10 cartes).
Droits + TVA à l'arrivée ≈ 23 % de la valeur (§2).

Conformité aux capacités JLCPCB (page *Capabilities*, 2026-08-28) : pistes ≥ 0,4 mm
(min 1 oz : 0,127), fentes non plaquées 1,0 mm (= min exact), fentes plaquées 1,0 / 1,3 / 1,6 mm
(min 0,5), perçages 0,35-3,2 mm, NPTH M3 (3,2 mm) séparés, cuivre-bord ≥ 0,5 mm.

### Assemblage (PCBA) — non recommandé

Carte 100 % traversante, ~35 références *Extended* → loading fees qui dominent le coût pour
5 cartes ; pièces conditionnelles par rôle impossibles à mutualiser. Fichiers de
prévisualisation dans `exports/pcba/` (BOM socle + CPL ; ne JAMAIS faire poser les
conditionnelles ni PS1/A1/A2). Détails : `exports/pcba/README.md`.

## 2. Le colis d'import LCSC — abandonné pour le Maroc (2026-10-30)

Principe d'origine : **un seul colis** LCSC + JLCPCB ; le Maroc n'a pas de franchise douanière → droits + TVA ≈ 23 % de la valeur
+ frais de dédouanement DHL. Les modules ESP32 **ne s'importent pas** (homologation ANRT) :
achat local (§3).

**Commande du 2026-10-30 — pas de colis LCSC vers le Maroc.** Vérifications faites ce jour
(comptes connectés, FAQ LCSC « combine LCSC & JLCPCB orders » et « combine orders ») :

- **Le regroupement LCSC + JLCPCB passe par l'entrepôt LCSC de Hong Kong** : la commande
  JLCPCB doit être livrée à l'adresse de l'entrepôt (Kwai Chung, NT, Hong Kong, 999077 — non
  modifiable après paiement), puis **LCSC facture le transport international** de l'ensemble.
  Conditions : même n° client (vérifié : **13313102A** des deux côtés) et même devise (USD,
  vérifié) ; une fois combinées, les commandes ne se séparent plus.
- **Tarif LCSC vers le Maroc** (estimation du panier, seul transporteur proposé : UPS) :
  **104,72 $ pour 1 kg facturé, 133,53 $ pour 1,5 kg** (~58 $/kg au-delà). Les 10 cartes
  (1,43 kg) + composants (~1,5 kg facturés) passeraient à ~200 $ de port, contre **49,06 $
  en FedEx direct chez JLCPCB**.
- **Conséquence** : commander les cartes **chez JLCPCB, livrées directement au lycée en
  FedEx**, sans regroupement ; **aucune commande LCSC** — même le panier « max composants »
  (relais, IRF4905, LD1117, borniers… 27,58 $, ~215 DH d'économie sur les pièces) est
  englouti par les 104,72 $ de port minimum. Tout s'achète au Maroc (`achats/`), sauf :
  - **TVS D12** (1.5KE6.8A, entrée 5 V, toutes les cartes) et **D8** (1.5KE18A, profil
    bus 12 V seulement) : introuvables lors du relevé du 18/10. Pistes : redemander dans les
    boutiques (accepter la série **P6KE6.8A / P6KE18A**, DO-15, mêmes trous — §2 tableau),
    ou un achat AliExpress ; la carte fonctionne sans elles le temps de les recevoir
    (protection contre les surtensions seulement, ne pas installer en extérieur sans).
- Le panier LCSC préparé ce jour (« Cart_20261031021007 », 12 lignes, 27,58 $) reste
  enregistré dans le compte, non payé, pour une future commande groupée vers un autre pays.

Le tableau ci-dessous reste la **référence de repli** si un composant manque localement.
Codes LCSC lus sur la page produit le **2026-10-06** (stock à ce jour) :

| Pièce (repères) | Qté / 5 cartes | LCSC | Note |
|---|---|---|---|
| Relais SRD-05VDC-SL-C (K1-K6) | 30 (+2) | **C35449** | Songle, Form C 7 A / 240 VAC réels |
| IRF4905PBF TO-220 (Q7, Q11, Q12) | 15 (+3) | **C2564** | 24 900 en stock ; repli IRF9540N C2575 (RDS plus élevé) |
| LD1117V33 TO-220 (U1) | 5 (+2) | **C283467** | ST, 1 = GND, 2 = OUT, 3 = IN, languette = OUT ; repli LM1117T-3.3 C498321 (même brochage) |
| BC337-40 TO-92 (Q1-Q6, Q8) | 35 (+10) | **C713611** | ou Moussasoft (brochage C-B-E) |
| 1N4007 (D1-D7) | 35 (+5) | **C2457** | l'ancien code C727 n'existe plus |
| ~~1N5822 (D5)~~ | 0 | — | **rev 0.2.2 : D5 = pont de fil**, plus rien à acheter (empreinte gardée) |
| 1N4744A zener 15 V (D9) | 5 (+2) | **C238928** | |
| TVS 1.5KE18A (D8) | 5 (+2) | **C1666858** | Littelfuse 1.5KE18A-B (1 000 en stock) ; équivalents DOWO C18198473, Liown C3034405 |
| TVS 1.5KE6.8A (D12) | 5 (+2) | **C412500** | ⚠️ 480 en stock : commander tôt ; repli P6KE6.8A DO-15 **C409413** (pattes 0,8 mm, mêmes trous) |
| Schottky clamp (D11) | 5 (+5) | **C19167** | BAT43W **SOD-123 (CMS)** sur les pads centraux de l'empreinte double — le BAT85 traversant (C549292) est **épuisé** chez LCSC ; un BAT85/BAT42 DO-35 d'une autre source se pose sur les trous |
| HLK-20M05 (PS1, profil secteur seulement) | selon profil | **C465406** | 175 en stock, mesurer les entraxes (§0.3) |
| Varistance 14D471K / 10D471K (RV1) | 5 | **C111188** | ou MicroPlanet 14D471K |
| Porte-fusible 5×20 à capot (F1) | 5 | **C3131** | Xucheng BLX-A : **pas 22,0 ±0,5 mm** (fiche LCSC), broches plates 1,5 × 0,35 → fentes 3 mm de l'empreinte universelle ; ou clips Schurter/Multicomp (22,5-22,6) sur les mêmes fentes |
| Cartouche T1A 5×20 | 10 | — | locale (quincaillerie) ou LCSC au choix |
| Barrette mâle 2,54 1×40 (JP1-JP21, J38, TP) | 10 | **C2337** | sécable |
| Barrette femelle 2,54 1×40 (supports A1/A2) | 10 | **C9811** | 2 × 19 (A1, rev 0.2.3) + 2 × 22 par carte ; ou Moussasoft |
| Cavaliers 2,54 | 100 | **C2689176** | 18 par carte + rechange |
| Condensateurs 10 µF / 100 µF radiaux (C11, C12, C2, C5) | 5 + 15 | — | locaux ou LCSC (Basic) |

Tout le reste (résistances 1/4 W, 100 nF, LED 5 mm, borniers KF301/KF128 5,08 à 2, 3 et
4 pôles, jack DC-005, visserie M3 dont **nylon pour H1/H5**, fil) : **achat local** (§3).

## 3. Achats au Maroc (référence) — et ailleurs en Afrique

**Liste chiffrée par projet** (quantités, prix relevés, liens) : `achats/achats-n3-universal.xlsx`,
régénérée depuis la BOM par `python3 achats/build_achats.py`.

| Fournisseur | Quoi | Remarque |
|---|---|---|
| **Moussasoft** (Casablanca, moussasoft.com) | relais SRD, BC337, 1N4007, borniers KF301, barrettes, résistances, condensateurs, ESP32 | catalogue le plus large, prix unitaires bas |
| **Shop4Makers** (Casablanca) | ESP32 38 broches (WROOM-32E, 114 DH), ESP32-S3 N16R8, HLK-20M05, borniers | modules ESP32 : **acheter ici**, pas d'import |
| **A2itronic** | IRF4905, TO-220 courants, LD1117 parfois | vérifier le stock avant |
| **MicroPlanet** | varistances 14D471K, porte-fusibles, fusibles | |

Ailleurs en Afrique : la même carte se monte avec le colis LCSC du §2 + n'importe quel
revendeur local pour les passifs et les borniers à vis 5,08 (KF301 = standard mondial).
Aucune pièce n'exige un distributeur occidental ; les seuls « spéciaux » (IRF4905,
LD1117V33, 1.5KE, relais Songle) sont tous dans le colis.

## 4. PCB Maroc (https://pcbmaroc.com/) — devis email obligatoire

Le configurateur en ligne **ne propose ni poids de cuivre, ni finition, ni fentes internes** :
une commande standard ne garantit ni la finition sans plomb ni les fentes internes. Procédure :

1. Email à `contact@pcbmaroc.com` (+212 602 714-499, Technopark Tanger) avec le zip
   gerbers **et** ce cahier des charges explicite :
   - 278 × 135 mm, 2 couches, FR-4 1,6 mm ;
   - cuivre extérieur **1 oz (35 µm)** (déclaré dans le `gbrjob`) ;
   - **19 fentes internes fraisées** (1,0 et 2,0 mm de large) — fonction : isolement 230 V ;
   - 7 trous **non métallisés** 3,2 mm ; fentes **plaquées** (jack 1,0 / fusible 1,6 × 3,0) ;
   - finition **sans plomb** (LeadFree HASL), tolérance contour ±0,2 mm ;
   - retirer le texte `JLCJLCJLCJLC` du dos (spécifique JLCPCB) ou l'ignorer.
2. Exiger une **confirmation écrite point par point** avant paiement.
3. Sans confirmation des fentes internes et de la finition → commander chez JLCPCB.

Calculateur en ligne au 2026-10-30 (2 couches, 278 × 135 mm, options non précisées) :
598 DH HT les 5, 1 088 DH HT les 10 — prix final sur devis sous 2 jours ouvrés.

## 5. Rappels de sécurité

- Relais SRD **Form C : 7 A / 240 VAC réels** (10 A seulement en 125 VAC / Form A), mais
  c'est le **cuivre 1 oz** qui fixe la limite de la carte : **≤ 1 kW résistif (≈ 4,5 A) et
  ≤ 3 A moteur (pompe) par canal**. Au-delà (gros chauffage), piloter un contacteur externe.
- **H1 et H5 : vis nylon** (coin relais et coin secteur) ; les 5 autres trous en métal.
- **JP5-JP10 : livrer SANS cavalier** (= AUTO). 1-2 force ON, 2-3 force OFF. **JP7 (chauffage K3) n'a pas de broche ON** : c'est voulu.
- Poser le porte-fusible **à capot** (C3131) plutôt que des clips nus si la carte est en
  boîtier ouvert : aucun 230 V accessible au doigt.
