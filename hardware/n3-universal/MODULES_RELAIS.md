# Modules relais, câblage par profil et essai sur table — n3-universal rev 0.3

Depuis la rev 0.3, la carte ne porte **plus de relais, plus de 230 V et plus de transistor de
commande**. Chaque canal sort sur un bornier à vis trois points (**S / 5V / GND**) où se visse un
**module relais du commerce, actif à l'état haut**. Le 230 V ne touche plus que les contacts des
modules, hors de la carte. Les firmwares ne changent pas : broche à l'état haut = relais collé.

Chaque chiffre est marqué **[mesuré]**, **[simulé]** (ngspice, `simulation/SIMULATION.md` §13),
**[estimé]** ou **[à vérifier]**. Rien n'a encore été mesuré sur une carte : la fiche d'essai du §8
est à faire **avant de payer la commande**.

## 1. Quel module acheter

| À prendre | À ne pas prendre |
|---|---|
| Module **1 relais 5 V « actif à l'état haut »** (high level trigger), type **KY-019** : trois broches **S, +, −** (ou IN, VCC, GND), un relais bleu SRD-05VDC-SL-C, une LED | Module qui colle quand l'entrée est **à la masse** (low level trigger) : c'est le cas de beaucoup de modules à optocoupleur 2, 4 ou 8 canaux |
| Module à cavalier **H / L** par canal, cavalier mis sur **H** | Module 12 V ou 24 V (bobine marquée SRD-12VDC…) |
| Relais **statique** (SSR) à entrée 3-32 V, pour une charge 230 V résistive — hors du périmètre simulé ici **[à vérifier]** | Module sans écart visible, au dos, entre les pistes des contacts et la partie basse tension |

- Les annonces ne disent presque jamais si le module est actif à l'état haut (relevé Jumia du
  22/11/2026, `achats/prix.py`). **Acheter un seul module, faire l'essai du §8, puis acheter le
  reste.**
- Les KY-019 déjà en service sur l'installation ffp3 sont **actifs à l'état haut** et commandés
  directement par les GPIO (vérifié par Oliv, 2026-11-19). Ce qui reste à contrôler est leur
  schéma interne.
- Hypothèse de la simulation, **non contrôlée sur un exemplaire [à vérifier]** : entrée S →
  résistance de 1 kΩ (marquée « 102 ») → base d'un transistor NPN (marqué « J3Y » = S8050) →
  bobine de 70 Ω ; LED avec 1 kΩ ; diode de roue libre sur la bobine.
- Consommation d'un module collé : **75 mA sous 5 V [estimé]** (bobine 71 mA + LED 3 mA).

## 2. Les six canaux

Bornes lues depuis le bord haut de la carte, de gauche à droite : **S – 5V – GND** (sérigraphié
au-dessus de chaque bornier).

| Canal | Bornier | Cavalier | ffp5cs | n3pp | GPIO WROOM (A1) | GPIO S3 (A2) | Positions du cavalier |
|---|---|---|---|---|---|---|---|
| K1 | J39 | JP5 | pompe aquarium | pompe d'arrosage | 16 | 15 | AUTO / ON / OFF |
| K2 | J40 | JP6 | pompe réservoir | — | 17 | 16 | AUTO / ON / OFF |
| K3 | J41 | JP7 | chauffage | — | 18 | 17 | AUTO / OFF (pas de ON : voulu) |
| K4 | J42 | JP8 | lumière | — | 19 | 18 | AUTO / ON / OFF |
| K5 | J43 | JP9 | AUX1 | — | 23 | 48 | AUTO / ON / OFF |
| K6 | J44 | JP10 | AUX2 | — | 25 | 45 | AUTO / OFF (pas de ON : GPIO45 du S3) |

msp n'a aucun relais : ses six canaux ne sont pas posés.

**Ce qu'il y a entre le GPIO et la borne S** (par canal) : une résistance série de **470 Ω**
(R1-R4, R28, R31), puis un rappel de **10 kΩ** vers la masse (R5-R8, R29, R32). Le rappel garde le
relais au repos tant que l'ESP32 n'a pas démarré, pendant un téléversement, ou si le fil S est
coupé côté carte.

**Cavaliers JP5 à JP10** (3 broches, livrés sans cavalier) :

| Position | Effet | Courant dans le GPIO |
|---|---|---|
| aucun cavalier | **AUTO** : le firmware commande | 1,8 mA [simulé] |
| haut (côté « ON ») | relais **collé** quoi que fasse le firmware (borne S au +3V3) | 6,5 mA si le firmware demande l'arrêt [simulé], sans danger (limite recommandée 20 mA) |
| bas (côté « OFF ») | relais **au repos** quoi que fasse le firmware (borne S à la masse) | 6,5 mA si le firmware demande la marche [simulé] |

JP7 (chauffage) n'a pas de position ON : on ne force pas un chauffage. JP10 non plus : sur le S3,
le GPIO45 fixe la tension de la mémoire au démarrage et ne doit pas être tiré vers le haut à la
mise sous tension.

## 3. Brancher un module

Trois fils de 0,5 mm², 30 cm au plus **[estimé]**, avec embouts, du module vers le bornier du
canal :

