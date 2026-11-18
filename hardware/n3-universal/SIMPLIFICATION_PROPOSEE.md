# Simplification proposée — n3-universal (vers une rev 0.3)

> **Statut : proposition, en attente d'arbitrage (2026-11-18).** Rien n'est modifié dans le
> générateur, le PCB routé, les achats ni les firmwares. La rev **0.2.3 reste la seule version
> commandable** ; elle n'est pas commandée à ce jour. Ce document réunit les conversations du
> 2026-11-02 et du 2026-11-18 sur la simplification de la carte (sortir les relais et
> l'alimentation 230 V), pour que les choix soient pris **avant** de toucher `generate.py`.
> Le prompt de lancement du chantier est dans [`PROMPT_REV_0.3.md`](PROMPT_REV_0.3.md).

Chaque affirmation porte son niveau de preuve :
**[mesuré]** lu sur le PCB 0.2.3 ou le générateur · **[schéma]** déduit du schéma, non essayé ·
**[estimé]** ordre de grandeur sans placement refait · **[à vérifier]** non contrôlé.

## 1. Résumé

- **Décision de principe (Oliv, 2026-11-02)** : garder le site S3 (A2) et la microSD ; sortir
  de la carte l'alimentation 230 V **et** les relais (options 2 et 3 du §3).
- **Recommandation du 2026-11-18** : oui, mais en **gardant l'étage à transistor** sur la carte
  (BC337, résistances, LED, cavaliers JP5-JP10) et en ne retirant que le relais, sa diode et son
  bornier. La carte pilote alors un module relais du commerce **actif à l'état bas**, sans
  changer les firmwares et sans refaire le forçage manuel (§5).
- **Ce que ça rapporte** : la sécurité et la simplicité de fabrication, pas la place ni le prix
  (§2). Carte ≈ 278 × 90 mm au lieu de 278 × 135 (≈ −33 %) **[estimé]**, largeur inchangée.
- **Ce que ça coûte** : une rev 0.3 (générateur, routage secteur, gardes, simulations, achats,
  Fritzing, documents, JLCDFM). Aucun firmware à modifier.
- **Avant de lancer** : un essai sur table d'une heure (§7), et quatre décisions (§9).

## 2. Ce que montrent les mesures sur la 0.2.3

Positions lues dans `kicad/n3-universal.kicad_pcb` (contour 40..318 × 40..175, soit 278 × 135 mm,
375 cm²) **[mesuré]** :

| Zone | Étendue (mm) | Contenu |
|---|---|---|
| Bande relais | x 50..250, y 46..101 | J3-J6, J23, J24 (y 46..54), K1-K6 (y 59,5..79,5), étage de commande D/R/Q/LED/JP5-JP10 (y 85..101) |
| Coin secteur | x 247..317, y 40..102 | PS1 Hi-Link (36 × 60), F1, RV1, J27, trou H5 |
| Bande logique | x 34..320, y 102..173 | A1, A2 couché, alimentation 5 V, rail capteurs, ponts ADC, bloc 12 V, servos, SD, rangée de borniers |

- **Relais + coin secteur = 61 mm sur 135 de hauteur.** Au-dessus de la bande logique il ne reste
  sinon que J21, J22 et trois trous de fixation.
- **La largeur est fixée par la rangée de borniers du bas** : 15 borniers, 224 mm cumulés, étalés
  de x 50 à x 310, plus le passage de A1. Retirer le haut de la carte ne change pas la largeur.
- **La bande logique est déjà dense** : 92 cm² de composants (corps + dégagements) dans ≈ 202 cm².
  L'historique le confirme : en 0.2.2, un P-FET de plus dans le couloir du bas laissait 7 nets non
  routés, et un reroutage à blanc en laissait 2. La resserrer est un chantier à part (§6, scénario E).
- **A2 couché occupe 71 × 29 mm** (x 248..320). Il ne tient pas debout dans la bande actuelle
  (71 mm de long pour 60 mm disponibles au-dessus de la rangée de borniers).
- **Aucun des quatre profils ne pose A2 ni la SD** : `achats/inventaire.py`,
  `NEVER = {A2, J35, JP2, JP3, JP4, JP11}`.

Conséquences :

| Critère | Effet de la simplification |
|---|---|
| Coffret | **inchangé** : 278 mm de large impose toujours un coffret de 300 mm **[à vérifier chez le vendeur pour la profondeur]** |
| Prix du PCB | ≈ −15 à −20 $ sur une commande de 93 $ (10 cartes + FedEx) **[estimé]** |
| Prix des pièces | neutre : relais + diode + bornier retirés ≈ 22 DH par canal (131 DH pour six) contre un module 8 canaux à 105-119 DH (§8) |
| Sécurité | **plus de 230 V sur la carte que manipulent les élèves** |
| Fabrication | plus de 19 fentes internes, plus de vis nylon H1/H5, plus de règle de ligne de fuite, plus de limite « ≤ 1 kW par canal » liée au cuivre 1 oz |
| Risques levés | empreinte du HLK-20M05 (colonnes à 51 mm selon la lib KiCad, 44,7 mm selon le dessin du datasheet, perçages sans rattrapage) ; entraxe du porte-fusible (22,0 à 22,6 mm) |
| Fabricant | **PCB Maroc redevient possible** : c'étaient les fentes internes et la finition que son configurateur ne garantissait pas (`COMMANDE.md` §4) **[à confirmer par devis]** |

## 3. Les propositions du 2026-11-02 (rappel)

| Bloc | msp1 | n3pp | ffp5cs 230 V | ffp5cs 12 V |
|---|---|---|---|---|
| Site A2 (S3) et SD | non | non | non | non |
| Alim 230 V (Hi-Link, fusible, varistance, bornier) | non | non | oui | non |
| Relais K1-K6 | 0 | 1 | 6 | 6 |
| Bloc bus 12 V | non | non | non | oui |

| # | Proposition | Verdict au 2026-11-18 |
|---|---|---|
| 1 | Supprimer A2 et la SD reliée au S3 | **Écartée par Oliv** (garder la voie S3 : USB natif, PSRAM, firmwares ffp5cs S3 existants). Reste le seul levier sur la largeur (≈ −40 mm **[estimé]**) |
| 2 | Alim 230 V hors de la carte | **Retenue.** Ne demande aucun dessin : ffp5cs 230 V s'alimente comme n3pp, par un bloc 5 V sur J2 |
| 3 | Relais sur un module du commerce | **Retenue**, dans la variante du §5 |
| 4 | Carte relais fille conçue par nous | Non : un second design à maintenir (générateur, gardes, achats, Fritzing) pour deux unités ffp5cs |
| 5 | Garder 4 relais au lieu de 6 | Non : ≈ −10 %, la bande logique reste aussi large |
| 6 | Densifier la logique (borniers enfichables, module en étage) | Non : contraire à « borniers à vis partout », réparations plus difficiles |
| 7 | CMS et assemblage JLCPCB | Non : plus de soudure à la main au Maroc, pièces conditionnelles par profil mal gérées |

## 4. Comment s'activent les modules relais du commerce

Nos firmwares mettent la broche **à l'état haut pour allumer** (`on()` / `off()` de
`ffp5cs/include/actuators.h`). Le type de module compte donc.

