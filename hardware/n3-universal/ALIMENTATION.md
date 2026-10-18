# n3-universal — sobriété et robustesse des quatre alimentations

Analyse du 2026-10-17, rev 0.2.1, en vue d'une **commande JLCPCB unique** : la carte doit être
juste du premier coup pour les quatre façons de l'alimenter. Chiffres issus de la simulation
SPICE (`simulation/SIMULATION.md`), du schéma (`generator/generate.py`) et des firmwares
(`msp`, `n3pp`, `ffp5cs`). Les hypothèses sont marquées « ~ » : ce sont les grandeurs à
mesurer sur la première carte montée (banc INA226 du firmware `energie/`).

| Unité | Source | Veille | Ce qui compte |
|---|---|---|---|
| **msp1** (météo + tracker) | 18650 Li-ion 1S, chargeur solaire (TP4056), boost 5 V sur J1/J2 | deep sleep 300 s, réveils ~15 s | **la fuite en sommeil** |
| **n3pp** (serre) | chargeur 5 V de bonne qualité sur J1/J2 | deep sleep 300 s | marge de tension (chaîne 5 V → DevKit) |
| **ffp5cs** (aquaponie) | secteur 230 V, Hi-Link 20M05 embarqué | jamais (light sleep + modem sleep) | sécurité secteur, relais 24/7 |
| **ffp5cs 12 V** | batterie AGM 12 V, régulateur PWM solaire, buck 5 V sur J36/J37 → J1 | jamais | **bobines de relais**, délestage, transitoires du bus |

## 1. Le principe : fuite et robinet

L'énergie d'une journée = (ce que la carte tire **en permanence**) × 24 h + (ce qu'elle tire
**quand elle travaille**) × le temps de travail. Pour une unité qui dort 99 % du temps (msp1,
n3pp), la **fuite** domine toujours : 10 mA permanents sous 5 V font 1,2 Wh par jour, autant
que 288 réveils Wi-Fi. Pour une unité qui ne dort jamais (ffp5cs), ce sont les consommateurs
continus — bobines de relais, pompe — qui dominent, et la fuite électronique est un détail.

Robustesse : une unité est robuste quand (1) la tension du cerveau (3,3 V du DevKit) ne passe
jamais sous ~3,1 V, quelle que soit la charge ; (2) une erreur de câblage ou un transitoire ne
détruit rien ; (3) une batterie n'est jamais vidée au-delà de ce qu'elle tolère.

## 2. Ce qui reste sous tension pendant le sommeil (rev 0.2.1)

Sur le rail **+5V**, permanent dès que la carte est alimentée :

| Poste | Courant ~ | Wh/jour | Remarque |
|---|---|---|---|
| DevKit V1 (AMS1117 ~5 mA + puce USB + LED rouge), ESP32 en deep sleep | **~10 mA** | 1,2 | mesuré 9–15 mA selon les clones |
| J15/J16 pin 2 = +5V permanent : **deux SG90 sans signal** | ~5 mA chacun | 1,2 | les servos n'ont rien à faire sous tension pendant le sommeil |
| LED5 (verte, présence 5 V, R13 1k) | ~3 mA | 0,4 | utile sur l'établi, inutile sur une unité autonome |
| R35 100k (grille Q7), pont VBAT 100k/100k, D11, D12 | < 0,1 mA | ~0 | négligeable |
| HC-SR04 ×3 (J7-J9 pin 1 = +5V permanent) | ~2 mA chacun | 0,7 | **ffp5cs seulement** ; msp/n3pp ne les posent pas |
| Module boost 3,7 → 5 V (msp1) : consommation à vide + rendement à 10 mA | ~1–2 mA | 0,3 | varie beaucoup selon le module (MT3608 : médiocre à vide) |
| **Rail capteurs +3V3_SW** (OLED ~15 mA, DS18B20, DHT, pluie, INA, RTC, SD) | **0 en sommeil** | 0 | grâce à la correction de Q7 ; en rev 0.2 il restait à 3,3 V (≈ 2,5 Wh/jour) |

