# Prompt — chantier n3-universal rev 0.3 (carte sans 230 V, sans relais, sans transistor)

> Prompt de lancement du chantier, rédigé par Oliv le 2026-11-19. Il remplace la version du
> 2026-11-18 de ce fichier, dont la variante « étage à transistor conservé » est abandonnée
> (elle reste dans l'historique git).
>
> **État au 2026-11-22 : chantier réalisé** (rev 0.3.0). Arbitrages et écarts :
> [`SIMPLIFICATION_PROPOSEE.md`](SIMPLIFICATION_PROPOSEE.md) §11. Un point de ce prompt a été
> révisé en cours de route : D3, six borniers 3 points S / 5V / GND au lieu d'un bornier 8 points.

---

Tu reprends la carte porteuse `hardware/n3-universal/` du dépôt GitHub `oliviera999/n3_firmwires`
(copie locale sur mon portable). La rev 0.2.3 (278 × 135 mm)
est validée mais **pas commandée**. Je veux la rev 0.3 : la même carte sans l'alimentation 230 V,
sans les relais et sans l'étage à transistor. Il n'y aura qu'**une seule commande** de cartes chez
JLCPCB : tout doit fonctionner du premier coup. Je préfère la bonne carte à la plus rapide.

Je suis novice en électronique : explique-moi les arbitrages comme un ingénieur senior le ferait
avec un collègue débutant, sans jargon inutile.

## Accès au dépôt

**J'autorise explicitement Claude à rattacher le dépôt `oliviera999/n3_firmwires` en écriture**
pour ce chantier. Méthode habituelle : branche éphémère `claude/…` créée depuis `origin/master`,
PR en brouillon vers `master`, je fusionne moi-même. Ne pousse jamais sur `master`.

## À lire avant de commencer

1. `CLAUDE.md` à la racine, section « Propagation des changements » : un changement n'est fini
   que lorsque tout ce qui en dépend a suivi, dans la même PR.
2. `hardware/n3-universal/SIMPLIFICATION_PROPOSEE.md` : mesures sur la 0.2.3 (§2), scénarios,
   liste des fichiers à répercuter (§10). Si le §11 « Arbitrages du 2026-11-19 » n'y est pas
   (branche `claude/n3-universal-arbitrages-2026-11-19` peut-être jamais poussée), **ajoute-le
   dans ta PR** à partir de la section « Décisions » ci-dessous, et aligne `PROMPT_REV_0.3.md`.
3. `hardware/n3-universal/README.md` (sections Rev 0.2.2 et 0.2.3 : méthode de reroutage),
   `ALIMENTATION.md` (§4, §5, §10), `COMMANDE.md`, `EVOLUTIONS_PROPOSEES.md` (forçage JP5-JP10).
4. Dans le projet claude.ai : `claude/etat-carte-n3-universal.md` (fiche d'état au 19/11).

## Décisions (ne pas rouvrir)

Prises avant le 19/11 :

- Le site S3 (A2) et la microSD **restent** sur la carte.
- L'alimentation 230 V **quitte** la carte : PS1 (Hi-Link), F1, RV1, J27, trou H5, pistes secteur,
  19 fentes, règles `Mains` du `.kicad_dru`, zone d'exclusion du plan de masse, vis nylon. Le
  profil ffp5cs 230 V s'alimente par un bloc 5 V externe sur J2, comme n3pp.
- Les relais **quittent** la carte : K1-K6 et leurs borniers de charge J3-J6, J23, J24. Ils sont
  remplacés par des modules relais du commerce, hors carte.
- Borniers à vis 5,08 partout, composants traversants, soudure à la main, pas d'assemblage usine.
- **Aucun firmware ne change** : mêmes GPIO, même polarité (broche haute = relais collé). Si une
  étape semble exiger une modification de firmware ou de `pinmap_universel_propose.json`,
  arrête-toi et demande-moi.
- Largeur 278 mm inchangée pour la 0.3 (bande logique intacte).

Prises le 19/11 :

- **D2 — étage à transistor retiré.** Mes modules relais sont des KY-019 (relais SRD-05VDC-SL-C
  inverseur NO/COM/NC, broches S / + / −, transistor, diode et LED intégrés, sans optocoupleur),
  **actifs à l'état haut** : je l'ai vérifié. Mon installation ffp3 (schéma Fritzing
  `aquapocontrol2.fzz` sur mon Drive) les commande déjà directement par les GPIO de l'ESP32,
  modules alimentés en 5 V, masses communes. Chaque canal devient :

  ```
  GPIO ──[Rs]──┬──> bornier de commande ──> S du module (actif à l'état haut)
               │
            [10 kΩ] vers la masse   (repos au démarrage, GPIO flottant ou fil coupé)
               │
            JP5..JP10 : sans cavalier = AUTO ; 1-2 = +3V3 direct (ON) ; 2-3 = masse directe (OFF)
  ```

  Supprimés par canal : BC337, LED témoin et sa résistance, Rf (le cavalier relie maintenant la
  ligne directement, sinon le forçage ne l'emporte pas sur le GPIO). JP7 (chauffage) reste sans
  position ON. Rs protège le GPIO pendant un forçage : 470 Ω ou 1 kΩ, **à trancher par
  simulation**. Modules compatibles : actifs à l'état haut (KY-019, ou module H/L cavalier sur H).
- **D3 — connecteur de commande : bornier à vis 8 points** (6 lignes, +5 V, masse), à la place du
  header J38. Règle générale : bornier à vis pour tout ce qui reste branché en service, Dupont
  seulement pour l'établi (J17, cavaliers).
- **D4 — diodes de roue libre D1-D4, D6, D7 supprimées**, empreintes comprises (les modules ont la
  leur). Récupère la place en hauteur.
- **D7 — alimentation par l'USB du DevKit : D5 = 1N5822 posée sur toutes les cartes** (anode côté
  rail `+5V`, cathode côté `VIN_5V`), **avec un cavalier 2 points en parallèle**. Cavalier ôté
  (état de livraison) : carte seule, ou carte et USB branchés ensemble sans risque. Cavalier posé :
  l'USB seul alimente toute la carte (établi : capteurs, un ou deux modules, jamais de servo) ;
  jamais l'USB et l'alimentation de la carte ensemble. Cavalier plutôt qu'interrupteur (élèves).
  Remplace la décision A1 du 2026-10-17 (D5 = pont de fil). Sérigraphie explicite des deux
  positions sur la carte.
- D1 : on fait la rev 0.3. D5 (fabricant) : JLCPCB par défaut ; je demanderai un devis à PCB Maroc
  une fois les gerbers sortis (plus de fentes).

## Ce que tu dois vérifier par simulation avant de figer le dessin

1. **Rs** : courant de base du transistor d'un KY-019 commandé en 3,3 V à travers Rs (prends une
   hypothèse prudente sur sa résistance de base et son gain, et dis laquelle), avec le 10 kΩ en
   dérivation ; courant dans le GPIO pendant un forçage ON ou OFF qui contredit le GPIO. Choisis
   Rs et justifie.
2. **Démarrage** : chaque ligne reste sous le seuil de collage pendant le reset et le boot, y
   compris **GPIO45 sur le site S3** (strapping VDD_SPI, doit rester bas) et les autres broches
   de strapping de la cartographie.
3. **D7** : tension sur `VIN_5V` avec la 1N5822 pour chaque profil (msp1 sur 18650 + boost, n3pp
   sur chargeur 5 V, ffp5cs sur bloc 5 V, ffp5cs 12 V sur buck), au courant de pointe du DevKit
   (Wi-Fi) ; marge du régulateur 3,3 V du DevKit. Mode cavalier posé : chute et courant admissible
   en USB seul. Signale tout profil où la marge est insuffisante.
4. **C2 par profil** (100 µF msp, non posé n3pp, 470 µF ffp5cs) : à revoir pour ffp5cs sur bloc
   5 V et avec la diode D7.

## Le travail

Objectif : une rev 0.3 qui est la 0.2.3 **amputée** de la bande relais et du coin secteur, bande
logique inchangée, étage de commande allégé. Taille estimée ≈ 278 × 85-90 mm, à confirmer au
placement.

1. **Générateur** (`generator/generate.py`, source de vérité) : `relay_channel()` réduit au schéma
   ci-dessus ; bloc PSU supprimé ; D5 en 1N5822 + cavalier ; bornier de commande 8 points ; J21,
   J22 et trous H1, H2 replacés ; contour ; textes de schéma et de sérigraphie (`SCH_TEXTS`,
   `SILK`) ; BOM ; `REV = 0.3.0`.
2. **Routage** : repartir du cuivre validé de la 0.2.3 comme amorce pour la bande logique (méthode
   de la 0.2.2, README), pas d'un reroutage à blanc. Retirer le tracé secteur, les fentes, les
   règles `Mains` et les contrôles `check_mains_*` devenus sans objet.
3. **Répercussions** : tout le tableau du §10 de `SIMPLIFICATION_PROPOSEE.md`, plus ce
   qu'entraînent D2 et D7 :
   - `tools/check_pinmap_vs_firmware.py` (nouvelle topologie du forçage, plus de transistor) ;
   - `simulation/` (`sim1_relay.py` à remplacer par la commande d'un module actif haut ;
     simulations d'alimentation avec la 1N5822, `SIMULATION.md`) ;
   - `ALIMENTATION.md` §4, §5 et §10, README (règle d'alimentation n° 1, cavaliers) ;
   - `achats/` : modules relais actifs haut et bloc 5 V par profil, 1N5822 et cavalier par carte,
     prix relevés et datés dans `prix.py` ; `build_achats.py` ;
   - `fritzing/` : modules relais et bloc 5 V dessinés, régénération ;
   - `COMMANDE.md` (dimensions, plus de fentes, PCB Maroc), `COMPOSANTS_FFP5CS.csv`, README des
     deux niveaux, `VERIFICATION.md`, workflow CI ;
   - au passage : la ligne « 1N4007 D1-D7 » de `COMMANDE.md`.
4. **Documentation de câblage** par profil : quel module (n3pp : 1 canal ; ffp5cs : 6 canaux,
   par exemple 6 KY-019 ou un module H/L 8 canaux sur H), comment le relier au bornier, d'où vient
   le 5 V des bobines, position du cavalier D7, et consignes de sécurité (coffret isolant, 230 V
   uniquement sur les modules, protection des charges en amont, regarder le dessous d'un module
   avant d'en acheter plusieurs, pas d'écart visible = pas d'achat).
5. **Essai sur table à faire avant de payer** : remplace l'essai du §7 de l'étude par un essai
   adapté (un KY-019 relié au DevKit par Rs, 10 kΩ vers la masse, forçages ON/OFF, reset et
   téléversement sans claquement, fil coupé = repos, USB + alimentation avec et sans cavalier D7).
   Écris-le comme une fiche que je peux suivre seul.

## Contraintes

- Ne rien supprimer des archives datées (`AUDIT-*.md`, sections « Rev x.y » passées,
  `EVOLUTIONS_PROPOSEES.md`) : ajouter « → remplacé en rev 0.3 » si nécessaire.
- Garder la 0.2.3 commandable jusqu'à validation de la 0.3 : ne retire pas
  `exports/gerbers-n3-universal-v0.2.3.zip` sans mon accord.
- Une estimation reste une estimation : marque chaque chiffre **[mesuré]**, **[simulé]**,
  **[estimé]** ou **[à vérifier]**.
- Travaille par étapes commitées, chacune cohérente (pas de commit qui casse la CI).

## Étapes et points d'arrêt

1. **Avant de coder** : lis tout, fais les simulations 1 à 3, puis envoie-moi en une page ton plan,
   la valeur de Rs proposée, les marges D7 par profil, et toute question bloquante. Attends mon
   feu vert avant de toucher `generate.py`.
2. **Après le placement** : dimensions réelles et une vue de la carte avant de router.
3. **À la fin** : la PR en brouillon et le compte rendu ci-dessous.

## Vérifications exigées avant de dire que c'est fini

Toutes doivent passer, sorties à l'appui :

- générateur idempotent (schéma, BOM, `n3u_sim.lib` identiques après régénération) ;
- ERC 0 erreur ; DRC 0 erreur, 0 non connecté, 0 écart de parité ;
- `tools/check_fet_orientation.py`, `check_pinmap_vs_firmware.py`, `check_silk_refs.py`,
  `check_pcb_clearance.py`, `annotate_pcb_roles.py --check`, `sync_pcb_sim_fields.py --check`,
  `check_jlcpcb_caps.py` ;
- `achats/build_achats.py --check`, `fritzing/generate_fritzing.py --check` ;
- banc SPICE sur la netlist KiCad et simulations de `simulation/` ;
- `export_fab.py --rev 0.3.0` (DRC bloquant) ;
- Hardware CI verte sur la PR.

## Ce que j'attends en retour à la fin

1. Dimensions réelles de la carte, vue 3D régénérée, tableau des contrôles.
2. Liste de ce qui a été répercuté, et de ce qui reste à ma charge : JLCDFM sur le nouveau zip,
   devis PCB Maroc, achat et inspection des modules relais, essai sur table, réexport du
   `schema.pdf` depuis KiCad si tu ne peux pas le faire.
3. Tout écart par rapport à ce prompt, dit clairement, avec la raison.
4. La mise à jour de `claude/etat-carte-n3-universal.md` dans le projet.
