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
| **ffp5cs** (aquaponie) | installation 230 V : **bloc 5 V externe ≥ 2 A** sur J2/J1 et modules relais (rev 0.3 ; Hi-Link embarqué jusqu'à la 0.2.3) | pas de veille profonde : light sleep de 33 min le jour, 100 min la nuit, relais coupés pendant la veille (§9.5) | sécurité secteur, ce qui reste alimenté en veille |
| **ffp5cs 12 V** | batterie AGM 12 V, régulateur PWM solaire, buck 5 V (réglé 5,2 V en rev 0.3) sur J36/J37 → J1 | jamais | **bobines de relais** (modules externes en rev 0.3), délestage, transitoires du bus |

## 1. Le principe : fuite et robinet

L'énergie d'une journée = (ce que la carte tire **en permanence**) × 24 h + (ce qu'elle tire
**quand elle travaille**) × le temps de travail. Pour une unité qui dort 99 % du temps (msp1,
n3pp), la **fuite** domine toujours : 10 mA permanents sous 5 V font 1,2 Wh par jour, autant
que 288 réveils Wi-Fi. Pour une unité qui ne dort jamais (ffp5cs), ce sont les consommateurs
continus — bobines de relais, pompe — qui dominent, et la fuite électronique est un détail.
(Correction du 2026-11-18 : ffp5cs dort en veille légère 88 % du temps, relais coupés ; ce sont
les capteurs et servos restés alimentés qui dominent, voir §9.5.)

Robustesse : une unité est robuste quand (1) la tension du cerveau (3,3 V du DevKit) ne passe
jamais sous ~3,1 V, quelle que soit la charge ; (2) une erreur de câblage ou un transitoire ne
détruit rien ; (3) une batterie n'est jamais vidée au-delà de ce qu'elle tolère.

## 2. Ce qui reste sous tension pendant le sommeil (rev 0.2.1)

Sur le rail **+5V**, permanent dès que la carte est alimentée :

| Poste | Courant ~ | Wh/jour | Remarque |
|---|---|---|---|
| DevKit (AMS1117 ~5 mA + puce USB + LED rouge), ESP32 en deep sleep | **~10 mA** | 1,2 | mesuré 9–15 mA selon les clones (DevKit V1 30 broches ; même architecture sur les DevKitC 38 broches du site A1 depuis la 0.2.3 — à remesurer) |
| J15/J16 pin 2 = +5V permanent : **deux SG90 sans signal** | ~5 mA chacun | 1,2 | les servos n'ont rien à faire sous tension pendant le sommeil |
| LED5 (verte, présence 5 V, R13 1k) | ~3 mA | 0,4 | utile sur l'établi, inutile sur une unité autonome |
| R35 100k (grille Q7), pont VBAT 100k/100k, D11, D12 | < 0,1 mA | ~0 | négligeable |
| HC-SR04 ×3 (J7-J9 pin 1 = +5V permanent) | ~2 mA chacun | 0,7 | **ffp5cs seulement** ; msp/n3pp ne les posent pas |
| Module boost 3,7 → 5 V (msp1) : consommation à vide + rendement à 10 mA | ~1–2 mA | 0,3 | varie beaucoup selon le module (MT3608 : médiocre à vide) |
| **Rail capteurs +3V3_SW** (OLED ~15 mA, DS18B20, DHT, pluie, INA, RTC, SD) | **0 en sommeil** | 0 | grâce à la correction de Q7 ; en rev 0.2 il restait à 3,3 V (≈ 2,5 Wh/jour) |

**Fuite msp1 telle quelle : ~23 mA, ~3,2 Wh/jour.** En supprimant les servos sous tension et LED5 :
**~11 mA, ~1,4 Wh/jour**. Le DevKit devient alors le seul gros poste : son AMS1117 consomme
5 mA à ne rien faire ; on ne l'évite qu'en alimentant le DevKit par sa broche 3V3 depuis un
régulateur à très faible consommation (HT7833 : 4 µA ; ME6211 : 40 µA), ce que la carte 0.2.1
ne prévoit pas (voir §7, arbitrage N2).

## 3. msp1 — 18650 + solaire

**Chaîne** : panneau 6 V → TP4056 → 18650 (3,0–4,2 V) → boost 5 V (module, réglé **5,1 V** ; **5,2 V en rev 0.3**, D5 revenue — §10) →
J1/J2 → Q12 → +5V → D5 → DevKit (AMS1117 → 3,3 V) ; +5V → Q7 → U1 → +3V3_SW (capteurs,
coupé en sommeil par GPIO13, JP1 ôté) ; JP12 en 2-3 (pont 100k/100k, 4,2 V → 2,10 V).

**Budget** (deep sleep 300 s, réveil ~15 s à ~120 mA moyens, rendement boost ~85 %) :

> ⚠️ **2026-11-18** : l'hypothèse « réveil ~15 s » est optimiste. Le journal série du 09/03/2026
> montre des réveils de 32 à 119 s (médiane 96 s). Budget recalculé au §9.3.

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

*Section mise à jour pour la rev 0.3 (2026-11-22). Chiffres [simulé] : `simulation/SIMULATION.md` §13.*

**Chaîne** : chargeur USB-A ou bloc 5 V → **câble vers J2** (jack 5,5/2,1, + au centre) ou J1
(bornier) → Q12 → +5V → bornier J39 (5 V du module relais de la pompe), **D5 ou JP22** → DevKit ;
+3V3_SW commuté (JP1 ôté). Deep sleep 300 s comme msp.

**Points durs** :
- **Cavalier JP22 posé sur cette unité.** Un chargeur de téléphone sort 5,0 V : avec la diode D5
  en série, la marge du régulateur du DevKit est de −0,15 V au pire cas et −0,01 V en typique
  [simulé] ; cavalier posé, +0,32 V. Contrepartie : **jamais l'USB du DevKit en même temps que le
  chargeur** (flasher chargeur débranché, ou en OTA). Avec une source réglable à 5,2 V, JP22
  s'ôte et l'USB redevient permis (+0,07 V au pire, +0,20 V en typique [simulé]).
- **Ne pas alimenter l'unité par le micro-USB du DevKit** : c'est le mode « USB seul » du §10,
  réservé à l'établi (4,4 V sur le rail pendant le Wi-Fi [simulé]).
- Un chargeur « 5 V 2 A » **de qualité** fournit 5,0–5,2 V ; le câble compte autant que lui :
  1 m de câble USB fin fait 0,3–0,5 Ω et perd 0,3 V sous 1 A. Prendre un cordon USB-A → jack
  5,5/2,1 court et épais (≥ 0,5 mm²) ou un bloc à cordon fixe. Seuil : **≥ 4,8 V sur J1/J2 sous
  charge** avec JP22 posé (le DevKit tient jusqu'à ~4,6 V de source en typique [simulé] ; garder
  0,2 V de réserve) ; **≥ 5,05 V** de source avec JP22 ôté [simulé, pire cas].
- Module relais : 75 mA collé [estimé] ; une pompe maintenue = 0,38 W = 9 Wh/jour, sans effet sur
  un chargeur.
- Charges DC inductives sur les contacts du module (pompe, électrovanne 12/24 V) : **diode de
  roue libre aux bornes de la charge**. Charges 230 V : uniquement sur le module, dans un
  coffret isolant (`MODULES_RELAIS.md` §6).
- Sobriété : même fuite que msp1 (DevKit + LED5) — sans importance sur chargeur, mais LED5 peut
  rester non posée par cohérence.
- **Mesure « batterie » sans batterie** : le firmware n3pp lit `pontdiv` (ADC_VBAT) et part en
  veille d'urgence sous `SeuilPontDiv`. Sur chargeur, J25 ne reçoit rien → lecture ≈ 0 → veille
  d'urgence. Relier **J25-1 au +5V (J18-1)** par un fil (5 V → 2,5 V sur l'ADC avec JP12 en 2-3),
  ou désactiver la veille infinie côté serveur (clé 112). À vérifier au premier allumage.