**Fuite msp1 telle quelle : ~23 mA, ~3,2 Wh/jour.** En supprimant les servos sous tension et LED5 :
**~11 mA, ~1,4 Wh/jour**. Le DevKit V1 devient alors le seul gros poste : son AMS1117 consomme
5 mA à ne rien faire ; on ne l'évite qu'en alimentant le DevKit par sa broche 3V3 depuis un
régulateur à très faible consommation (HT7833 : 4 µA ; ME6211 : 40 µA), ce que la carte 0.2.1
ne prévoit pas (voir §7, arbitrage N2).

## 3. msp1 — 18650 + solaire

**Chaîne** : panneau 6 V → TP4056 → 18650 (3,0–4,2 V) → boost 5 V (module, réglé **5,1 V**) →
J1/J2 → Q12 → +5V → D5 → DevKit (AMS1117 → 3,3 V) ; +5V → Q7 → U1 → +3V3_SW (capteurs,
coupé en sommeil par GPIO13, JP1 ôté) ; JP12 en 2-3 (pont 100k/100k, 4,2 V → 2,10 V).

**Budget** (deep sleep 300 s, réveil ~15 s à ~120 mA moyens, rendement boost ~85 %) :

| | Telle quelle | Sans servos sous tension ni LED5 |
|---|---|---|
| Fuite | 3,2 Wh/j | 1,4 Wh/j |
| Réveils (288/j) | 0,85 Wh/j | 0,85 Wh/j |
| **Total côté batterie** | **~4,0 Wh/j (~1,1 Ah)** | **~2,3 Wh/j (~0,6 Ah)** |
| Autonomie, 18650 2 800 mAh à 80 % | ~2 jours | ~3,5 jours |
| Panneau 2 W (hiver Casablanca ~3,5 h de plein soleil, TP4056 ~70 %, orientation ~80 %) ≈ 3,8 Wh/j | **déficitaire** | excédentaire ×1,6 |
| Panneau 5 W ≈ 9,6 Wh/j en hiver, ~18 Wh/j en été | excédentaire ×2,4 | ×4 |

**Recommandations** : panneau **5–6 W / 6 V** ; LED5 **non posée** ; servos coupés en sommeil
(rail `+5V_SERVO` commuté, arbitrage N1) ; boost réglé 5,1 V, module à faible consommation à
vide (les TPS61023/TPS61088 ont une broche EN ; un MT3608 convient si on accepte ~1,5 mA) ;
réveil mesuré (10 à 30 s selon le temps de connexion HTTPS — c'est le poste le plus incertain :
chaque seconde de réveil en moins vaut ~0,06 Wh/jour).

**Robustesse** :
- Le TP4056 n'est pas un chargeur solaire : pas de MPPT (il tire le panneau à ~4,3 V), fin de
  charge faussée par la charge permanente, charge jusqu'à 4,2 V (vieillissement accéléré à
  chaud). Un **CN3791** (MPPT 1S, même format de module) ou un gestionnaire solaire 1S à sortie
  5 V est préférable ; le TP4056 reste acceptable avec un panneau surdimensionné.
