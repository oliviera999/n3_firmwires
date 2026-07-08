# Carte porteuse ffp5cs `wroom-prod` — VARIANTE 230 V (KiCad)

Variante **secteur** de la carte porteuse [`hardware/ffp5cs-wroom-prod/`](../ffp5cs-wroom-prod/)
(qui reste la référence 12/24 V). Même firmware, même brochage (`pinmap.json` identique,
vérifié contre `ffp5cs/include/pins.h`), mais les 4 relais commutent des **charges
230 V ≤ 10 A** dans une **zone secteur physiquement isolée**, et la connectique est
pensée « zéro soudure » : tout se branche en borniers à vis ou en Dupont.

```
kicad/ffp5cs-wroom-prod-230v.*   Projet KiCad 8 (schéma, PCB ROUTÉ, règles DRC .kicad_dru)
exports/gerbers-*.zip            Fabrication (Gerbers + perçages) — 166 x 110 mm, 2 couches
BOM.csv                          Nomenclature
generator/generate.py            Régénère schéma + PCB (non routé) + BOM depuis pinmap.json
generator/route_230v.py          Pipeline de routage sécurisé (voir ci-dessous)
tools/check_pinmap_vs_firmware.py  Garde anti-dérive code <-> plan
```

## Sécurité 230 V — comment c'est construit

- **Zone secteur dédiée** en haut de carte : seuls les contacts relais, leurs pistes
  (2,5 mm) et les borniers y vivent. Les plans de masse et le routage logique en sont
  **exclus par construction** (zones interdites pendant l'autoroutage, pours GND
  arrêtés à la frontière).
- **Fentes d'isolement fraisées** entre les 4 canaux, à la frontière droite de la
  zone, et de part et d'autre du pad COM de chaque relais (le point faible du
  boîtier SRD, où COM passe à ~3,5 mm de la bobine).
- **Règle DRC dédiée** (`.kicad_dru`) : tout cuivre de classe `Mains` doit rester à
  **≥ 3 mm** de tout cuivre non-Mains — vérifiée par `kicad-cli pcb drc` **et** par
  un contrôle géométrique indépendant dans `route_230v.py` (double ceinture).
- Routage secteur **déterministe, tracé en dur** (pas d'autorouteur dans la zone) :
  lignes droites bornier ↔ contacts, COM passant entre les broches bobine.
- Sérigraphie : « ZONE 230V — DANGER », « NC COM NO » sous chaque bornier.
- État validé : **DRC 0 violation, 0 pad non connecté, contrôle 3 mm : 0 écart**
  (KiCad 8.0.9).

### Ce qui reste À TA CHARGE côté installation (non négociable)

1. **Protection amont** : chaque circuit 230 V commuté doit être protégé par
   fusible/disjoncteur + différentiel 30 mA. Il n'y a **pas de fusible sur la carte**
   (choix : la protection appartient au tableau/à la ligne, comme sur les modules
   relais du commerce).
2. **Boîtier obligatoire**, borniers 230 V inaccessibles au doigt, presse-étoupes.
3. Charges inductives (pompe 230 V) : prévoir un **snubber RC ou une varistance côté
   charge** pour préserver les contacts.
4. Relais SRD-05 = isolation bobine/contact d'un relais grand public (type modules
   Arduino). Pour un usage durable/humide, un relais à meilleure séparation
   (ex. Hongfa HF115F) est un upgrade raisonnable — footprint à adapter.
5. Ne jamais intervenir carte sous tension (la sérigraphie te le rappelle).

## Les trois demandes spécifiques de cette variante

- **Zéro soudure à l'usage** : charges sur borniers à vis 5,08 mm ; capteurs sur JST-XH ;
  servos, OLED, I2C sur headers ; DS18B20 et LDR sur borniers ; alim sur jack **ou** bornier.
- **TOUS les GPIO libres accessibles** sur le header J17 (1×14, Dupont) :
  `3V3, GND, EN, RX0, TX0, IO5, IO14, IO17, IO23, IO25, IO32, IO35, IO36, IO39`
  (IO35/36/39 = entrées seules ; RX0/TX0 : les laisser libres pendant un flash USB).
- **Distribution d'alim supplémentaire** : bornier 5 V/GND (J18), bornier 3V3/GND (J19),
  et rail header 6 points J20 (2×5V, 2×GND, 2×3V3) pour brancher des modules en Dupont.

## Régénérer / rerouter / vérifier

```bash
cd hardware/ffp5cs-wroom-prod-230v
python3 generator/generate.py          # schéma + PCB placé (NON routé) + BOM
python3 generator/route_230v.py        # autoroute la logique, trace le secteur en dur,
                                       # coud les masses, corrige les reliefs, contrôle 3 mm
python3 tools/check_pinmap_vs_firmware.py
kicad-cli pcb drc --severity-error kicad/ffp5cs-wroom-prod-230v.kicad_pcb -o /tmp/drc.rpt
```

⚠️ `generate.py` écrase le PCB routé — enchaîner systématiquement avec `route_230v.py`.
Le routage livré dans `kicad/` est déjà fait et validé.

## Différences avec la carte 12/24 V

| | 12/24 V (`ffp5cs-wroom-prod`) | 230 V (ce dossier) |
|---|---|---|
| Zone secteur isolée + fentes | non (inutile) | **oui** |
| Pistes contacts relais | 2,0 mm | 2,5 mm, tracées à la main |
| Règle DRC 3 mm mains↔logique | — | **oui (.kicad_dru + contrôle python)** |
| GPIO libres en header | 10 broches | **14 broches (tout, dont RX0/TX0/36/39)** |
| Distribution 5V/3V3/GND | — | **2 borniers + rail 6 points** |
| Taille | 150×100 mm | 166×110 mm |

Empreintes : bibliothèque officielle KiCad 8.0.9 (CC-BY-SA 4.0 + exception d'usage),
vendorées dans `generator/footprints/`.