## 5. ffp5cs — installation 230 V : bloc 5 V externe et modules relais (rev 0.3)

*Jusqu'à la rev 0.2.3 : Hi-Link HLK-20M05, fusible et varistance sur la carte (→ remplacé en
rev 0.3 ; le zip 0.2.3 reste dans `exports/`).*

**Chaîne** : bloc 5 V **≥ 2 A, mieux 3 A** → J2 (jack) ou J1 (bornier) → Q12 → +5V → borniers
J39-J44 (5 V des modules relais), D5 → DevKit ; JP1 fermé (rail capteurs permanent) ; HC-SR04,
servos (nourrisseurs), SD, OLED, INA… jamais de sommeil profond (light sleep + modem sleep).

- **Sécurité** : plus aucun 230 V sur la carte. Il ne passe que par les contacts des modules
  relais, dans un coffret isolant fermé, avec **protection en amont** (fusible ou disjoncteur,
  différentiel 30 mA) : la carte ne protège aucune charge (`MODULES_RELAIS.md` §6).
- **Tension du bloc** : c'est le profil le plus chargé. Avec 6 modules alimentés par la carte, la
  marge du régulateur du DevKit est de −0,21 V au pire cas à 5,0 V et **+0,02 V à 5,2 V**
  [simulé]. Règle : **bloc donnant 5,2 V en charge, JP22 ôté**. Si le bloc ne dépasse pas
  5,0 V : **JP22 posé** (+0,29 V), et alors jamais d'USB en même temps que le bloc.
