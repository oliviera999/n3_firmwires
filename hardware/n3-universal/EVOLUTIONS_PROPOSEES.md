# Évolutions proposées — n3-universal (prochain spin)

> **Statut : proposition, en attente d'arbitrage.** Rien n'est modifié dans le générateur,
> le PCB routé ni les firmwares. Ce document prépare le prochain spin de la carte
> (rev 0.2 ou suivante) pour que les choix soient pris **avant** de toucher `generate.py`.
> Il complète la check-list rev 0.2 de [`AUDIT-2026-08-28.md`](AUDIT-2026-08-28.md) §4
> (qui reste la liste des correctifs de la rev 0.1) par des **évolutions fonctionnelles**.

Deux sujets, plus des suggestions annexes :

- **A.** un **sélecteur AUTO / ON par canal relais** (header 3 points + cavalier, sérigraphie
  `AUTO ● ON`) pour forcer manuellement un relais sans passer par le firmware ;
- **B.** une revue de la **connectique capteurs / actionneurs** (borniers à vis, à ressort,
  JST-XH, enfichables…) zone par zone, avec options et recommandation.

Chaque option se termine par une **décision à prendre** ; le §4 les récapitule.

---

## 0. Périmètre et rappels

- Carte cible : **`n3-universal`** (décision actée : elle remplace à terme
  `n3pp-msp-commun`, `ffp5cs-wroom-prod` et `ffp5cs-wroom-prod-230v`, qui restent gelées
  et commandables). Les mêmes évolutions sont portables sur `ffp5cs-wroom-prod-230v`
  (même fonction `relay_channel()` dans son générateur) si un spin de cette carte est un
  jour décidé ; ce n'est pas proposé ici.
- Étage relais actuel, identique sur les 6 canaux K1..K6 (`generate.py` →
  `relay_channel()`) :

  ```
  GPIO ──[Rb 1k]──┬── base BC337 ── collecteur ──> bobine SRD-05 ──> +5V
                  │        │                          │ (1N4007 roue libre)
               [Rp 10k]  émetteur ── GND           LED témoin + 1k
  ```

  Relais **actif HAUT**, pull-down 10 k sur la base : état sûr au boot, y compris sur
  les broches de strapping (GPIO2/15 sur les cartes historiques, GPIO45 côté S3 ici).
- Rails 3,3 V : `+3V3` = sortie **permanente** de l'AMS1117 du DevKit ; `+3V3_SW` =
  rail capteurs **commuté** par GPIO13 (Q7), coupé en veille sur les profils batterie
  msp/n3pp (JP1 ôté). Cette distinction compte pour l'évolution A.
- Budget GPIO : **aucune broche libre** sur n3-universal (WROOM : GPIO12 = SD_MISO ;
  S3 : 26 nets, 0 libre). Toute fonction nouvelle qui « lit » quelque chose passe donc
  par l'I2C (ports libres J14/J21/J22/J28), pas par un GPIO.

---

## 1. Évolution A — sélecteur AUTO / ON par canal relais

### 1.1 Principe électrique

Insérer un header 3 points **entre le net GPIO et la résistance de base** de chaque canal.
Le cavalier choisit ce qui alimente la base du transistor :

```
                 JPn (1×3, pas 2,54)
  GPIO (net Kn) ──o 1  AUTO
                    2 ●──────[Rb 1k]──┬── base BC337 ──> relais
  +3V3 permanent ──o 3  ON            │
                                   [Rp 10k] ── GND
```

| Cavalier | Base pilotée par | Effet | Usage |
|---|---|---|---|
| **1-2** (défaut, livré ainsi) | GPIO du module | comportement actuel : le firmware commande | production |
| **2-3** | `+3V3` permanent | relais **collé** dès la mise sous tension, quoi que fasse le firmware | secours, test de charge, maintenance |
| **retiré** | rien — `Rp` 10 k tire la base à GND | relais **au repos** quoi que fasse le firmware | consignation d'une charge, test firmware sans charge |

