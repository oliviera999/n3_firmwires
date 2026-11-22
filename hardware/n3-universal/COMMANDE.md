# Commander n3-universal rev 0.3.0 — check-list fabricant et colis

> Rev **0.3.0 (2026-11-22)** : la 0.2.3 **sans le 230 V, sans les relais et sans l'étage
> transistor** (README § Rev 0.3, [`MODULES_RELAIS.md`](MODULES_RELAIS.md),
> [`simulation/SIMULATION.md`](simulation/SIMULATION.md) §13, [`ALIMENTATION.md`](ALIMENTATION.md)
> §4, §5, §10). Carte **278 × 91 mm**, reroutée à partir du cuivre de la 0.2.3 et revalidée.
> **Fichier à envoyer : `exports/gerbers-n3-universal-v0.3.0.zip`** (7 couches + PTH/NPTH +
> `gbrjob` portant l'empilage **1 oz** ; DRC KiCad 10.0.8 : 0 erreur, 0 non connecté, 0 écart de
> parité). Plus aucune fente interne fraisée, plus de règle de ligne de fuite : il n'y a plus de
> 230 V sur la carte.
>
> `exports/gerbers-n3-universal-v0.2.3.zip` est **conservé** : la 0.2.3 reste commandable tant
> que la 0.3 n'est pas validée par l'essai sur table du §0. Les zips 0.1.x, 0.2 et 0.2.2 sont
> périmés et retirés.
>
> Il n'y aura **qu'une seule commande** : les vérifications du §0 sont obligatoires.

## 0. AVANT de payer — ce que le dépôt ne peut pas faire à ta place

1. **Essai sur table d'un module relais** ([`MODULES_RELAIS.md`](MODULES_RELAIS.md) §8, une
   demi-heure, sans 230 V) : la carte suppose des modules **actifs à l'état haut** dont l'entrée
   passe par ~1 kΩ. C'est une hypothèse **non vérifiée** sur les modules réellement achetés. Si
   l'essai échoue, ne pas commander : la valeur de Rs (470 Ω) ou le type de module se revoit.
2. **JLCDFM** (`jlcdfm.com`, gratuit, compte JLCPCB connecté) sur
   `gerbers-n3-universal-v0.3.0.zip` — **à faire** (carte reroutée, nouveau contour). Attendu :
   9 fichiers reconnus, tout le cuivre au vert. Lignes *Danger* attendues, par analogie avec la
   v0.2.2 passée le 2026-10-30 **[à vérifier, les nombres changeront]** :
   - 2 × « tht to smd » : empreinte double de **D11** (DO-35 **ou** SOD-123, une seule posée) ;
   - « silkscreen to hole » et « silkscreen to pad » : sérigraphie contre des vias tentés ou au
     ras des pastilles (26 avertissements de sérigraphie au DRC KiCad, contre 64 en 0.2.3).
   *Warning* attendu : « silkscreen line width » (traits 0,15 mm = minimum exact). Les deux
   « short slot » du porte-fusible F1 ont disparu avec lui. Toute **autre** ligne touchant au
   cuivre → ne pas commander, me donner le rapport.
3. **Vue 3D dans KiCad** (`kicad/n3-universal.kicad_pro`, Alt+3) ou `exports/pcb-3d-dessus.png` :
   vérifier à l'œil les deux sites A1/A2 (un module enfiché à la fois, antennes vers
   l'extérieur), la rangée de borniers du bas et les six borniers de commande du haut
   (ouvertures vers le bord).
4. Vérifier au récapitulatif de commande : **1 oz**, **LeadFree HASL**, **278 × 91 mm**,
   « Mark on PCB : Remove Mark ».
5. **Devis PCB Maroc** (§4) : la carte n'a plus de fentes internes, ce qui levait l'objection
   principale. À demander si l'option locale t'intéresse.

## 1. JLCPCB (https://jlcpcb.com/) — valeurs du formulaire