- **Servo qui force** (nourrisseur bloqué, 0,65 A) : 3V3 du DevKit à 2,85 V avec un bloc à 5,0 V
  et JP22 ôté ; 3,12 V à 5,2 V ; 3,26 V JP22 posé [simulé]. Câble court et épais (≤ 0,15 Ω).
- **Budget** : ~0,5 A continu (ESP 0,2 + capteurs 0,15 + 2 modules 0,15) et des pointes à
  ~1,5 A (6 modules + Wi-Fi + HC-SR04 + servos) [estimé] : un bloc 2 A est le minimum, 3 A
  donne de la marge.
- **Robustesse** : D12 (TVS 6,8 V) écrête les surtensions brèves du bloc ; **elle ne protège pas
  d'un bloc 12 V branché par erreur** sur le jack (sérigraphie « PAS DE 12V »). C1 1000 µF tient
  ~50 ms de micro-coupure.
- Variante : alimenter les modules directement depuis le bloc (seuls S et GND vont à la carte) :
  +0,10 à +0,15 V de marge [simulé].

## 6. ffp5cs 12 V — batterie AGM, régulateur PWM, buck

**Chaîne** : panneau → régulateur PWM → **batterie AGM** (10,5–15,5 V) → fusible lame → J26 →
Q11 (anti-inversion) + D9 + D8 (TVS 18 V) → J36/J37 → **buck 5 V** (MP1584 conseillé, réglé
5,1 V ; **5,2 V en rev 0.3**) → J1 → Q12 → +5V. Pompes 12 V commutées par les **contacts** des relais (modules externes en rev 0.3), directement sur
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
- ⚠️ **2026-11-18** : une pompe n'est pas « maintenue 24 h » : le firmware coupe les relais
  pendant le light sleep, K1 n'est collé que ~12 % du temps. Budget recalculé au §9.5-9.6.
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
| A1 | **D5 court-circuitée** (pont de fil) — il n'y aura jamais USB + alimentation en même temps (**→ remplacé en rev 0.3** : D5 = 1N5822 partout + cavalier JP22, §10) | +0,35 V de marge au DevKit ; **règle absolue : ne jamais brancher l'USB du DevKit carte sous tension** (flasher hors tension, ou OTA) ; l'empreinte DO-201 reste pour reposer une 1N5822 si ce besoin revient |
| A3 | **R39 = 18k** | BOM ; aucun firmware ne lit ADC_VBAT sur le profil 12 V aujourd'hui (la batterie est mesurée par INA226 / `n3_power`) — si un jour c'est le cas, ratio 118/18 |
| N5 | **TP4056 conservé** (déjà en stock) | seuil de veille d'urgence firmware ≥ 3,3 V ; cellule à l'ombre ; panneau 5–6 W |
| N4 | **Relais conservés** | cycles de pompe et interdiction du chauffage sur batterie côté firmware |
| N6 | quantité | recommandation : 10 cartes |

## 9. Bilan énergétique détaillé (2026-11-18, rev 0.2.3)

### 9.1 Ce qui est mesuré, ce qui est estimé

| Donnée | Source | Valeur |
|---|---|---|
| Durée d'un réveil msp | journal série `msp/monitor_3min_2026-03-09_*.log`, 39 réveils | 9 réveils de 32-44 s, 30 réveils de 94-119 s ; **médiane 96 s**, moyenne 86 s (firmware de mars, rafales d'erreurs I2C et OTA en échec : à remesurer en 2.79) |
| Rythme de ffp5cs | iot.olution.info, ffp3 prod, 15 → 18/11/2026, et `automatism_sleep.cpp` | éveil d'environ 7 min (mesures toutes les 30 s), puis light sleep de 2000 s le jour, 6000 s la nuit (×3) : **25 éveils par 24 h, soit ~12 % du temps éveillé**. Avant chaque veille le firmware **coupe tous les actionneurs** (`prepareActuatorsForSleep`) et les rétablit au réveil |
| Collage des relais ffp5cs | idem | K1 (pompe aquarium) collé pendant les éveils, soit ~12 % du temps ; chauffage et UV 0 % sur 30 jours. ⚠️ La moyenne brute de `etatPompeAqua` (94 %) ne mesure pas ce taux : la carte n'envoie de mesures que lorsqu'elle est éveillée |
| msp1 en veille 3000 s (ancienne carte) | iot.olution.info, 15/02 → 05/03/2026 | le plus souvent 21 à 27 envois par jour sur 28,8 possibles, 19 jours consécutifs avec des données |
| msp1 sans veille, `WakeUp` = 1 (ancienne carte) | iot.olution.info, à partir du 13/03/2026 | envois irréguliers, trous quotidiens, dernier contact le 26/05/2026 |
| n3pp (ancienne carte) | iot.olution.info, 23/06 → 23/07/2026, 325 341 mesures | `WakeUp` = 1 sur 99,5 % des mesures, un envoi toutes les 8 s : **jamais en veille profonde** |
| Banc énergie INA226 | iot.olution.info | **aucune mesure enregistrée** |