- La protection DW01 des modules coupe à **2,5 V**, trop bas pour la cellule : le vrai seuil est
  celui du firmware (`SeuilPontDiv`, veille d'urgence) — à régler pour **≥ 3,3 V** sous charge
  (pont 100k/100k : ADC ≥ 1,65 V). La carte le mesure en permanence (18 µA).
- **Chaleur** : au-dessus de 45 °C on ne charge plus une Li-ion et elle vieillit vite ; cellule
  à l'ombre (sous le panneau), jamais dans un boîtier plein soleil.
- Brownout : la chaîne 5 V → DevKit (Q12 + D5 + AMS1117) ne laisse que ~0,2 V de marge ; un
  boost réglé à 5,1–5,2 V règle la question (`SIMULATION.md` §6).
- Tracker : les SG90 reprennent leur position au réveil (le firmware réécrit l'angle), un
  panneau de 5 W tient par friction pendant le sommeil.

## 4. n3pp — chargeur 5 V

**Chaîne** : chargeur USB-A ou bloc 5 V → **câble vers J1/J2** (jack 5,5/2,1 ou bornier) → Q12
→ +5V → relais (pompe, lumière, chauffage…), D5 → DevKit ; +3V3_SW commuté (JP1 ôté). Deep
sleep 300 s comme msp.

**Points durs** :
- **Ne pas alimenter par le micro-USB du DevKit** : D5 bloque le 5 V USB vers le rail de la
  carte, les relais et les capteurs restent sans alimentation. L'entrée, c'est J1/J2.
- Un chargeur de téléphone « 5 V 2 A » **de qualité** fournit 5,0–5,2 V ; le câble compte autant
  que lui : 1 m de câble USB fin fait 0,3–0,5 Ω et perd 0,3 V sous 1 A. Prendre un cordon
  USB-A → jack 5,5/2,1 court et épais (≥ 0,5 mm²) ou un bloc à cordon fixe. Seuil : **≥ 4,8 V
  sur J1/J2 sous charge** ; en dessous, redémarrages pendant le Wi-Fi (`SIMULATION.md` §6).
- Relais : 72 mA par bobine collée ; une pompe maintenue = 0,36 W = 8,6 Wh/jour, sans effet sur
  un chargeur, à connaître pour le dimensionner (6 relais + Wi-Fi ≈ 1 A crête : un chargeur 2 A
  suffit, 1 A non).
- Charges DC inductives sur les contacts (pompe, électrovanne 12/24 V) : **diode de roue libre
  aux bornes de la charge** (extérieure à la carte), sinon arc et usure des contacts. Charges
  230 V : contacts 7 A résistifs / 3 A inductifs réels (SRD Form C).
- Sobriété : même fuite que msp1 (DevKit + LED5 + servos s'il y en a) — sans importance sur
  chargeur, mais LED5 peut rester non posée par cohérence.
- **Mesure « batterie » sans batterie** : le firmware n3pp lit `pontdiv` (ADC_VBAT) et part en
  veille d'urgence sous `SeuilPontDiv`. Sur chargeur, J25 ne reçoit rien → lecture ≈ 0 → veille
  d'urgence. Relier **J25-1 au +5V (J18-1)** par un fil (5 V → 2,5 V sur l'ADC avec JP12 en 2-3),
  ou désactiver la veille infinie côté serveur (clé 112). À vérifier au premier allumage.

## 5. ffp5cs — secteur 230 V (Hi-Link)

**Chaîne** : 230 V → F1 T1A + RV1 → PS1 HLK-20M05 (5 V / 3,6 A) → +5V directement (pas de Q12
sur ce chemin) ; JP1 fermé (rail capteurs permanent) ; relais, HC-SR04, servos (nourrisseurs),
SD, OLED, INA… jamais de sommeil profond (light sleep + modem sleep).

- **Sécurité** : fusible et varistance embarqués, zone secteur isolée (fentes, 3 mm, ligne de
  fuite 6,4 mm vérifiée par DRC), 2 oz, H1/H5 en vis nylon. Boîtier + presse-étoupes
  obligatoires ; pas de terre (classe II) — le boîtier doit l'être aussi.
- **Budget** : Hi-Link 20 W pour ~0,5 A continu (ESP 0,2 + capteurs 0,15 + 2 relais 0,15) et
  des pointes à ~1,5 A (6 relais + Wi-Fi + HC-SR04 + servos) : marge ×2. Les relais collés en
  permanence (pompe) consomment 0,36 W chacun, 24 h/24 — c'est le secteur qui paie.
- **Robustesse** : D12 (TVS 6,8 V) protège le rail des surtensions du module ; le Hi-Link
  n'aime pas les micro-coupures répétées (redémarrages) — un condensateur C1 1000 µF tient
  ~50 ms. Si le réseau est mauvais, un onduleur en amont, pas une modification de carte.
- Sobriété : non critique. Le seul levier serait des relais bistables (0 W maintenus) — hors
  périmètre de cette carte.

## 6. ffp5cs 12 V — batterie AGM, régulateur PWM, buck

**Chaîne** : panneau → régulateur PWM → **batterie AGM** (10,5–15,5 V) → fusible lame → J26 →
Q11 (anti-inversion) + D9 + D8 (TVS 18 V) → J36/J37 → **buck 5 V** (MP1584 conseillé, réglé
5,1 V) → J1 → Q12 → +5V. Pompes 12 V commutées par les **contacts** des relais, directement sur
le bus. INA226 ×3, JP12 en 1-2 (100k/18k depuis la 0.2.2).

- **Où brancher la carte sur le régulateur PWM** : sur les bornes **batterie** (via le fusible),
  pas sur la sortie « charge » du régulateur : sur les modèles de qualité moyenne, cette sortie
  est parfois coupée la nuit (fonction éclairage), hachée ou protégée par un LVD à 11 V réglé
  pour des lampes, pas pour une pompe vitale. Le délestage, c'est le firmware qui le fait
  (`SIMULATION.md` §8, seuils AGM), en gardant la pompe aquarium pour la fin.
- **Donnée de terrain (Oliv, 2026-10-17)** : **14,5 V mesurés en sortie du régulateur PWM** —
  c'est l'absorption normale d'une AGM. À cette tension D8 reste muette (conduction à partir
  de 17,1 V, fuite < 5 µA à 15,3 V — d'où le rejet de la 1.5KE15A), Q11 voit VGS = −14,5 V
  (limite ±20 V), D9 (15 V ± 5 %) peut laisser passer 2–3 µA par R40 : sans effet ; C5 25 V,
  buck MP1584 28 V. Seule la **mesure** est gênée : 14,5 V → 2,61 V avec 22k (zone comprimée),
  2,19 V avec 18k (zone linéaire) — ce qui a motivé R39 = 18k en 0.2.2.
- **Transitoires** : un PWM hache le courant de charge → ondulation sur le bus ; D8 (1.5KE18A),
  C5 470 µF et le buck absorbent. Absorption AGM 14,4–14,7 V, égalisation parfois 15+ V sur
  certains PWM : D8 ne conduit qu'à partir de 17 V, C5 est en 25 V, Q11 borné à −15 V de VGS
  par D9 : tout tient (`SIMULATION.md` §7, surtension 30 V/2 ms écrêtée à 21 V).
- **Mesure batterie** : avec 100k/22k, 14,4–14,7 V tombe au-dessus de la zone linéaire de l'ADC
  (> 2,45 V). Si les seuils de délestage s'appuient sur cette mesure (c'est le cas), R39 = **18k**
  (14,7 V → 2,24 V, pleine échelle 20,3 V) et ratio ajusté dans le firmware (arbitrage A3).
- **Sobriété — le vrai poste, les relais** : chaque relais collé = 72 mA sous 5 V = 0,36 W, soit
  ~0,43 W pris sur la batterie à travers le buck ; **une pompe maintenue 24 h = 10 Wh/jour**,
  sur une AGM de 144 Wh dont on ne veut utiliser que 30–50 %. Le plancher électronique (buck
  ~0,1 mA + DevKit 10 mA + capteurs permanents ~30 mA + INA) ≈ 0,25 W ≈ 6 Wh/jour. Le levier
  n'est pas sur la carte : **cycle de fonctionnement des pompes** (le firmware fait déjà du
  discontinu) et interdiction du chauffage sur batterie (README).