| Borne de la carte | Broche du module | Rôle |
|---|---|---|
| **S** | S (ou IN) | commande 0 / 3,3 V |
| **5V** | + (ou VCC) | alimentation de la bobine : rail +5V de la carte, pris après la protection d'entrée Q12 |
| **GND** | − (ou GND) | masse |

Côté contacts du module (bornier bleu à vis : **COM, NO, NC**) : la charge se branche entre
**COM et NO**. Carte éteinte ou relais au repos, la charge est coupée.

Le 5 V des bobines traverse la carte (entrée J1/J2 → Q12 → rail +5V → bornier). Six modules
collés font 0,45 A **[estimé]** en plus du reste. Variante pour ffp5cs : alimenter les modules
directement depuis le bloc 5 V (broche + du module au + du bloc, la borne 5V de la carte reste
vide, les masses restent communes) : le régulateur du DevKit y gagne 0,10 à 0,15 V **[simulé]**.

## 4. Par profil

| | msp1 (météo) | n3pp (serre) | ffp5cs 230 V | ffp5cs 12 V |
|---|---|---|---|---|
| Modules relais | aucun | 1 sur J39 | 4 sur J39-J42 (+ 2 en option sur J43, J44) | 3 sur J39, J40, J42 (+ chauffage, AUX en option) |
| Source | boost réglé **5,2 V** sur J1 | chargeur 5 V 2 A sur le jack J2 | bloc **5 V ≥ 2 A, mieux 3 A**, sur J2 ou J1 | buck réglé **5,2 V** de J36 vers J37 |
| Cavalier JP22 | **ôté** | **posé** (chargeur à 5,0 V) | ôté si le bloc donne 5,2 V en charge ; posé sinon | **ôté** |
| USB du DevKit en même temps que la source | permis | **interdit** (JP22 posé) | permis seulement si JP22 est ôté | permis |
| Charges | — | pompe 12 V ou 230 V sur COM / NO | 230 V sur COM / NO, protection en amont | 12 V du bus sur COM / NO, diode de roue libre sur chaque pompe |
| Vue de câblage | `fritzing/n3-universal-msp1.fzz` | `…-n3pp.fzz` | `…-ffp5cs-230v.fzz` | `…-ffp5cs-12v.fzz` |

Les deux entrées 5 V sont le **jack J2** (5,5 × 2,1 mm, + au centre) et le **bornier J1** ; une
seule source à la fois. Le 12 V n'entre que par le bornier **J26**. ⚠️ Un bloc 12 V a la même
fiche qu'un bloc 5 V : marqué « PAS DE 12V » à côté du jack. La diode D12 écrête une surtension
brève, pas un 12 V permanent.

## 5. Le cavalier JP22 et la diode D5

D5 (1N5822) est posée sur toutes les cartes, entre le rail +5V et la broche 5 V du DevKit. Elle
empêche le 5 V de l'USB de remonter vers la source de la carte. Elle coûte **0,3 à 0,45 V
[simulé]**. JP22 (2 broches, repère « USB SEUL ») la court-circuite.

| JP22 | Source de la carte | USB du DevKit | Résultat |
|---|---|---|---|
| **ôté** | 5,2 V | débranché ou branché | fonctionnement normal ; le moniteur série et le téléversement se font carte alimentée |
| **ôté** | 5,0 V | — | marge du régulateur du DevKit nulle ou négative pendant le Wi-Fi : 3V3 à 3,1-3,3 V **[simulé]** ; redémarrages possibles avec un servo qui force (2,85 V **[simulé]**) |
| **posé** | 5,0 V | **jamais branché** | fonctionnement normal (la diode ne compte plus) |
| **posé** | aucune | branché | « USB seul » : toute la carte vit sur l'USB, à 4,4-4,6 V **[simulé]** : essais d'établi, pas de servo, deux modules au plus |
| **posé** | présente | branché | **interdit** : les deux sources se retrouvent reliées à travers la seule diode du DevKit |

Règle retenue le 2026-11-19 : **source réglée à 5,2 V, JP22 ôté**. Exception : n3pp sur un
chargeur à 5,0 V, JP22 posé, jamais d'USB en même temps.

## 6. Sécurité 230 V

- **Aucun 230 V sur la carte.** Il ne passe que par les contacts des modules.
- Modules et charges dans un **coffret isolant fermé** ; la carte peut être dans le même coffret,
  à distance des bornes 230 V, ou dans un autre.
- **Protection en amont obligatoire** (fusible ou disjoncteur, différentiel 30 mA) : la carte ne
  protège aucune charge.
- Couper la phase (**L**) par le relais, pas le neutre. Charge entre **NO** et le neutre.
- Un relais SRD tient 7 A résistifs sous 240 V **[fiche du relais, à vérifier sur le module
  acheté]** ; c'est la piste et le bornier **du module** qui fixent la limite réelle. Au-delà de
  1 kW ou pour un moteur de plus de 3 A : contacteur.