Faute de banc, **tous les courants ci-dessous sont estimés** (fiches techniques, valeurs usuelles
des DevKit), notés « ~ ». Les durées et les taux de collage, eux, sont mesurés.

### 9.2 Postes de consommation, sous 5 V

| Poste | Courant ~ | Puissance | Quand |
|---|---|---|---|
| DevKit, ESP32 en veille profonde (AMS1117, puce USB, LED) | 10 mA (5-15) | 50 mW | permanent |
| DevKit, ESP32 éveillé, Wi-Fi actif | 125 mA (100-160) | 0,63 W | réveils msp/n3pp |
| DevKit, ESP32 éveillé, Wi-Fi en modem-sleep | ~100 mA (75-125) | 0,50 W | ffp5cs, pendant les éveils |
| DevKit, ESP32 en light sleep, Wi-Fi coupé | ~12 mA (5-15) | 60 mW | ffp5cs, entre deux éveils |
| Rail capteurs msp (LDO 5, OLED 12, sondes, LDR) | 22 mA | 0,11 W | éveillé |
| Rail capteurs n3pp (LDO 5, OLED 12, 4 sondes de sol, DHT) | 40 mA | 0,20 W | éveillé |
| Rail capteurs ffp5cs (LDO 5, OLED 12, sondes, RTC, INA) | 22 mA | 0,11 W | permanent |
| Deux SG90 au repos | 12 mA | 60 mW | rail allumé |
| HC-SR04 ×3 au repos | 6 mA | 30 mW | permanent (ffp5cs) |
| LED5 | 3,2 mA | 16 mW | permanent si posée |
| **Un relais collé** (bobine 71 mA, LED 3 mA, base 3 mA) | **77 mA** | **0,39 W**, soit 9,3 Wh par jour | pendant le collage |
| D12 (TVS 6,8 V), fuite | 0,05 à 1 mA | 0,3 à 5 mW | permanent, **à mesurer** : la fiche garantit seulement ≤ 1 mA à 5,8 V |
| Pont VBAT 100k/100k, R35 | 0,02 à 0,05 mA | négligeable | permanent |

### 9.3 msp1 — 18650, boost 5 V

Énergie par jour **côté batterie** (rendement du boost : 75 % en veille, 85 % éveillé ; 1 mA de
consommation propre). Entre parenthèses, la part de la capacité utile d'une 18650 de 2 800 mAh
(8,1 Wh à 80 %).

| Veille entre deux réveils | Réveil de 15 s (hypothèse du §3) | Réveil de 40 s (mesuré, bon cas) | Réveil de 100 s (mesuré, cas courant) |
|---|---|---|---|
| 300 s (défaut firmware) | 2,7 Wh (34 %) | 4,2 Wh (52 %) | **7,0 Wh (87 %)** |
| 1200 s | 2,0 Wh (25 %) | 2,4 Wh (30 %) | 3,3 Wh (41 %) |
| 3000 s (réglage de février 2026) | 1,8 Wh (23 %) | 2,0 Wh (25 %) | 2,4 Wh (30 %) |
| jamais de veille (`WakeUp` = 1) | **22,8 Wh par jour** : la cellule est vide en 8 h | | |

- **Plancher : 1,7 Wh par jour**, même sans aucun réveil. C'est le DevKit endormi (10 mA),
  vu à travers le boost. Une fois la durée de réveil maîtrisée, c'est lui qui domine.
- **Au réglage par défaut (300 s) avec les réveils mesurés, la station consomme 7 Wh par jour.**
  Un panneau de 5 W en fournit ~9,8 en hiver à Casablanca (3,5 h de plein soleil, TP4056 70 %,
  orientation 80 %) : marge de 1,4, sans réserve pour deux jours couverts. Avec un panneau de
  2 W (~3,9 Wh), c'est déficitaire.