| Option | Valeur | Note |
|--------|--------|------|
| Base Material | **FR-4** | |
| Layers | **2** | |
| Dimensions | **278 × 91 mm** | lues des gerbers à l'upload (278 × 135 en 0.2.3) |
| PCB Qty | **10** | une carte par profil + rechange : la carte ne sera pas refaite (5 = minimum) |
| Different Design | 1 | |
| Delivery Format | **Single PCB** | pas de panélisation |
| PCB Thickness | **1.6 mm** | |
| PCB Color | Green | le moins cher / délai mini ; libre |
| Silkscreen | White | |
| Surface Finish | **LeadFree HASL** ⚠️ | défaut = HASL **au plomb** — carte manipulée par des élèves |
| **Outer Copper Weight** | **1 oz** | défaut du formulaire. **Décision du 2026-10-30, ex-2 oz** (le 2 oz coûtait 58 $ sur 102 $ pour 10 cartes) : rev 0.3 : plus aucune piste de puissance 230 V sur la carte, le 1 oz ne limite plus que le 5 V (≤ 3 A) ; en 0.2.3 : pistes contacts 2,5 mm en 1 oz ≈ 4,5 A à +10 °C (IPC-2221, couche externe) — d'où la règle ≤ 1 kW résistif / 3 A moteur par canal ; chemin d'alimentation du DevKit 41 mΩ au lieu de 21 mΩ (≈ +10 mV à 0,5 A, négligeable devant la marge de 100-200 mV) ; bus 12 V 26 mΩ au lieu de 13 (≈ 80 mV à 3 A). Bonus : soudure à la main plus facile. Le `gbrjob` déclare 1 oz, mais le formulaire ne le lit pas : vérifier la case. Zone 230 V (pistes 2,5 mm) et budget résistif calculés en 2 oz. |
| Via Covering | Tented | défaut |
| Min via hole size | 0.3 mm | défaut sans surcoût |
| Board Outline Tolerance | ±0.2 mm | fraisage, défaut |
| **Confirm Production file** | **Yes** | grande carte, commande unique (+~1 j) ; plus de fentes internes en rev 0.3 |
| **Mark on PCB** (ex-« Remove Order Number ») | **Remove Mark** | défaut du formulaire en 2026-10, sans surcoût ; `JLCJLCJLCJLC` reste placé au dos au cas où. |
| Flying Probe Test | Fully Test | défaut, inclus |
| Gold Fingers / Castellated / Edge Plating | No | |

Surcoûts : surface > 100 mm (hors promo), LeadFree HASL (léger) — le 2 oz n'en fait plus partie.

⚠️ Le devis ci-dessous date de la 0.2.3 (278 × 135 mm). La 0.3 est un tiers plus petite : prix
des cartes et port **à relever de nouveau [à vérifier]**, attendus un peu plus bas.

**Devis du 2026-10-30** (compte connecté, FedEx Express 6-11 jours ouvrés, livraison au
Lycée Lyautey, 260 Bd Ziraoui, 20000 Casablanca) :

| | 5 cartes | 10 cartes (retenu) |
|---|---|---|
| 1 oz | 25,05 $ + 38,66 $ = 63,71 $ | **44,05 $ + 49,06 $ = 93,11 $** |
| 2 oz (pour mémoire) | 70,65 $ + 38,66 $ = 109,31 $ | 102,05 $ + 56,84 $ = 158,89 $ |

Port DHL / UPS au même moment : 51,59 / 54,05 $ (5 cartes), 70,48 / 79,48 $ (10 cartes).
Droits + TVA à l'arrivée ≈ 23 % de la valeur (§2).

Conformité aux capacités JLCPCB (page *Capabilities*, 2026-08-28 ; `tools/check_jlcpcb_caps.py`
sur la rev 0.3) : plus aucune fente fraisée non plaquée ; fentes plaquées du jack J2 (min 0,5) ;
151 vias 0,35 / 0,70 mm ; trou à trou ≥ 0,60 mm ; NPTH M3 (3,2 mm) séparés ; cuivre-bord ≥ 0,5 mm.

### Assemblage (PCBA) — non recommandé