- Raccordement 230 V par un adulte, hors tension, embouts sur les fils souples.
- Regarder le dos du module : une fente ou un écart net (≥ 3 mm **[estimé]**) doit séparer les
  pistes des contacts du reste. Sinon, ne pas l'utiliser en 230 V.

## 7. Ce qui est simulé, ce qui ne l'est pas

| Point | Résultat | Marquage |
|---|---|---|
| Collage avec Rs 470 Ω, module à 1 kΩ, pire cas (3,0 V, gain faible, −10 °C) | transistor saturé, 98 % de la tension sur la bobine | [simulé] |
| 150 tirages (tolérances, 4,5-5,25 V, −10 à 60 °C) | 0 échec, bobine ≥ 4,44 V | [simulé] |
| Même chose avec Rs 1 kΩ | 0 échec sur 150 tirages, mais au pire cas le transistor ne sature plus : 3,7 à 4,0 V sur la bobine pour 5,25 V d'alimentation, à la limite du collage | [simulé] |
| Démarrage, GPIO en l'air | borne S à 0 V, bobine 0 mA | [simulé] |
| Démarrage, GPIO tiré vers 3,3 V par 45 kΩ (résistance interne typique) | 1,4 mA dans la bobine, relais au repos | [simulé] |
| Idem avec 30 kΩ (bas de fourchette) et le module le plus sensible | 0,7 V sur la bobine : sous le seuil de collage (3,75 V au plus) | [simulé] |
| Impulsion de 60 µs à la mise sous tension du S3 | 3 à 6 mA crête, pas de collage | [simulé] |
| Valeur réelle de la résistance et du transistor du module | — | **[à vérifier]**, §8 |
| Tenue du module en 230 V, distances d'isolement | — | **[à vérifier]** à l'œil |

Si le module acheté a une résistance d'entrée plus forte que 1 kΩ, Rs se remplace par une valeur
plus faible (220 Ω) sans toucher au circuit imprimé.

## 8. Fiche d'essai sur table — à faire avant de payer

Matériel : un module relais, un ESP32 DevKit sur son câble USB (il fournit le 3,3 V et le 5 V),
une plaque d'essai, des résistances **470 Ω, 1 kΩ, 10 kΩ**, des fils Dupont, un multimètre.
Rien en 230 V : les contacts du module restent vides.

Alimenter le module : **+** sur la broche 5V (VIN) du DevKit, **−** sur GND.

| N° | Montage | Résultat attendu | Si ce n'est pas le cas |
|---|---|---|---|
| 1 | Lire le marquage de la petite résistance près de la broche S et du transistor | « 102 » (1 kΩ) et « J3Y » | noter ce qui est lu et me le donner : la valeur de Rs se recalcule |
| 2 | S en l'air | relais au repos, LED éteinte | module sensible aux parasites : le rappel de 10 kΩ de la carte y remédie (essai 5) |
| 3 | S relié au GND | relais **au repos** | s'il colle, le module est **actif à l'état bas : ne convient pas** |
| 4 | S relié au 3V3 du DevKit à travers **470 Ω** | relais **collé** franchement, LED allumée | ne convient pas tel quel : me donner le marquage de l'essai 1 |
| 5 | Comme 4, avec en plus **10 kΩ** entre S et GND ; puis débrancher le fil du 3V3 | collé, puis **retombe aussitôt** | — |
| 6 | S relié au 3V3 à travers **1 kΩ** (à la place de 470 Ω), 10 kΩ vers GND en place | relais **collé** franchement | la marge est mince : le dire avant la commande, Rs passera à 220 Ω |
| 7 | Mesurer la tension entre les deux pattes de la bobine pendant l'essai 4 (ou entre + et le collecteur du transistor) | ≥ 4,5 V pour 5 V d'alimentation | transistor non saturé : même suite que l'essai 4 |
| 8 | Remplacer le 3V3 par un GPIO : programme qui met GPIO16 à l'état haut 2 s puis bas 2 s, GPIO16 → 470 Ω → S, 10 kΩ de S à GND | le relais bat toutes les 2 s | — |
| 9 | Montage 8 : appuyer dix fois sur EN (reset), puis téléverser le programme une fois | **aucun claquement** pendant les redémarrages et le téléversement | noter à quel moment il claque |
| 10 | Montage 8, programme à l'état bas : relier S directement au 3V3 (c'est le cavalier en position ON) | relais collé ; le GPIO ne chauffe pas | — |
| 11 | Montage 8, programme à l'état haut : relier S directement au GND (position OFF) | relais au repos | — |
| 12 | Mesurer au multimètre le bloc 5 V prévu pour ffp5cs, chargé par une résistance ou une lampe d'environ 1 A | ≥ 5,1 V | prévoir JP22 posé sur cette unité (§5), ou un bloc réglable |

Essais 3, 4, 6 et 9 réussis : les modules de ce modèle conviennent, la carte peut être commandée.

Après réception de la première carte montée (avant de fermer un coffret) : source à 5,2 V sur J1,
JP22 ôté, USB branché : mesurer TP2 (5 V) et TP4 (3V3 du DevKit) ; attendu 5,1 V et 3,30 V.
Débrancher la source, USB seul, JP22 ôté : le DevKit vit, TP2 à 0 V.