Le cavalier est un **sélecteur** (rupture avant fermeture, un shunt 2,54 mm ne peut relier
que deux broches adjacentes) : il est impossible de mettre `+3V3` en parallèle sur la
sortie GPIO. C'est ce qui rend le montage sûr pour l'ESP32.

### 1.2 Vérifications électriques

- **Courant de base en ON** : (3,3 − 0,7) V / 1 k ≈ **2,6 mA**. Avec le BC337-40
  (h<sub>FE</sub> ≥ 250) le transistor sature largement pour les ~72 mA de la bobine. Rien
  à changer dans le canal.
- **Charge sur `+3V3`** : 6 canaux forcés ON = ~16 mA sur l'AMS1117 du DevKit. Négligeable.
- **Pourquoi `+3V3` et pas `+5V`** : 5 V sur la base via 1 k fonctionnerait aussi, mais on
  garde le net `+3V3` pour qu'une erreur de câblage vers le module (fil volant, interposeur,
  sonde) ne dépasse jamais l'absolu maximum 3,6 V d'un GPIO.
- **Pourquoi `+3V3` permanent et pas `+3V3_SW`** : sur les profils batterie, `+3V3_SW`
  tombe en veille ; un relais « forcé ON » qui retomberait à chaque deep sleep n'est pas un
  forçage. Le net `+3V3` (broches 3V3 du DevKit, J17) est déjà routé côté logique.
- **Broches de strapping** : en position 2-3 ou retiré, le GPIO du module est **déconnecté**
  de la base ; en 1-2 rien ne change par rapport à la rev 0.1. Le boot n'est affecté dans
  aucune position. La consigne S3 « ne rien raccorder qui tire GPIO45 haut au boot » est
  d'ailleurs **mieux tenue** qu'aujourd'hui : en forçage, la base n'est plus reliée à GPIO45.
- **Cavalier perdu** = canal OFF (pas ON). C'est le comportement de sécurité voulu.
- **Zone 230 V** : le header est un net 3,3 V dans la colonne de commande (y ≥ 86 mm),
  loin de la bande secteur (contacts à y ≤ 79,5, plan GND repoussé à y84). La règle
  `.kicad_dru` (cuivre Mains ≥ 3 mm du reste) s'applique sans adaptation.

### 1.3 Chauffage : ON forcé ou pas ?

Le forçage ON du canal **K3 (RADIATEURS ffp5cs)** court-circuite l'hystérésis de
`HeaterOrchestrator` : plus rien ne borne la température de l'eau. Trois options :

| Option | Description | Pour | Contre |
|---|---|---|---|
| **A3-a** | même header 3 points sur les 6 canaux, avertissement sérigraphié sur K3 | générateur uniforme, test de résistance chauffante possible | un cavalier mal mis = eau surchauffée |
| **A3-b (recommandée)** | header 3 points sur K3 mais **broche 3 non câblée** (comme la broche 3 de JP1) : K3 ne connaît que AUTO / OFF | impossible de forcer le chauffage, même empreinte, même BOM | on perd le test manuel de la résistance (faisable par le web local : `startHeaterManualLocal`) |
| A3-c | pas de header sur K3 | plus simple | canal non consignable (pas de OFF forcé), incohérent avec les autres |

Sur n3-universal, K3 n'est le chauffage **que pour ffp5cs** (n3pp/msp n'utilisent pas K3
aujourd'hui). A3-b se fait au niveau du générateur par un paramètre `force_on=False`
sur le canal 3 ; si un futur rôle a besoin du ON sur K3, un fil de pontage suffit.

> **Décision A3** : A3-a / **A3-b** / A3-c.

### 1.4 Sérigraphie

- Repère composant : `JP5`..`JP10` pour K1..K6 (JP1..JP4 existent). Pour l'utilisateur, le
  libellé lisible est **le nom du canal** : `K1 AUTO ● ON` … `K6 AUTO ● ON`, texte
  **≥ 1 mm de haut, trait ≥ 0,15 mm** (contrainte `GBR-02`), **dégagé des pads**
  (`GBR-06`), broche 1 = pad carré côté `AUTO`.