- Les données de terrain vont dans le même sens : 19 jours consécutifs à 3000 s de veille, des
  trous quotidiens dès que la veille est désactivée. (Ancienne carte, panneau non documenté :
  cohérent avec le calcul, pas une preuve.)
- **Leviers, par ordre de gain** :
  1. **Durée du réveil** (firmware) : passer de ~100 s à 40 s économise 2,8 Wh par jour à 300 s
     de veille ; chaque seconde gagnée vaut ~0,05 Wh par jour. Pistes : comprendre pourquoi 30
     réveils sur 39 durent ~96 s (attentes I2C, tentative OTA à chaque réveil), ne vérifier
     l'OTA qu'une fois par jour.
  2. **Période** : 1200 s au lieu de 300 s divise le coût des réveils par 3,6 ; une station
     météo n'a pas besoin de mieux que 20 minutes.
  3. **Ne jamais laisser `WakeUp` = 1** sur une unité à batterie : c'est 10 fois le budget.
  4. **Plancher du DevKit** (matériel, essai au banc) : D5 non posée et DevKit alimenté par sa
     broche 3V3 depuis un régulateur à faible consommation sur J17-1 (arbitrage N2). Gain
     attendu ~1 Wh par jour, limité par ce que l'AMS1117 et la LED du DevKit continuent de tirer.
  5. Poste mineur : mesurer la fuite de D12 ; au pire (1 mA) elle coûte 0,16 Wh par jour.

### 9.4 n3pp — chargeur 5 V

| Mode | Sous 5 V | À la prise (chargeur ~75 %, 0,1 W à vide) |
|---|---|---|
| Veille 300 s, réveil 40 s | 3,5 Wh par jour | ~7 Wh par jour |
| Veille 300 s, réveil 100 s | 6,0 Wh par jour | ~10 Wh par jour |
| Jamais de veille (mode constaté en juin-juillet 2026) | 20 Wh par jour | ~29 Wh par jour, soit ~11 kWh par an |

Sur chargeur, l'énergie n'est pas un enjeu : ce qui compte est la tenue du 5 V (§4). À vérifier
sur l'ancienne carte : `etatPompe` vaut 1 sur 99,5 % des mesures de juin-juillet. Si c'est bien
l'état de la bobine, un relais y restait collé en permanence (0,39 W).

### 9.5 ffp5cs — secteur (Hi-Link) (→ rev 0.3 : bloc 5 V externe, même bilan côté 5 V)

ffp5cs alterne des éveils d'environ 7 minutes et des veilles légères (light sleep) de 33 minutes
le jour, 100 minutes la nuit. Les relais sont coupés pendant la veille ; le rail capteurs et les
servos restent alimentés (JP1 fermé, le firmware ne pilote pas la grille de Q7).

| Poste | Éveillé (12 % du temps) | En veille légère (88 %) | Moyenne | Wh par jour |
|---|---|---|---|---|
| DevKit (ESP32, AMS1117, puce USB) | ~100 mA | ~12 mA | 23 mA | 2,7 |
| Rail capteurs (LDO 5, OLED 12, sondes, RTC, INA) | 22 mA | 22 mA | 22 mA | 2,6 |
| Servos SG90 ×2 au repos | 12 mA | 12 mA | 12 mA | 1,4 |
| K1, pompe aquarium | 77 mA | 0 | 9 mA | 1,1 |
| HC-SR04 ×3 au repos | 6 mA | 6 mA | 6 mA | 0,7 |
| LED5 | 3 mA | 3 mA | 3 mA | 0,4 |
| **Total sous 5 V** | **~220 mA, 1,1 W** | **~55 mA, 0,28 W** | **~75 mA, 0,38 W** | **~9** |

À la prise, avec un Hi-Link très peu chargé (rendement ~60 %, 0,15 W à vide, à mesurer) :
**~0,8 W, ~19 Wh par jour, ~7 kWh par an**. Une pompe de 20 W qui tourne 12 % du temps fait
~58 Wh par jour. Le chauffage et les UV n'ont pas fonctionné sur les 30 derniers jours (eau
entre 18,0 et 23,8 °C, seuil à 15 °C).

### 9.6 ffp5cs — batterie AGM 12 V

Même électronique, vue à travers le buck (rendement ~80 %) : **~0,47 W, ~11 Wh par jour, un peu
moins de 1 Ah par jour**, soit ~16 % de la moitié utile d'une AGM de 144 Wh, hors pompes.

- **Les deux tiers partent pendant la veille**, dans ce qui reste alimenté alors que rien ne
  mesure : rail capteurs, servos, HC-SR04, LED5 (~43 mA sur 55).