Carte 100 % traversante, ~35 références *Extended* → loading fees qui dominent le coût pour
5 cartes ; pièces conditionnelles par rôle impossibles à mutualiser. Fichiers de
prévisualisation dans `exports/pcba/` (BOM socle + CPL ; ne JAMAIS faire poser les
conditionnelles ni A1/A2). Détails : `exports/pcba/README.md`.

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
| ~~Relais SRD-05VDC-SL-C (K1-K6)~~ | 0 | — | **rev 0.3 : retirés de la carte** (modules relais du commerce, achat local) |
| IRF4905PBF TO-220 (Q7, Q11, Q12) | 15 (+3) | **C2564** | 24 900 en stock ; repli IRF9540N C2575 (RDS plus élevé) |
| LD1117V33 TO-220 (U1) | 5 (+2) | **C283467** | ST, 1 = GND, 2 = OUT, 3 = IN, languette = OUT ; repli LM1117T-3.3 C498321 (même brochage) |
| BC337-40 TO-92 (Q8 ; Q1-Q6 retirés en rev 0.3) | 5 (+5) | **C713611** | ou Moussasoft (brochage C-B-E) |
| ~~1N4007 (D1-D4, D6, D7)~~ | 0 | — | **rev 0.3 : retirées** avec l'étage relais (les modules portent leur diode). Restent utiles hors carte : une par pompe DC, aux bornes de la pompe |
| **1N5822 (D5)** | 5 (+2) | **C2476** | **rev 0.3 : de retour, une par carte**, avec le cavalier JP22 en parallèle (0.2.2-0.2.3 : pont de fil) |
| 1N4744A zener 15 V (D9) | 5 (+2) | **C238928** | |
| TVS 1.5KE18A (D8) | 5 (+2) | **C1666858** | Littelfuse 1.5KE18A-B (1 000 en stock) ; équivalents DOWO C18198473, Liown C3034405 |
| TVS 1.5KE6.8A (D12) | 5 (+2) | **C412500** | ⚠️ 480 en stock : commander tôt ; repli P6KE6.8A DO-15 **C409413** (pattes 0,8 mm, mêmes trous) |
| Schottky clamp (D11) | 5 (+5) | **C19167** | BAT43W **SOD-123 (CMS)** sur les pads centraux de l'empreinte double — le BAT85 traversant (C549292) est **épuisé** chez LCSC ; un BAT85/BAT42 DO-35 d'une autre source se pose sur les trous |
| ~~HLK-20M05 (PS1)~~ | 0 | — | **rev 0.3 : retiré** (bloc 5 V externe) |
| ~~Varistance (RV1)~~ | 0 | — | **rev 0.3 : retirée** |
| ~~Porte-fusible 5×20 (F1) et cartouche T1A~~ | 0 | — | **rev 0.3 : retirés** ; la protection des charges 230 V est en amont des modules relais, hors carte |
| Barrette mâle 2,54 1×40 (JP1-JP22, TP) | 10 | **C2337** | sécable |
| Barrette femelle 2,54 1×40 (supports A1/A2) | 10 | **C9811** | 2 × 19 (A1, rev 0.2.3) + 2 × 22 par carte ; ou Moussasoft |
| Cavaliers 2,54 | 100 | **C2689176** | 19 par carte + rechange |
| Condensateurs 10 µF / 100 µF / 220 µF radiaux (C11, C12, C2, C5, **C14 220 µF**) | 5 + 15 + 5 | — | locaux ou LCSC (Basic) |

