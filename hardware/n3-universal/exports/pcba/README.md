# Fichiers d'assemblage (PCBA) — n3-universal rev 0.3.0

> Rev 0.3 : relais, étage transistor et bloc secteur ont quitté la carte ; les fichiers ont été
> régénérés par `generator/export_pcba.py` le 2026-11-22.

Fichiers au format attendu par JLCPCB (BOM : `Comment,Designator,Footprint,LCSC Part #` ;
CPL : `Designator,Mid X,Mid Y,Layer,Rotation`, repère identique aux gerbers, Y négatif).
Depuis la 0.2 ils sont **générés** par `generator/export_pcba.py` à partir de `BOM.csv` et de
`exports/cpl-jlcpcb.csv` (plus de fichiers tenus à la main : en 0.1.2 ils s'étaient
désynchronisés du PCB). Codes LCSC lus sur la page produit le 2026-10-06.

| Fichier | Contenu |
|---------|---------|
| `BOM-PCBA-socle.csv` | 41 groupes / **120 composants** THT du **socle commun à tous les rôles** (transistors, diodes, résistances, cavaliers-headers, connectique). Lignes sans n° LCSC = génériques (Basic/Extended le moins cher en stock, ou achat local). |
| `BOM-PCBA-conditionnels.csv` | **8 pièces à pose manuelle selon le profil d'alimentation — NE PAS les faire assembler** : bloc bus 12 V (J26, Q11, D8, D9, R40, C5, J36, J37, profil c). Le bloc d'entrée secteur (J27, F1, RV1) a quitté la carte en rev 0.3. Depuis la 0.2 **plus aucune résistance n'est conditionnelle** : les diviseurs ADC / HC-SR04 / VBAT sont sélectionnés par cavalier (JP12-JP20), tout se pose. |
| `CPL-n3-universal-top.csv` | Positions/rotations des 120 composants du socle (tout en face Top), dans le repère des gerbers. |

## Verdict assemblage (inchangé)

**Pour 5 cartes, l'assemblage JLCPCB n'est PAS recommandé** : carte 100 % traversante,
~35 références « Extended » → loading fees qui dominent la main-d'œuvre. Scénario de
référence : **PCB nu + colis LCSC (`COMMANDE.md` §2) + achats locaux + soudure main**.
Les fichiers sont fournis pour une future série homogène et pour l'import du panier LCSC.

## Exclusions d'assemblage (déjà retirées des fichiers)

- **A1 / A2** : modules ESP32 **enfichés** (supports femelles 1×19 / 1×22 en barrette sécable, rev 0.2.3) ;
- **H1-H4, H6, H7** : trous de fixation (rev 0.3 : plus de vis nylon imposée) ; **TP1-TP4** : pads nus ;
- **D11** : empreinte double — la BOM socle porte la variante **SOD-123 BAT43W (C19167)**,
  seule disponible chez LCSC ; un BAT85 traversant se pose sur les deux trous à la place.
