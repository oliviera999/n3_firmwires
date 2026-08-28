# Fichiers d'assemblage (PCBA) — n3-universal rev 0.1

Fichiers au format attendu par JLCPCB (BOM : `Comment,Designator,Footprint,LCSC Part #` ;
CPL : `Designator,Mid X,Mid Y,Layer,Rotation`, repère identique aux gerbers, Y négatif).

| Fichier | Contenu |
|---------|---------|
| `BOM-PCBA-socle.csv` | 36 groupes / 113 composants THT du **socle commun** à tous les rôles (relais, transistors, diodes, résistances fixes, connectique, supports). Références LCSC vérifiées en stock le 2026-08-28 pour les pièces critiques ; lignes sans n° LCSC = génériques (choisir l'équivalent Basic/Extended le moins cher en stock). |
| `BOM-PCBA-conditionnels.csv` | Les **11 résistances à pose conditionnelle** (R17-R19, R27, R38/R39, R43-R47) — **NE PAS les faire assembler** : elles dépendent du rôle (msp / n3pp / ffp5cs) de chaque unité, et R38/R39 changent de **valeur** selon le profil d'alim. Pose à la main, voir « Consignes de pose par profil » du README principal. |
| `CPL-n3-universal-top.csv` | Positions/rotations des 113 composants du socle (tout en face Top). |

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
- **H1-H4** : trous de fixation (H1 = **vis nylon obligatoire**) ;
- résistances conditionnelles (fichier séparé, pose main).

## À acheter en plus (absents de la BOM d'origine — audit 2026-08-28)

- **2× clips porte-fusible 5×20 mm** à languette (entraxe ~22,5 mm, languette ≤ 1,3×2,6 mm)
  pour F1 — sans eux le profil secteur est inutilisable ;
- **5× cavaliers 2,54 mm** (JP1 fermé par défaut profil ffp5cs ; JP2-JP4 en 1-2) ;
- vis nylon M3 (H1) + entretoises.
