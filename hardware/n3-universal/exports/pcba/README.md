# Fichiers d'assemblage (PCBA) — n3-universal rev 0.1

Fichiers au format attendu par JLCPCB (BOM : `Comment,Designator,Footprint,LCSC Part #` ;
CPL : `Designator,Mid X,Mid Y,Layer,Rotation`, repère identique aux gerbers, Y négatif).

| Fichier | Contenu |
|---------|---------|
| `BOM-PCBA-socle.csv` | 34 groupes / **110 composants** THT du **socle commun à tous les rôles et à tous les profils d'alimentation** (relais, transistors, diodes, résistances fixes, connectique, supports). Références LCSC vérifiées en stock le 2026-08-28 pour les pièces critiques ; lignes sans n° LCSC = génériques (choisir l'équivalent Basic/Extended le moins cher en stock). |
| `BOM-PCBA-conditionnels.csv` | **14 pièces à pose conditionnelle — NE PAS les faire assembler.** Chaque ligne porte sa condition en clair : les 11 résistances qui dépendent du **rôle** (msp / n3pp / ffp5cs) ou dont la **valeur** change selon le profil, et les 3 pièces du **bloc d'entrée secteur** (J27, F1, RV1) réservées au profil (d). Pose à la main. |
| `CPL-n3-universal-top.csv` | Positions/rotations des **110 composants du socle** (tout en face Top), dans le repère des gerbers. |

## Verdict assemblage (audit 2026-08-28)

**Pour 5 cartes, l'assemblage JLCPCB n'est PAS recommandé** : carte 100 % traversant,
~30 références « Extended » → **~90 $ de loading fees** qui dominent les ~26 $ de
main-d'œuvre facturée (~300 points/carte × 0,0173 $), soit **+150-200 $** pour éviter
~10-14 h de fer à souder. Scénario de référence : **PCB nu + panier LCSC + soudure main**
(~130-170 € les 5 cartes complètes). Les fichiers sont fournis pour :
- une future série homogène (≥ 5 unités du même rôle) où le PCBA redevient discutable ;
- l'import du panier LCSC (colonne `LCSC Part #`).

## Exclusions d'assemblage (déjà retirées des fichiers)

- **A1 / A2** : modules ESP32 **enfichés**, jamais soudés (+ leurs supports femelles
  1×15 / 1×22, à acheter en barrette sécable) ;
- **PS1 (HLK-20M05)** : option du seul profil secteur — **mesurer les entraxes d'un
  module réel avant toute commande** (le dessin mécanique du datasheet contredit
  l'empreinte, voir COMMANDE.md) ;
- **J27, F1, RV1** : le reste du bloc d'entrée secteur, passé en conditionnel le
  2026-08-30. Les poser sans PS1 donnerait à une unité 5 V, batterie 1S ou bus 12 V
  un bornier sérigraphié « ENTREE SECTEUR 230V » sans rien derrière — trompeur, et
  inutile. Ces trois pièces se posent **ensemble avec PS1**, ou pas du tout ;
- **H1-H4** : trous de fixation (H1 = **vis nylon obligatoire**) ;
- résistances conditionnelles (fichier séparé, pose main).

## À acheter en plus (absents de la BOM d'origine — audit 2026-08-28)

- **2× clips porte-fusible 5×20 mm** à languette (entraxe ~22,5 mm, languette ≤ 1,3×2,6 mm)
  pour F1 — sans eux le profil secteur est inutilisable ;
- **5× cavaliers 2,54 mm** (JP1 fermé par défaut profil ffp5cs ; JP2-JP4 en 1-2) ;
- vis nylon M3 (H1) + entretoises.

## Ce qui se peuple selon le profil d'alimentation

Le socle est identique pour les quatre profils. Ne changent que les pièces ci-dessous,
toutes regroupées dans `BOM-PCBA-conditionnels.csv` :

| Profil | À poser en plus du socle |
|--------|--------------------------|
| (a) 5 V externe (jack J2 / bornier J1) | rien |
| (b) solaire 1S (TP4056 + 18650 hors carte) | R38 = 100k, **R39 = 100k** |
| (c) bus 12 V (buck externe) | R38 = 100k, **R39 = 27k** |
| (d) secteur | **PS1 + J27 + F1 + RV1** (les quatre ensemble), + R38/R39 selon la mesure batterie éventuelle |

⚠️ **R39 est le piège de cette BOM** : la colonne « Valeur » du `BOM.csv` d'origine
porte `27k`, la valeur du profil bus 12 V. La poser sur une unité batterie 1S fausse
la jauge d'un facteur ~2,4 ; à l'inverse, poser 100k sur un bus 12 V envoie ~7,75 V
sur une entrée ADC prévue pour 3,3 V — **destruction de l'entrée**.

Et selon le **rôle** de l'unité : R17-R19 (ffp5cs uniquement), R43-R46 (LDR msp),
R27 (LDR), R47 (msp). Détail dans chaque ligne de la BOM conditionnelle.