- **Levier n° 1 : couper le rail capteurs et les servos pendant la veille légère.** La carte le
  permet déjà : JP1 retiré, c'est GPIO13 (GATE) qui commande Q7, comme sur msp et n3pp. Il faut
  que le firmware ffp5cs pilote cette broche (allumer au réveil, attendre que les capteurs
  démarrent, couper avant la veille). Gain : ~30 mA en moyenne, soit **~4,5 Wh par jour côté
  batterie, 40 % du total**. Poser alors C2 en 100 µF sur cette unité (`SIMULATION.md` §12).
- Les HC-SR04 (J7-J9) sont sur le 5 V permanent ; on les coupe aussi en déplaçant leur fil
  d'alimentation sur le 5 V commuté (§9.9).
- LED5 non posée sur une unité à batterie : ~0,5 Wh par jour.
- Le contact NC de K1 n'apporte rien ici : la pompe est arrêtée 88 % du temps.
- Rappel : avec un régulateur PWM, la tension du panneau au-dessus de celle de la batterie est
  perdue. Un panneau « 12 V » (18 V au point de puissance) y laisse environ un quart de sa
  production ; un panneau de tension plus élevée, davantage.

### 9.7 Ensemble des quatre unités

| Unité | Électronique, par jour | Ce qui domine | Premier levier |
|---|---|---|---|
| msp1 (18650) | 2 à 7 Wh côté batterie | durée et fréquence des réveils, puis le DevKit endormi | firmware : réveil ≤ 40 s, période 1200 s |
| n3pp (chargeur) | 7 à 29 Wh à la prise | veille active ou non | sans enjeu ; activer la veille par cohérence |
| ffp5cs secteur | ~19 Wh à la prise | capteurs et servos alimentés pendant la veille | sans enjeu devant les pompes |
| ffp5cs 12 V | ~11 Wh côté batterie | capteurs et servos alimentés pendant la veille | JP1 retiré et rail coupé par le firmware en veille |

### 9.8 Mesures à faire sur la première carte montée

1. Courant du DevKit 38 broches en veille profonde (multimètre en série sur le 5 V, DevKit seul).
2. Durée réelle d'un réveil msp et n3pp avec les firmwares actuels (horodatage du journal série).
3. Courant de ffp5cs éveillé puis en veille légère (banc INA226 du firmware `energie/`).
4. Fuite de D12 à 5,1 V (carte nue, sans DevKit ni LED5).
5. Creux du 3V3 à la fermeture de Q7 (`simulation/SIMULATION.md` §12).

### 9.9 Évolution envisagée : tout couper pendant la veille de ffp5cs (2026-11-18)

Discutée le 2026-11-18, **non décidée, rien d'implémenté**. Elle vise l'unité sur batterie 12 V ;
sur secteur elle n'apporte rien et JP1 fermé doit continuer de fonctionner tel quel.