- Sur K3 en option A3-b : `K3 AUTO ● —` et « ON interdit (chauffage) » à côté.
- Une ligne d'explication commune, une seule fois, près de la rangée des cavaliers :
  `JP5-10 : 1-2 AUTO (firmware) | 2-3 ON force | ote = OFF force`. Même style que les
  textes JP1 / JP SD existants (`SILK` de `generate.py`, hauteur 0,8).
- Rappel utile au même endroit : « le firmware ignore la position du cavalier » (§1.6),
  jusqu'à l'évolution A2 (§1.7).

### 1.5 Implémentation dans le générateur (quand go)

Tout tient dans `relay_channel()` de `generator/generate.py` ; le reste du pipeline
(routage, tidy, export, gardes) est inchangé.

1. **Nouveau net** `REL{n}_CMD` : la résistance de base `Rb` passe de
   `{"1": gpio_net, ...}` à `{"1": f"REL{n}_CMD", ...}`.
2. **Nouveau composant** dans la liste retournée :
   ```python
   dict(ref=f"JP{n + 4}", sym="CONN_03", value="Jumper AUTO/ON",
        fp="PinHeader_1x03_P2.54mm_Vertical",
        desc=f"Selecteur K{n} : 1-2 AUTO (GPIO, defaut) ; 2-3 ON force (+3V3) ; ote = OFF force",
        sch=(bx - 6, by), pcb=(col, 105, 90),
        nets={"1": gpio_net, "2": f"REL{n}_CMD", "3": "+3V3"}),
   ```
   Option A3-b : pour `n == 3`, ne pas câbler la broche 3 (`nets` sans `"3"`), même
   mécanisme que JP1. Empreinte déjà vendorée (`PinHeader_1x03_P2.54mm_Vertical`).
3. **BOM** (`gen_bom()`) : la ligne « JP1-JP4 (cavaliers) » devient JP1-JP10, quantité
   **12** (10 posés + 2 rechange), libellé « livrer JP5-JP10 en 1-2 (AUTO) ».
   Prendre des **shunts à languette** (pull-tab) : manipulables sans pince, contexte élèves.