- **Robustesse** : fusible lame 7,5–10 A externe obligatoire ; bobines SRD-05 depuis le buck
  (le README évoque des SRD-12VDC sur le bus : même puissance, pas de gain) ; C5 25 V.

## 7. Conséquences pour la commande unique

Ce qui est **déjà juste** en 0.2.1 : Q7 corrigé (rail capteurs réellement coupé), protections
(Q11/Q12/D8/D12), étage relais, ponts de mesure, zone secteur. Ce que l'analyse fait remonter
**avant** de commander, parce que ça touche le PCB ou la BOM :

| # | Sujet | Niveau | Gain | Coût |
|---|---|---|---|---|
| N1 | **Rail `+5V_SERVO` commuté** : J15/J16 pin 2 alimentés par un second P-FET (Q13, grille sur GATE_G comme Q7) + cavalier de bypass (JP comme JP1) ; sur msp les servos s'éteignent avec les capteurs | PCB (2 composants + 1 cavalier, zone logique) | −1,2 Wh/j sur msp1 ; câblage en étoile possible | reroutage local, revalidation DRC |
| N2 | Alimenter le DevKit par sa broche 3V3 depuis un régulateur à très faible consommation (HT7833 TO-92 500 mA + 2 condensateurs), profil 1S | PCB (section alim) | −1 Wh/j sur msp1 (AMS1117 contourné) ; mais pointes Wi-Fi 450 mA sur un LDO 500 mA, et le rail +5V reste nécessaire (servos, U1) | redessin partiel de la section alim ; un seul profil en profite |
| N3 | LED5 non posée sur msp/n3pp | BOM | −0,4 Wh/j | 0 |
| N4 | R39 = 18k (profil 12 V) | BOM + firmware | précision du délestage | ratio firmware |
| N5 | Chargeur solaire 1S : CN3791 (MPPT) plutôt que TP4056 ; boost à broche EN ou MT3608 réglé 5,1 V | hors carte | charge +20–30 %, fin de charge correcte | import |
| N6 | 10 cartes plutôt que 5 | commande | une carte d'avance par profil + 2 de rechange | +~10 € |
| N7 | Diodes de roue libre sur les charges DC inductives, fusible lame 12 V, câble 5 V court et épais | câblage | contacts et marges | 0 |