| Type de module | S'allume quand IN est… | Commandé directement par le GPIO | Commandé par le collecteur du BC337 (§5) |
|---|---|---|---|
| Optocoupleur **actif à l'état bas** (le plus vendu, cavalier JD-VCC) | basse | non : logique inversée, impulsion possible au démarrage, relais parfois collé en 3,3 V | **oui** |
| Optocoupleur avec cavalier **H/L** | haute (H) ou basse (L) | oui, cavalier sur H (commande 3,3 V à vérifier sur l'exemplaire) | **oui, cavalier sur L** |
| Sans optocoupleur (simple transistor) | selon le modèle | à vérifier exemplaire par exemplaire | seulement s'il est actif à l'état bas |
| Relais statique (SSR, type G3MB-202P) | en général basse | inversion nécessaire | possible, mais 230 V alternatif seulement, ≤ 2 A, courant de fuite au repos : non retenu |

La recommandation du 2026-11-02 était la deuxième ligne, commandée directement par le GPIO. Elle
obligeait à refaire le forçage manuel (JP5-JP10 agit sur la base du transistor) et à garder une
résistance de rappel de 10 kΩ par ligne. La variante du §5 la remplace.

## 5. La variante recommandée : garder l'étage à transistor

Étage actuel d'un canal (`relay_channel()` de `generator/generate.py`) **[mesuré]** :

```
GPIO ──[Rb 1k]──┬── base BC337 ── collecteur (REL{n}_SW) ──> bobine K ──> +5V
                │        │                  │                (D 1N4007 en roue libre)
             [Rp 10k]  émetteur ── GND     LED témoin + R 1k ──> +5V
                │
            JP5..JP10 : sans cavalier = AUTO ; 1-2 = ON forcé (+3V3 via Rf 1k) ; 2-3 = OFF forcé
```

On retire **K, D et le bornier de charge** ; on garde Rb, Rp, Q, la LED et sa résistance, Rf et le
cavalier. Le collecteur devient une **sortie à collecteur ouvert**, reliée à l'entrée IN d'un
module actif à l'état bas **[schéma]** :

| Propriété | Pourquoi |
|---|---|
| Firmwares inchangés | GPIO haut → transistor passant → IN à la masse → relais collé |
| Relais éteint au démarrage | Rp tire la base à la masse tant que le GPIO flotte ; collecteur ouvert = IN non tirée = relais au repos |
| Forçage AUTO / ON / OFF conservé | JP5-JP10 agissent sur la base, rien ne change ; JP7 (chauffage) reste sans position ON |
| Plus de question 3,3 V / 5 V | le transistor commute le 5 V du module, le GPIO ne voit que Rb |
| LED de la carte conservée | elle reste entre +5 V et le collecteur : témoin de la commande ; les LED du module montrent le relais |
| Fil IN débranché ou coupé | relais au repos |

**Déjà disponible sur la 0.2.3** : le header **J38 « CMD SENSE »** porte exactement ces signaux —
broches 1-6 = `REL1_SW`..`REL6_SW`, 7 = GND, 8 = +5 V **[mesuré]**. La piste +5 V qui y arrive
fait 1,2 mm de large (les collecteurs 0,3-0,4 mm) : elle porte sans difficulté les 0,43 A de six
bobines collées. Sur une 0.2.3 on peut donc essayer le principe sans rien redessiner : K, D et
borniers de charge non posés, module relié à J38. Le bornier de distribution +5 V est **J18**
(J19 distribue le 3,3 V commuté, à ne pas confondre).

Limites à connaître :

- Un module **actif à l'état haut seul** ne convient pas (la LED de la carte ferait office de
  rappel au 5 V : logique inversée).