**Carte : rien à modifier.** JP1 retiré, Q7 est commandé par le net `GATE` : GPIO13 sur le site
A1 (WROOM), GPIO3 sur le site A2 (S3). Ces broches sont libres dans le brochage
`PINMAP_UNIVERSAL` de ffp5cs (`ffp5cs/include/pins.h` ne définit pas de broche `GATE`
aujourd'hui). C'est le montage que msp et n3pp utilisent déjà.

**Firmware ffp5cs, à écrire** (sur une branche à part, activable par option) :

1. Au réveil : `GATE` à l'état haut, attendre le démarrage des capteurs (~2 s pour le DHT),
   réinitialiser l'OLED, les INA226 et la carte SD, qui perdent leur configuration à chaque
   coupure.
2. Avant la veille, après `prepareActuatorsForSleep` : lignes de données des capteurs et des
   servos au repos (sinon un capteur éteint se réalimente par ses fils de données), `TRIG` des
   ultrasons à l'état bas, puis `GATE` à l'état bas.
3. Un réglage pour désactiver la coupure.

**Modules ultrason** : la borne « 5V » de J7-J9 est sur le 5 V permanent. Pour les couper avec le
reste, sans reroutage : fil VCC des HC-SR04 sur `LDO_IN`, le 5 V commuté par Q7 — broche
centrale de J15 ou J16 (connecteurs servo), ou broche 2 de JP1 une fois le cavalier retiré.
TRIG, ECHO et GND restent sur J7-J9, la borne « 5V » reste vide. Attendre ~0,1 s après le
rallumage avant la première mesure. Bénéfice annexe possible : un HC-SR04 figé repart à la
remise sous tension (capteur d'aquarium figé à 81 mm pendant 31 h fin octobre 2026 ; lien non
établi).

**Points à vérifier** : les servos des nourrisseurs sont coupés eux aussi, s'assurer qu'aucun ne
bouge seul à la remise sous tension ; poser C2 en 100 µF sur cette unité (`SIMULATION.md` §12).

| Veille légère de ffp5cs | Courant ~ | Côté batterie 12 V, par jour |
|---|---|---|
| Aujourd'hui (JP1 fermé) | 55 mA | ~11 Wh |
| Capteurs et servos coupés | 21 mA | ~7 Wh |
| Ultrasons coupés aussi | 15 mA | ~6 Wh |
| LED5 non posée en plus | 12 mA | ~5,5 Wh |

## 10. Alimentation par l'USB du DevKit (rev 0.3 : D5 = 1N5822 et cavalier JP22)

*Section réécrite le 2026-11-22. Elle décrivait la rev 0.2.3, où D5 était un pont de fil et la
règle « jamais d'USB carte sous tension » sans exception (→ remplacé en rev 0.3). Chiffres
[simulé] : `simulation/sim7_d7_supply.py`, `SIMULATION.md` §13.2. Rien n'est mesuré.*

### 10.1 Ce que dit le schéma

Lu dans `generator/generate.py` et `kicad/n3-universal.kicad_sch` :

- **D5 est une 1N5822** (Schottky 3 A), anode sur le rail `+5V`, cathode sur `VIN_5V`, la broche
  5 V du DevKit. Posée sur **toutes** les cartes. Elle empêche le 5 V de l'USB de remonter vers
  le rail de la carte et vers sa source.
- **JP22** (2 broches, repère « USB SEUL ») est en parallèle de D5. Posé, il la court-circuite :
  la carte se comporte comme la rev 0.2.3.
- **C14** (220 µF) est sur `VIN_5V`, derrière la diode : réservoir du DevKit.
- **Q12** : grille à la masse par R57 (100k), drain sur `VIN_RAW` (J1, J2, J37), source sur `+5V`.
- **`+3V3_SW`**, le 3,3 V des capteurs, sort de U1 (LD1117V33), alimenté par `LDO_IN` : le `+5V`
  à travers Q7, ou à travers JP1. Il ne vient pas du 3,3 V du DevKit.
- Le 3,3 V du DevKit (`+3V3`) va au header J17, à la broche ON des cavaliers JP5, JP6, JP8, JP9
  et à la diode D11.
- C1 (1000 µF) est sur `+5V`. Les modules relais prennent leur 5 V sur `+5V` (borniers J39-J44).

### 10.2 Les cas

| JP22 | Source de la carte | USB | Ce qui se passe |
|---|---|---|---|
| **ôté** | présente | débranché | fonctionnement prévu ; la diode prend 0,3 à 0,45 V : **source à 5,2 V** |
| **ôté** | présente | branché | **sans danger** : le DevKit tire sur celle des deux sources qui lui présente la tension la plus haute (119 mA sur l'USB et 573 mA sur une source à 5,1 V [simulé]) ; rien ne remonte vers la source |
| **ôté** | absente | branché | le DevKit seul vit ; modules relais, U1 et capteurs restent éteints (rail à 0 V) |
| **posé** | présente | débranché | comme la 0.2.3 : +0,3 à +0,5 V de marge ; convient à une source à 5,0 V |
| **posé** | absente | branché | **« USB seul »** : toute la carte est alimentée par l'USB, à travers la diode du DevKit |
| **posé** | présente | branché | **interdit** |

Règle retenue le 2026-11-19 : **source à 5,2 V, JP22 ôté**. n3pp sur chargeur 5,0 V : JP22 posé
en service, jamais d'USB en même temps.

### 10.3 Marge du régulateur du DevKit selon la source

Marge = `VIN_5V` − 4,49 V sous 0,45 A (pointe Wi-Fi). Pire cas : courbe maximale de la diode,
Q12 défavorable. Tableau complet : `SIMULATION.md` §13.2.

| Profil | Source | JP22 ôté, pire / typique | JP22 posé, typique |
|---|---|---|---|
| msp1 | boost 5,1 V | +0,07 / +0,20 V | +0,52 V |
| msp1 | **boost 5,2 V** | **+0,18** / +0,30 V | +0,62 V |
| n3pp | chargeur 5,0 V | −0,15 / −0,01 V | **+0,32 V** |
| ffp5cs, 6 modules | bloc 5,0 V | −0,21 / −0,03 V | +0,29 V |
| ffp5cs, 6 modules | **bloc 5,2 V** | **+0,02** / +0,17 V | +0,50 V |
| ffp5cs 12 V | buck 5,1 V | −0,04 / +0,12 V | +0,44 V |

Une marge négative fait descendre le 3V3 du DevKit d'autant pendant les pointes Wi-Fi ; elle ne
coupe rien à elle seule. Le seuil de brownout par défaut de l'ESP32 est vers 2,4 V [à vérifier
sur la configuration des firmwares].

### 10.4 USB seul (JP22 posé, source débranchée) : ce qui tient

Chemin : prise USB → diode série du DevKit → broche 5 V → JP22 → `+5V`. [simulé], USB à 5,0 V et
câble de 0,3 Ω :

| Charge | Rail +5V | 3V3 du DevKit | Courant USB |
|---|---|---|---|
| DevKit au repos + rail capteurs | 4,58 V | 3,30 V | 170 mA |
| DevKit en Wi-Fi | 4,43 V | 3,23 V | 495 mA |
| + 1 module relais collé | 4,40 V | 3,20 V | 569 mA |
| + 2 modules collés | 4,37 V | 3,18 V | 643 mA |

Avec un câble de 0,6 Ω ou un port à 4,75 V, U1 (3,3 V capteurs) ne régule plus. Précautions :

1. **Usage d'établi seulement** : capteurs, un ou deux modules relais, pas de servo.
2. **Débrancher d'abord la source de la carte.** Le 5 V USB remonte vers `VIN_RAW` : Q12 conduit
   dans les deux sens dès que sa grille est à la masse. Il atteint donc ce qui est branché sur
   J1, J2 ou J37 (sortie du boost, du buck, du chargeur).
3. Remettre JP22 dans sa position de service avant de réinstaller l'unité.

### 10.5 Pourquoi « jamais les deux » quand JP22 est posé

Entre le port USB et la source de la carte, il ne reste que la diode série du DevKit.

- **La carte ne la maîtrise pas** : elle dépend du module. Si un clone n'en a pas, ou si elle est
  en court-circuit, une source de 5,2 V capable de 3 A pousse dans le port du PC.
- **Même avec la diode** : si la source faiblit ou se coupe, le port USB reprend d'un coup toute
  la charge de la carte (692 mA dans la simulation, modules et servos en plus).

Contrôle de la diode d'un DevKit, au multimètre en test de diode : module hors carte et hors
tension, câble USB-A branché sur le module, autre bout libre. Sur la fiche USB-A, repérer GND
(contact extérieur en continuité avec la broche GND du module) ; VBUS est l'autre contact
extérieur. Pointe rouge sur VBUS, pointe noire sur la broche 5 V du module : 0,15 à 0,45 V.
Pointes inversées : pas de conduction. Environ 0 V dans les deux sens : pas de diode ; avec
JP22 ôté la carte reste protégée par D5, avec JP22 posé ce module ne doit jamais voir l'USB et
une source en même temps.

### 10.6 Travailler sur une carte alimentée

- **JP22 ôté** (msp1, ffp5cs, ffp5cs 12 V) : brancher l'USB du DevKit carte alimentée est
  permis — moniteur série, téléversement.
- **JP22 posé** (n3pp sur chargeur 5,0 V) : débrancher le chargeur avant de brancher l'USB, ou
  OTA, ou moniteur série par un adaptateur USB-série 3,3 V sur J17 : deux fils seulement, GND
  (J17-2) et TX0 (J17-5) vers le RX de l'adaptateur. Ne relier ni J17-1 (3V3) ni J17-6 (+5V).
- Sur ffp5cs-WROOM, ôter JP1 pour téléverser par USB (pull-up 1-Wire sur GPIO2) : inchangé.

### 10.7 Simulé ou déduit, non mesuré

1. Chute réelle de la 1N5822 posée, à froid et à chaud : deux courbes de fiche simulées.
2. Présence, chute et calibre de la diode série sur les DevKit 38 broches et les clones S3
   utilisés (contrôle du §10.5, à faire sur chaque lot).
3. Tension réelle des sources en charge (boost, buck, bloc, chargeur) : à mesurer au multimètre
   sur J1 avant de fermer un coffret.
4. Conduction inverse de Q12 avec VGS ≈ −4,6 V : déduite de la fiche de l'IRF4905.
5. Tolérance du boost, du buck et du chargeur à ~4,4 V appliqués sur leur sortie, entrée non
   alimentée (cas USB seul, source restée branchée) : non vérifiée module par module.
6. Envoi de données vers RX0 par J17-4 alors que la puce USB du DevKit pilote la même ligne :
   non vérifié ; c'est pourquoi le §10.6 se limite à la lecture.