Tout le reste (résistances 1/4 W, 100 nF, LED 5 mm, borniers KF301/KF128 5,08 à 2, 3 et
4 pôles, jack DC-005, visserie M3, fil, **modules relais actifs à l'état haut**, bloc 5 V) :
**achat local** (§3). Rev 0.3 : le nylon n'est plus imposé (H1-H4, H6, H7).

## 3. Achats au Maroc (référence) — et ailleurs en Afrique

**Liste chiffrée par projet** (quantités, prix relevés, liens) : `achats/achats-n3-universal.xlsx`,
régénérée depuis la BOM par `python3 achats/build_achats.py`.

| Fournisseur | Quoi | Remarque |
|---|---|---|
| **Moussasoft** (Casablanca, moussasoft.com) | BC337, borniers KF301, barrettes, résistances, condensateurs, ESP32 | catalogue le plus large, prix unitaires bas |
| **Shop4Makers** (Casablanca) | ESP32 38 broches (WROOM-32E, 114 DH), ESP32-S3 N16R8, borniers | modules ESP32 : **acheter ici**, pas d'import |
| **A2itronic** | IRF4905, TO-220 courants, LD1117 parfois | vérifier le stock avant |
| **MicroPlanet** | résistances par lots, passifs | |
| **Jumia.ma** | modules relais 5 V 1, 2 ou 8 canaux (18 / 28 / 105 DH le 22/11/2026) | l'annonce ne dit pas s'ils sont actifs à l'état haut : en acheter un, faire l'essai (`MODULES_RELAIS.md` §8) |

Ailleurs en Afrique : la même carte se monte avec le colis LCSC du §2 + n'importe quel
revendeur local pour les passifs et les borniers à vis 5,08 (KF301 = standard mondial).
Aucune pièce n'exige un distributeur occidental ; les seuls « spéciaux » (IRF4905,
LD1117V33, 1.5KE, 1N5822) sont tous dans le colis.

## 4. PCB Maroc (https://pcbmaroc.com/) — devis email à demander

Le configurateur en ligne ne propose ni poids de cuivre ni finition. Depuis la rev 0.3 la carte
**n'a plus de fentes internes** : l'objection principale à une fabrication locale est levée.
Procédure :

1. Email à `contact@pcbmaroc.com` (+212 602 714-499, Technopark Tanger) avec
   `exports/gerbers-n3-universal-v0.3.0.zip` **et** ce cahier des charges explicite :
   - 278 × 91 mm, 2 couches, FR-4 1,6 mm ;
   - cuivre extérieur **1 oz (35 µm)** (déclaré dans le `gbrjob`) ;
   - 6 trous **non métallisés** 3,2 mm ; fentes **plaquées** du jack (1,0 mm) ;
   - vias 0,35 mm de perçage, 0,70 mm de pastille, recouverts de vernis ;
   - finition **sans plomb** (LeadFree HASL), tolérance contour ±0,2 mm ;
   - retirer le texte `JLCJLCJLCJLC` du dos (spécifique JLCPCB) ou l'ignorer.
2. Exiger une **confirmation écrite point par point** avant paiement (vias 0,35 mm et finition
   sans plomb surtout).
3. Sans confirmation → commander chez JLCPCB.

Calculateur en ligne au 2026-10-30, pour la 0.2.3 (278 × 135 mm, options non précisées) :
598 DH HT les 5, 1 088 DH HT les 10. Pour la 0.3 : **à relever [à vérifier]**.

## 5. Rappels de sécurité

- **Aucun 230 V sur la carte.** Les charges 230 V se raccordent aux contacts des **modules
  relais**, dans un coffret isolant fermé, avec protection en amont (fusible ou disjoncteur,
  différentiel 30 mA). Détail : [`MODULES_RELAIS.md`](MODULES_RELAIS.md) §6.
- Modules relais **actifs à l'état haut** uniquement. Un module actif à l'état bas collerait dès
  la mise sous tension, relais de chauffage compris.
- **JP5-JP10 : livrer SANS cavalier** (= AUTO). Haut force ON, bas force OFF. **JP7 (chauffage
  K3) et JP10 (K6, GPIO45 du S3) n'ont pas de position ON** : c'est voulu.
- **JP22** : ôté = source à 5,2 V, USB permis ; posé = jamais USB et source ensemble.
- Ne jamais brancher un bloc **12 V** sur le jack J2 (même fiche que le bloc 5 V) : repère
  « PAS DE 12V » sur la carte.
- (Rev 0.2.3, → remplacé en rev 0.3 : relais sur la carte limités par le cuivre 1 oz à 1 kW
  résistif / 3 A moteur par canal ; H1 et H5 en vis nylon ; porte-fusible à capot.)