- L'isolement des modules bon marché est inégal : les pistes 230 V passent parfois très près de la
  basse tension, sans fente. **Ouvrir un module et regarder le dessous avant d'en acheter plusieurs.**
- Sans diode de roue libre sur la carte, le collecteur ne doit **pas** commander directement une
  bobine nue (voir décision D4, §9).

### Ce que deviendrait la carte

- **Part** : K1-K6, D1-D4, D6, D7, J3-J6, J23, J24, PS1, F1, RV1, J27, H5, les pistes secteur
  tracées en dur, les 19 fentes, les règles `Mains` du `.kicad_dru`, la zone d'exclusion du plan
  de masse, la consigne de vis nylon.
- **Reste** : toute la bande logique, et l'étage de commande (y 85..101).
- **Bouge** : J21, J22 et les trous H1, H2 (aujourd'hui au-dessus de la bande logique) ; le bord
  haut descend de y 40 à y ≈ 84.
- **Arrive** : un connecteur de commande 8 points à la place de J38 (décision D3).
- **Taille** : ≈ 278 × 90 mm, ≈ 253 cm², **≈ −33 %** **[estimé]** (≈ 278 × 87 si les diodes de roue
  libre disparaissent aussi).

### Ce qu'il faut autour de la carte

| Profil | Module relais | Alimentation |
|---|---|---|
| msp1 | aucun | inchangée (18650 + boost sur J1) |
| n3pp | 1 canal (≈ 50 × 26 mm) | inchangée (chargeur 5 V sur J2) |
| ffp5cs 230 V | 8 canaux (≈ 138 × 56 mm), ou 4 + 2 | **bloc 5 V ≥ 2 A, mieux 3 A, sur J2** (budget `ALIMENTATION.md` §5 : ≈ 0,5 A continu, pointes ≈ 1,5 A) ; variante rail DIN type Mean Well HDR-15-5 |
| ffp5cs 12 V | 8 canaux, ou 4 + 2 | inchangée (buck sur J36/J37 → J1) |

Tailles de modules : ordres de grandeur courants **[à vérifier sur les modèles achetés]**.

**Pour ffp5cs, l'encombrement total ne baisse pas** : la carte rétrécit, mais le coffret doit loger
le module et le bloc 5 V. L'avantage y est la sécurité et la liberté de placement. Le vrai gain de
place est pour msp1 (plus aucun bloc inutile) et n3pp (un petit module 1 canal).

**Protection des charges** : sur la 0.2.3, F1 ne protège que le primaire du Hi-Link, pas les
charges. Rien ne change donc de ce côté : le circuit 230 V reste à protéger en amont
(fusible ou disjoncteur), coffret isolant et presse-étoupes obligatoires.

## 6. Autres scénarios envisagés

| # | Scénario | Avis |
|---|---|---|
| A | **Commander la 0.2.3 telle quelle** et ne poser ni relais ni Hi-Link ; module sur J38, bloc 5 V sur J2 | Solution d'attente valable : aucun risque de conception, cartes disponibles tout de suite. Mais la bande vide, les fentes et l'import restent |
| B | **Rev 0.3 = 0.2.3 amputée** du haut, étage à transistor conservé (§5) | **Recommandée** : la bande logique ne bouge pas, son cuivre validé sert d'amorce au routage (méthode de la 0.2.2) ; une seule commande étant prévue, autant commander la carte voulue |
| C | Carte sécable (bande relais détachable par rainure ou pastilles) | Non : bords rugueux près du 230 V, facturation possible en panneau |
| D | Deux cartes : 0.2.3 complète pour ffp5cs, carte légère pour msp1 / n3pp | Non : deux designs à maintenir, contraire au but de la carte universelle |
| E | Refonte compacte, borniers sur deux bords, A2 redressé (≈ 200-240 × 100 mm **[estimé]**) | Seulement si un coffret plus petit devient un objectif : placement et routage complets à refaire |
| F | Module MOSFET au lieu de relais pour les pompes du profil 12 V | **Piste à vérifier.** Un relais maintenu coûte ≈ 10 Wh par jour sur l'AGM (`ALIMENTATION.md` §6) ; l'étage de sortie hors carte permet de choisir par profil. La décision N4 du 2026-10-17 (« relais conservés ») reste valable tant que ce n'est pas essayé |

## 7. Essai sur table à faire avant toute décision

Matériel : un DevKit ESP32, un BC337, une résistance de 1 kΩ (base) et une de 10 kΩ (base → masse),
un module relais à optocoupleur actif à l'état bas, une alimentation 5 V. Le module est alimenté
par le même 5 V que le montage.

| # | Essai | Attendu |
|---|---|---|
| 1 | Mise sous tension, reset, téléversement | le relais ne colle **jamais**, même brièvement |
| 2 | GPIO haut puis bas, plusieurs cycles | collage et décollage francs |
| 3 | Fil IN débranché, module alimenté | relais au repos |
| 4 | Montage éteint, module alimenté | relais au repos |
| 5 | Base forcée à +3,3 V par 1 kΩ, puis à la masse (équivalents de JP en 1-2 et 2-3) | ON forcé, OFF forcé, quel que soit le GPIO |
| 6 | Courant dans IN au multimètre | quelques mA (loin des 800 mA du BC337) |
| 7 | Dessous du module | écart visible entre pistes 230 V et basse tension ; fente si possible |

Si un essai échoue, la variante du §5 est à revoir avant de lancer la rev 0.3.

## 8. Prix relevés (2026-11-18)

Modules relais 5 V sur Jumia Maroc, prix affichés dans les résultats de recherche, non vérifiés en
boutique : 4 canaux à optocoupleur 70 DH ; 8 canaux 105 DH ; 8 canaux à optocoupleur 119 DH.
Moussasoft, Shop4Makers et A2itronic : non relevés **[à vérifier]**. À reporter dans
`achats/prix.py` (avec sa `DATE`) quand la décision sera prise.

Pièces de la carte, d'après `achats/prix.py` du 18/10/2026 : relais SRD-05 18 DH, 1N4007 0,90 DH,
bornier 3 points 3 DH ; Hi-Link 90 DH, porte-fusible 4 DH, varistance 4 DH ; chargeur 5 V 2 A 50 DH.

## 9. Décisions à prendre

| # | Question | Options | Recommandation |
|---|---|---|---|
| D1 | Quelle carte commander ? | 0.2.3 telle quelle (scénario A) / rev 0.3 (scénario B) | **rev 0.3**, sauf besoin urgent de cartes |
| D2 | Commande des modules | directe par GPIO, module H/L sur H (2026-11-02) / **étage à transistor conservé**, module actif bas | **étage conservé**, si l'essai du §7 passe |
| D3 | Connecteur de commande (6 lignes, +5 V, masse) | header 1×8 Dupont comme J38 / bornier à vis 8 points | **bornier à vis** (cohérent avec « borniers à vis partout », un Dupont se déboîte) |
| D4 | Diodes de roue libre D1-D7 | supprimées / empreintes gardées non posées (permettent de commander une bobine nue ≤ 100 mA) | à trancher : ≈ 3 mm de hauteur en jeu |
| D5 | Fabricant | JLCPCB (import, FedEx) / PCB Maroc (devis à demander, sans fentes) | demander le devis PCB Maroc une fois les gerbers 0.3 sortis |
| D6 | Largeur | 278 mm inchangés / réorganiser la droite de la carte (scénario E partiel) | **inchangée** pour la 0.3 |

Décisions déjà prises, à ne pas rouvrir : S3 et SD conservés (2026-11-02) ; alimentation 230 V et
relais hors de la carte (2026-11-02) ; borniers à vis 5,08 partout (2026-10-06) ; pas de CMS ni
d'assemblage usine ; firmwares et GPIO inchangés.

## 10. Ce qu'une rev 0.3 oblige à répercuter

Règle « Propagation des changements » du `CLAUDE.md` racine — tout dans la même PR :

| Fichier | Changement |
|---|---|
| `generator/generate.py` | `relay_channel()` sans K, D, bornier ; bloc PSU supprimé ; connecteur de commande ; contour ; `SCH_TEXTS`, `SILK`, `gen_bom()`, `REV` |
| `generator/route_universal.py` | `add_mains_tracks`, `check_mains_gap`, `check_mains_plane_gap`, fentes : à retirer ou neutraliser |
| `kicad/n3-universal.kicad_dru` | règles `mains_*` |
| `tools/` | `check_pinmap_vs_firmware.py` (topologie du forçage), `check_pcb_clearance.py` (corps 3D), `check_jlcpcb_caps.py` (fentes), `annotate_pcb_roles.py`, `sync_pcb_sim_fields.py` |
| `simulation/` | `sim1_relay.py` (la charge du collecteur devient l'entrée d'un module), `SIMULATION.md` |
| `achats/` | `inventaire.py` (`RELAY1`, `RELAY6`, `FFP`, `POP`, `OFF` : modules relais, bloc 5 V, plus de fusible ni de cordon sur la carte), `prix.py` + `DATE`, `classeur.py`, puis `build_achats.py` |
| `fritzing/` | `WIRING` : module relais et bloc 5 V dessinés ; régénérer |
| Documents | `README.md` (section Rev 0.3, cavaliers), `COMMANDE.md` (dimensions, plus de fentes, devis, PCB Maroc), `ALIMENTATION.md` §5, `COMPOSANTS_FFP5CS.csv`, `../README.md`, `../VERIFICATION.md` |
| `exports/` | gerbers v0.3.0, PDF, BOM/CPL ; JLCDFM à refaire par Oliv |
| `.github/workflows/hardware-ci.yml` | étapes liées au secteur |
| Firmwares, `pinmap_universel_propose.json` | **rien** (GPIO et polarité inchangés) |

Les sections « Rev x.y » passées du README, `AUDIT-2026-08-28.md` et `EVOLUTIONS_PROPOSEES.md`
restent telles quelles (archives datées), avec au besoin « → remplacé en rev 0.3 ».
