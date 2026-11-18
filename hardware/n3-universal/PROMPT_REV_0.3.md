# Prompt — chantier n3-universal rev 0.3 (carte sans 230 V)

> À coller tel quel dans le projet / la session PCB (dépôt `oliviera999/n3_firmwires`).
> Rédigé le 2026-11-18. L'étude complète est dans
> [`SIMPLIFICATION_PROPOSEE.md`](SIMPLIFICATION_PROPOSEE.md) : la lire en premier.

---

Tu reprends la carte porteuse `hardware/n3-universal/` du dépôt `oliviera999/n3_firmwires`
(rev 0.2.3, 278 × 135 mm, validée mais **pas encore commandée**). Je veux la simplifier en
sortant de la carte tout ce qui touche au 230 V. Il n'y aura qu'une seule commande de cartes :
je préfère commander la bonne carte que la plus rapide.

## À lire avant de commencer

1. `CLAUDE.md` à la racine, section « Propagation des changements » : un changement n'est fini
   que lorsque tout ce qui en dépend a suivi, dans la même PR.
2. `hardware/n3-universal/SIMPLIFICATION_PROPOSEE.md` : mesures sur la 0.2.3, scénarios étudiés,
   variante recommandée, décisions ouvertes, liste des fichiers à répercuter (§10).
3. `hardware/n3-universal/README.md` (sections Rev 0.2.2 et 0.2.3 : méthode de reroutage),
   `COMMANDE.md`, `ALIMENTATION.md`, `EVOLUTIONS_PROPOSEES.md` (forçage JP5-JP10, header J38).

## Ce qui est décidé (ne pas rouvrir)

- Le site S3 (A2) et la microSD **restent** sur la carte.
- L'alimentation 230 V **quitte** la carte : PS1 (Hi-Link), F1, RV1, J27, trou H5. Le profil
  ffp5cs 230 V s'alimente par un bloc 5 V externe sur J2, comme n3pp.
- Les relais **quittent** la carte : K1-K6 et leurs borniers de charge J3-J6, J23, J24. Ils sont
  remplacés par un module relais du commerce, hors carte.
- Borniers à vis 5,08 partout, composants traversants, soudure à la main, pas d'assemblage usine.
- **Aucun firmware ne change** : mêmes GPIO, même polarité (broche haute = allumé). Si une étape
  semble exiger une modification de firmware, arrête-toi et demande.

## Ce que je recommande et que tu dois me faire confirmer

Garder sur la carte l'**étage à transistor** de chaque canal (Rb 1 k, Rp 10 k, BC337, LED et sa
résistance, Rf, cavalier JP5-JP10) et ne retirer que le relais, sa diode et son bornier. Le
collecteur (`REL{n}_SW`) devient une sortie à collecteur ouvert qui tire à la masse l'entrée d'un
module relais à optocoupleur **actif à l'état bas**. Le forçage AUTO / ON / OFF reste tel quel,
JP7 (chauffage) reste sans position ON. Raisonnement sur le schéma, pas encore essayé.

## Questions à me poser avant de toucher `generate.py`

Pose-les en une fois, avec ta recommandation pour chacune :

1. **L'essai sur table** (`SIMPLIFICATION_PROPOSEE.md` §7) a-t-il été fait, et avec quel résultat ?
   S'il n'est pas fait, tu peux préparer le chantier mais pas figer l'étage de sortie.
2. **Connecteur de commande** (6 lignes, +5 V, masse) à la place du header J38 : bornier à vis
   8 points (ma préférence) ou header 1×8 ?
3. **Diodes de roue libre D1-D4, D6, D7** : supprimées, ou empreintes gardées non posées pour
   pouvoir commander une bobine nue ?
4. **Largeur** : je pars sur 278 mm inchangés (bande logique intacte). Dis-moi si tu vois un gain
   de largeur sans risque de routage, sinon n'y touche pas.
5. **Numéro de révision** : 0.3.0, sauf objection.

## Le travail

Objectif : une rev 0.3 qui est la 0.2.3 **amputée** de la bande relais et du coin secteur, bande
logique inchangée. Taille visée ≈ 278 × 90 mm (estimation à confirmer au placement).

1. **Générateur** (`generator/generate.py`, source de vérité) : `relay_channel()` sans relais,
   diode ni bornier ; bloc PSU supprimé ; connecteur de commande ; J21, J22 et les trous H1, H2
   replacés ; contour de carte ; textes de schéma et de sérigraphie ; BOM ; `REV`.
2. **Routage** : repartir du cuivre validé de la 0.2.3 comme amorce pour la bande logique (méthode
   de la 0.2.2, README), pas d'un reroutage à blanc. Retirer le tracé secteur en dur, les fentes,
   les règles `Mains` du `.kicad_dru` et les contrôles `check_mains_*` devenus sans objet.
3. **Répercussions** : tout le tableau du §10 de `SIMPLIFICATION_PROPOSEE.md` — gardes `tools/`,
   simulations, `achats/` (modules relais et bloc 5 V par profil, prix relevés et datés),
   `fritzing/`, `COMMANDE.md`, `ALIMENTATION.md`, `COMPOSANTS_FFP5CS.csv`, README des deux niveaux,
   `VERIFICATION.md`, `exports/`, workflow CI.
4. **Documentation de câblage** : pour chaque profil, quel module relais (nombre de canaux, type
   actif bas, position du cavalier H/L s'il existe), comment le relier, d'où vient le 5 V des
   bobines, et les consignes de sécurité (coffret isolant, 230 V uniquement sur le module,
   protection des charges en amont, vérifier le dessous du module avant achat).

## Contraintes

- Ne rien supprimer des archives datées (`AUDIT-*.md`, sections « Rev x.y » passées,
  `EVOLUTIONS_PROPOSEES.md`) : ajouter « → remplacé en rev 0.3 » si nécessaire.
- Garder la 0.2.3 commandable jusqu'à ce que la 0.3 soit validée : ne retire le zip
  `gerbers-n3-universal-v0.2.3.zip` qu'à la fin, quand je l'aurai dit.
- Travailler sur une branche éphémère rebasée sur `origin/master`, PR en brouillon.
- Une estimation reste une estimation : donne les dimensions réelles une fois le placement fait.

## Vérifications exigées avant de dire que c'est fini

Toutes doivent passer, sorties à l'appui :

- générateur idempotent (schéma, BOM, `n3u_sim.lib` identiques après régénération) ;
- ERC 0 erreur ; DRC 0 erreur, 0 non connecté, 0 écart de parité ;
- `tools/check_fet_orientation.py`, `check_pinmap_vs_firmware.py`, `check_silk_refs.py`,
  `check_pcb_clearance.py`, `annotate_pcb_roles.py --check`, `sync_pcb_sim_fields.py --check`,
  `check_jlcpcb_caps.py` ;
- `achats/build_achats.py --check`, `fritzing/generate_fritzing.py --check` ;
- banc SPICE sur la netlist KiCad et simulations `simulation/` ;
- `export_fab.py --rev 0.3.0` (DRC bloquant) ;
- Hardware CI verte sur la PR.

## Ce que j'attends en retour

1. Avant de coder : tes questions et ton plan, en une page.
2. À la fin : dimensions réelles de la carte, tableau des contrôles, liste de ce qui a été
   répercuté et de ce qui reste à ma charge (JLCDFM sur le nouveau zip, devis PCB Maroc
   maintenant qu'il n'y a plus de fentes, achat et inspection d'un module relais).
3. Tout écart par rapport à ce prompt, dit clairement, avec la raison.