## 8. Décisions (Oliv, 2026-10-17) → rev 0.2.2

| # | Décision | Mise en œuvre |
|---|---|---|
| N1 | **Oui** : servos coupés en sommeil | **J15/J16 pin 2 et C2 passent sur `LDO_IN`**, le 5 V déjà commuté par Q7 : les servos suivent le rail capteurs (JP1 ôté = coupés en deep sleep, JP1 fermé = permanents sur ffp5cs). Aucun composant ajouté : une première version avec un second P-FET (Q13) + cavalier bouchait le couloir de pistes du bas de carte (7 nets non routés). SPICE : un SG90 bloqué (0,65 A) à travers Q7 fait tomber LDO_IN à 4,96 V (4,91 V avec un Q7 défavorable), 4,60 V pour un MG996R : +3V3_SW reste à 3,30 V dans tous les cas (U1 n'a besoin que de 4,3 V). Carte reroutée (générateur → freerouting → contrôles). Câblage en étoile / gros servo : fil +5V du servo directement sur la source, J15/J16 pin 2 vide |
| N2 | **Non** (LDO faible Iq pour le DevKit) | essai possible plus tard par module HT7833 externe sur J17-1, sans toucher à la carte |
| N3 | **LED5 non posée** sur msp/n3pp | BOM (étoilée), posée sur ffp5cs |
| A1 | **D5 court-circuitée** (pont de fil) — il n'y aura jamais USB + alimentation en même temps | +0,35 V de marge au DevKit ; **règle absolue : ne jamais brancher l'USB du DevKit carte sous tension** (flasher hors tension, ou OTA) ; l'empreinte DO-201 reste pour reposer une 1N5822 si ce besoin revient |
| A3 | **R39 = 18k** | BOM ; aucun firmware ne lit ADC_VBAT sur le profil 12 V aujourd'hui (la batterie est mesurée par INA226 / `n3_power`) — si un jour c'est le cas, ratio 118/18 |
| N5 | **TP4056 conservé** (déjà en stock) | seuil de veille d'urgence firmware ≥ 3,3 V ; cellule à l'ombre ; panneau 5–6 W |
| N4 | **Relais conservés** | cycles de pompe et interdiction du chauffage sur batterie côté firmware |
| N6 | quantité | recommandation : 10 cartes |