4. **`SCH_TEXTS`** : compléter le bloc « canaux relais » (AUTO / ON / OFF, chauffage).
5. **`SILK`** : les 6 libellés + la ligne d'explication (§1.4).
6. **Gardes** : `check_pinmap_vs_firmware.py` compare les nets des **pads des sites A1/A2**
   aux `pins.h` — le net GPIO reste sur ces pads, rien à changer. Lancer quand même
   `tools/check_pcb_clearance.py` après placement (corps 3D + couloir d'insertion du shunt).
7. **Documents** : `README.md` (tableau des cavaliers), `COMMANDE.md` si la sérigraphie
   change de gabarit, `exports/pcba/` (les shunts ne sont pas posés machine),
   `VERIFICATION.md`, et la note « le firmware ignore le cavalier ».

### 1.6 Placement

Chaque colonne de commande (x = k_x − 7, k_x ∈ {58, 92, 126, 160, 194, 228}) empile
D (y86), Rb (y91), Rp (y96), R LED (y100). Le header **couché à 90° sous la colonne
(y ≈ 105, 7,6 mm de large)** est la place naturelle : lisible, au-dessus des sites modules,
sur la même verticale que le relais qu'il commande. À valider sur le PCB placé : le
keepout antenne du site A1 (texte à y≈101,5 vers x≈113) et le jack J2 (coin x48/y116)
sont les deux voisins à surveiller. Repli si ça frotte : décaler d'un cran la colonne
(D à y84, header à y107) — la bande y84..y86 est libre de cuivre logique par construction.

> **Décision A1** : go / no-go sur le sélecteur AUTO / ON (6 canaux, générateur).

### 1.7 Ce que le firmware ne verra pas — et l'option de retour d'état (A2)

En position 2-3 ou « retiré », le GPIO ne pilote plus rien mais **le firmware n'en sait
rien** : `etatPompeAqua`, `etatHeat`, `etatAux1/2` (ffp5cs), l'état pompe n3pp et les
alertes de cohérence (pompe « ON » mais niveau qui ne bouge pas, chauffage « OFF » mais
température qui monte) deviennent faux. C'est acceptable pour un secours court, pas pour
un forçage qui dure.

Sans GPIO libre, le retour d'état passe par un **expandeur I2C** :

- **Empreinte `U?` PCF8574 (DIP-16, socketé, DNP par défaut)** sur le bus I2C
  (`+3V3_SW`, pull-ups déjà présents), adresse 0x20 (A0-A2 à GND ; 0x27 sur les modules
  du commerce). 6 entrées P0..P5 lisent les nets `REL{n}_CMD` à travers **10 k série**
  (protection ; P6/P7 libres, ou réservées à « cavalier présent » si on veut distinguer
  OFF forcé d'un GPIO bas — non nécessaire).
- Variante sans empreinte : un **header 1×8 « CMD SENSE »** (`REL1..6_CMD`, GND, `+3V3_SW`)
  et un module PCF8574 enfiché sur J28. Moins propre, mais zéro composant sur la carte.
- Le niveau lu vaut 3,3 V en ON forcé, 0 V en OFF forcé, et copie le GPIO en AUTO. Le
  firmware compare à l'état commandé : **divergence = mode MANUEL** sur ce canal.
- Alimenté par `+3V3_SW` : en veille (profils batterie) l'expandeur est coupé, ce qui est
  correct (on ne lit qu'au réveil, comme le DS3231).

Côté firmware (hors périmètre carte, chantier séparé, un bump par firmware concerné) :

| Firmware | Où | Quoi |
|---|---|---|
| ffp5cs | `RelayController` / `Pump…Controller` (`actuators.h`), `gpio_mapping.h` | lecture PCF8574 à chaque cycle, flag `manual` par canal, publication (`etatManuelK{n}` ou bit dans le JSON existant), affichage OLED « MAN », **suspension des alertes de cohérence** du canal |
| n3pp | `main.cpp` / `n3pp_network.cpp` (`POMPE` = K1) | même logique sur 1-2 canaux |
| msp | — | pas de relais métier aujourd'hui |
| serveur n3_serveur | tables `*Data*` | colonne/clé optionnelle « manuel » (contrat à aligner, cf. `docs/`) |

> **Décision A2** : rien / empreinte PCF8574 DNP (**recommandée** : 16 pads, ~2 cm²,
> aucune contrainte) / header CMD SENSE seul.

### 1.8 En attendant le spin : interposeur DevKit (sans modification du PCB)

Le DevKit est socketé (2 × 1×15). Un **interposeur** entre le module et ses supports
donne la même fonction sur les cartes existantes :

- deux barrettes mâles 1×15 dessous, deux femelles 1×15 dessus, **toutes broches
  traversantes sauf celles des relais** (n3-universal WROOM : 16/17/18/19/23/25 ;
  cartes ffp5cs historiques : 16/18/2/15 + 23/25 sur la 230 V) ;
- pour chaque broche coupée, un header 1×3 : 1 = broche du DevKit, 2 = broche du support
  (vers la carte), 3 = `3V3` du DevKit. Même table de positions qu'au §1.1 ;
- une plaque à trous 2,54 suffit ; ou un mini-PCB dédié (~50 × 30 mm) généré à part si
  on veut le distribuer aux élèves.

Même limite : le firmware ne voit pas le forçage (§1.7), et il n'y a pas d'entrée libre
pour un retour d'état sur n3-universal (sur les cartes historiques, IO35/36/39 de J17
peuvent lire les broches 2 des headers via 10 k).

---

## 2. Évolution B — connectique capteurs / actionneurs

### 2.1 Inventaire rev 0.1, par zone

| Zone | Connecteurs | Type actuel | Ce qui s'y branche |
|---|---|---|---|
| **Charges (secteur ou 12/24 V)** | J3-J6, J23, J24 | bornier à vis 3 p, **5,08 mm** | câble 1,5 mm² H05VV-F (pompes, chauffage, lumière, AUX) |
| **Alimentations** | J1, J2, J18, J19, J25, J26, J27, J36, J37 | bornier à vis 2 p 5,08 mm + jack 5,5/2,1 | alim 5 V, bus 12 V, secteur (J27), sonde batterie, buck, distributions |
| **Ultrasons** | J7, J8, J9 | **JST-XH 4 p** (2,5 mm) | HC-SR04 / JSN-SR04T (module à header mâle 2,54) |
| **DHT / pluie** | J10, J29, J30 | **JST-XH 3 p** | DHT11/22 (3 fils ou module), pluie DO |
| **Sondes à fils nus** | J11 (DS18B20), J12 (LDR), J31-J34 (ADC A-D), J25 (VBAT) | bornier à vis 2-3 p **5,08 mm** | câble souple 0,2-0,5 mm², 2-3 conducteurs |
| **Servos** | J15, J16 | header mâle 1×3 2,54 | connecteur servo standard |
| **Modules I2C / OLED / SD** | J13, J14, J21, J22, J28, J35 | support femelle 1×4 / 1×6 | modules à header mâle, enfichés directement |
| **Service / rails** | J17, J20 | header mâle 1×6 | Dupont |

Deux constats :

1. La carte mélange **trois familles** pour les capteurs (JST-XH à sertir, borniers 5,08,
   headers 2,54) alors que la promesse est « zéro soudure, tout en borniers ou Dupont ».
   Le JST-XH impose une **pince à sertir** (ligne d'outillage de `ACHATS.md`) ou des câbles
   pré-sertis, et les borniers 5,08 sont surdimensionnés pour des fils de 0,25 mm².
2. **Aucun détrompage** sur les borniers (constat `SEC-02` : pas de +/−). Contexte élèves,
   fils fins, humidité d'aquaponie : c'est là que se jouent les erreurs de câblage.

### 2.2 Critères

Sans soudure ni outil spécial à l'usage · **détrompage** ou au moins marquage · tenue aux
vibrations / manipulations répétées (pompes, élèves) · section de fil réelle (0,2-0,5 mm²
capteurs, 1,5 mm² charges) · humidité (aquaponie : contacts étamés, pas de lame nue) ·
encombrement (la carte fait déjà 278 × 120 mm) · **empreinte disponible dans la lib KiCad
8.0.9 vendorée** (sinon empreinte à dessiner et à vérifier) · coût et disponibilité
(JLCPCB/LCSC, fournisseurs FR, PCB Maroc).

### 2.3 Options de connecteurs « signal » (2 à 4 fils)

| Option | Exemples | Outil | Détrompage | Fil | Vibrations | Encombrement | Coût/pos | Empreinte KiCad |
|---|---|---|---|---|---|---|---|---|
| **B1** bornier à vis 5,08 (actuel) | KF301 / DG301 | tournevis | non | 0,5-2,5 mm² | moyen (vis se desserrent) | 5,08 / pos | ~0,10 € | vendorée |
| **B2** bornier à vis 3,5 | KF350 / DG350 | tournevis | non | 0,2-1,5 mm² | moyen | 3,5 / pos | ~0,10 € | lib KiCad (`TerminalBlock_Phoenix_MPT-0,5-*`, `TerminalBlock_RND_*`) |
| **B3** bornier à ressort **push-in** 3,5 avec bouton | Wago 2601/2604, Phoenix PTSA 0,5, Degson DG141R/KF141R (2,54) | aucun (bouton pour fil souple) | non | 0,14-1,5 mm² | **bon** (ressort) | 3,5 / pos | ~0,25-0,40 € | Wago/Phoenix dans la lib KiCad (à vérifier réf. par réf.) ; DG141 : à dessiner |
| **B4** bornier **enfichable** 3,81 (embase + fiche à vis) | 2EDG 3.81 / Phoenix MC 1,5 | tournevis sur la fiche, hors carte | oui (ergots de codage) ou par forme | 0,2-1,5 mm² | bon (verrouillage) | 3,81 / pos + 8 mm de haut | ~0,50-0,80 € | lib KiCad (`TerminalBlock_Phoenix_MC-*`, `TerminalBlock_Xinya_XY308-*`) |
| **B5** JST-XH (actuel) | B3B/B4B-XH-A | pince à sertir **ou** câbles pré-sertis | **oui** (clip) | 0,08-0,33 mm² serti | bon (verrou) | 2,5 / pos | ~0,05 € + câble | vendorée |
| **B6** header 2,54 + Dupont | — | aucun | non | — | **mauvais** (se déboîte) | 2,54 / pos | ~0,02 € | vendorée |
| **B7** connecteur normalisé capteur | Grove (HY2.0 4 p), Qwiic (JST-SH) | câbles du commerce | oui | — | bon | compact | modules Grove | lib KiCad |

Notes :

- B3 : le **push-in** n'accepte un fil souple qu'avec embout ou via le bouton ; la
  variante « avec bouton » (Wago 2601, PTSA) est celle qui convient aux câbles DS18B20/LDR.
- B4 est la solution des automates industriels : on câble la fiche à la table, on l'enfiche
  ensuite ; une fiche débranchée = charge consignée. Elle existe aussi en **5,08 mm 250 V/10 A**
  (2EDG 5.08 / Phoenix MSTB) pour les charges.
- B7 n'a de sens que si les capteurs sont eux-mêmes Grove/Qwiic : HC-SR04, DHT et DS18B20
  ne le sont pas. Non retenu.

### 2.4 Analyse par zone et recommandation

| Zone | Recommandation | Pourquoi | Alternative |
|---|---|---|---|
| **Charges** (6 canaux) | **Garder le bornier à vis 5,08** (B1), avec les compléments déjà listés (`COM = PHASE`, boîtier, presse-étoupes) | 1,5 mm², 250 V, empreinte validée dans la zone secteur avec la règle 3 mm : ne pas rouvrir ce dossier | **B4 en 5,08** (2EDG/MSTB) si on veut pouvoir **débrancher une charge sans tournevis** dans la zone 230 V : mêmes pas et pads, corps plus haut, ~1 € de plus par canal, **fentes et 3 mm à revalider** |
| **Alimentations** | Garder B1 + jack ; ajouter le marquage +/− (`SEC-02`) | courants 3 A, fils 1 mm² | J25/J36/J37 en B3 ou B4 pour l'homogénéité de la bande basse (facultatif) |
| **Sondes à fils nus** (DS18B20, LDR, ADC A-D, VBAT) | **Passer en B3 (push-in 3,5 avec bouton)** — 7 connecteurs | c'est la zone où les fils sont fins, manipulés par les élèves, et où les vis 5,08 se desserrent ; zéro outil ; gain de largeur ~30 % sur la bande basse | B2 (vis 3,5) si le coût prime ; B4 en 3,81 si on veut le détrompage par forme |
| **Ultrasons** (3 × 4 fils) | **Garder JST-XH (B5)** et inscrire à `ACHATS.md` des **câbles XH ↔ Dupont femelle pré-sertis** (courants, ~1 €) : la pince à sertir sort de la liste | le HC-SR04 a un header mâle 2,54 : XH-Dupont est le câble naturel, détrompé côté carte, verrouillé | **empreinte double** XH + header 1×4 sur les mêmes nets (l'assembleur pose l'un ou l'autre) — coûte 10 mm de large par canal, à réserver si le sertissage reste un frein |
| **DHT / pluie** (3 × 3 fils) | Même choix que les ultrasons (XH + câbles pré-sertis) | modules à header ou capteur 3 fils : les deux se sertissent | B3 3 p si le DHT est un capteur nu à fils (fréquent) — à trancher avec le choix ultrasons pour ne garder qu'une famille |
| **Servos** | Garder les headers 1×3 (B6) | connecteur servo standard, verrouillé par friction, pas d'alternative sérieuse | — |
| **I2C / OLED / SD** | Garder les supports femelles | modules enfichés directement | un port I2C en **Qwiic/JST-SH** ou **Grove** en plus (J28) pour les capteurs du commerce (INA/BME en Qwiic) : 4 pads, à considérer |
| **Service / rails** | Garder les headers | Dupont, usage atelier | — |

Effet attendu si tout est retenu : **deux familles** au lieu de trois côté capteurs (JST-XH
pour ce qui a un header, push-in pour ce qui a des fils), plus de pince à sertir, borniers
5,08 réservés à ce qui porte du courant.

> **Décisions B** : B-charges (B1 / B4-5,08) · B-sondes (B3 / B2 / B4-3,81 / statu quo) ·
> B-ultrasons-DHT (XH + câbles pré-sertis / empreinte double / B3) · B-Qwiic (oui / non).

### 2.5 Impact générateur

- Chaque connecteur est une entrée `dict(ref=…, fp=…, nets=…)` de `build_components()` :
  changer de famille = changer `fp` (et `value`/`desc`), **une ligne par connecteur**.
- Empreintes : vendorer dans `generator/footprints/` depuis la lib officielle KiCad 8.0.9
  (`TerminalBlock_WAGO`, `TerminalBlock_Phoenix`, `Connector_JST` pour SH/Qwiic) — même
  provenance et licence que les empreintes actuelles (CC-BY-SA 4.0 + exception). Pour un
  DG141/KF141 (pas de lib officielle), empreinte à dessiner et à contrôler au pied à
  coulisse sur l'exemplaire réel.
- La bande basse est **re-routée** par `route_universal.py` (autorouteur pour la logique) ;
  la zone secteur n'est touchée que si B4-5,08 est retenu (tracé en dur à reprendre +
  contrôle 3 mm).
- `check_pcb_clearance.py` : les push-in et enfichables sont plus hauts (8-15 mm) et
  s'ouvrent par le haut ou le côté : ajouter leurs **couloirs d'insertion** (fil / fiche)
  à la table des corps 3D.
- `ACHATS.md` / `BOM.csv` / `exports/pcba/` : nouvelles références, câbles pré-sertis,
  suppression de la pince à sertir si retenu.

---

## 3. Autres suggestions (à trancher avec le reste)

| Id | Suggestion | Coût carte | Intérêt |
|---|---|---|---|
| **S1** | **Sérigraphier le rôle de chaque canal par firmware** près de la rangée relais : `K1 POMPE_AQUA (ffp5cs) / POMPE (n3pp)`, `K2 POMPE_RESERV`, `K3 CHAUFFAGE`, `K4 LUMIERE`, `K5 AUX1`, `K6 AUX2` | texte | l'assembleur et l'élève branchent la bonne charge sur le bon bornier sans ouvrir `pins.h` ; complète `COM = PHASE` (`SEC-COM-01`) |
| **S2** | **Shunts à languette** pour tous les JP (BOM) | 0 | manipulation sans pince, cavaliers moins perdus |
| **S3** | **Test points GND** (2-3 pads 1,5 mm ou picots) dans la zone logique | ~0 | mesure au multimètre / oscillo sans chercher un GND sur un bornier |
| **S4** | Marquage +/− et couleur sur **tous** les borniers basse tension, et code couleur des fils dans `ACHATS.md` (rouge 5 V, orange 3V3, noir GND, jaune signal) | texte + doc | `SEC-02` étendu, contexte élèves |
| **S5** | Sérigraphie d'alerte près de J7-J9 (« HC-SR04 : R17-R19 requises ») — déjà en check-list `NET-03` — **et** un cavalier « US 5V/3V3 » plutôt que des résistances à poser/déposer selon le rôle ? Non retenu : un cavalier de plus par canal pour un cas rare (unités msp) ; garder la consigne de pose | — | — |
| **S6** | Bouton poussoir « impulsion » par relais (test sans cavalier) | 6 boutons | **non retenu** : le cavalier en 2-3 fait le test, et un bouton dans une zone 230 V voisine invite à manipuler carte sous tension |
| **S7** | Résistance série 1 k + diode de clamp sur les entrées ADC A-E (fils longs vers l'extérieur : LDR, sondes sol) | 10 composants | protection ESD/surtension des GPIO d'entrée ; à évaluer avec le budget de surface |
| **S8** | Firmware : **journaliser au boot la configuration attendue des cavaliers** (« JP5-10 attendus en AUTO ») et, avec A2, l'état lu | 0 | diagnostic à distance : un relais « qui ne répond pas » est souvent un cavalier |

---

## 4. Récapitulatif des décisions à prendre

| # | Question | Options | Recommandation |
|---|---|---|---|
| A1 | Sélecteur AUTO / ON par canal | oui / non | **oui**, 6 canaux, JP5-JP10, livré en AUTO |
| A2 | Retour d'état vers le firmware | rien / empreinte PCF8574 DNP / header CMD SENSE | **empreinte PCF8574 DNP** (firmware à part, plus tard) |
| A3 | Chauffage K3 | ON possible / **broche 3 non câblée** / pas de header | **broche 3 non câblée** (AUTO / OFF seulement) |
| B-charges | Borniers de charge | vis 5,08 (statu quo) / enfichable 5,08 | **statu quo** (ne pas rouvrir la zone secteur) ; enfichable = option « confort » à chiffrer |
| B-sondes | DS18B20, LDR, ADC A-D, VBAT | statu quo 5,08 / vis 3,5 / **push-in 3,5** / enfichable 3,81 | **push-in 3,5 avec bouton** (Wago 2601 ou Phoenix PTSA) |
| B-ultrasons-DHT | HC-SR04, DHT, pluie | **XH + câbles pré-sertis** / empreinte double / push-in | **XH + câbles pré-sertis** (retirer la pince à sertir de `ACHATS.md`) |
| B-Qwiic | Port I2C normalisé en plus | oui / non | oui si des modules Qwiic/Grove sont prévus, sinon non |
| S1-S8 | Suggestions annexes | au cas par cas | S1, S2, S3, S4, S8 oui ; S7 à chiffrer ; S5, S6 non |

---

## 5. Plan d'exécution (une fois les décisions prises)

1. Branche `claude/…` depuis `master`, **un seul spin** regroupant les évolutions retenues
   ici **et** la check-list rev 0.2 de l'audit (mêmes fichiers, même re-routage).
2. `generate.py` : `relay_channel()` (§1.5), connecteurs (§2.5), `SCH_TEXTS`, `SILK`,
   `gen_bom()` ; empreintes vendorées ; option PCF8574.
3. `python3 generator/generate.py` → `route_universal.py` → `tidy_silkscreen.py` →
   `export_fab.py` ; `tools/check_pinmap_vs_firmware.py` ; `tools/check_pcb_clearance.py` ;
   `kicad-cli pcb drc --severity-error` ; contrôle 3 mm (`check_mains_gap`) ; DFM JLCPCB
   (`COMMANDE.md` §8).
4. Documents : `README.md` (cavaliers, connectique, tableau des différences rev 0.1 → 0.2),
   `BOM.csv`, `ACHATS.md`, `COMMANDE.md`, `exports/pcba/`, `../VERIFICATION.md`,
   `../TUTO_PCB.md` (§ canal relais : ajouter le sélecteur).
5. Firmwares : **aucun changement requis par A1** (le cavalier est transparent en AUTO).
   A2 = chantier firmware séparé (ffp5cs puis n3pp), avec bump de version et alignement
   serveur.
6. Rev 0.1 : reste commandable telle quelle ; l'interposeur (§1.8) couvre le besoin de
   forçage manuel d'ici là.
