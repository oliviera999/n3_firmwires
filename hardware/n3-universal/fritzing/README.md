# Vues de câblage Fritzing — n3-universal rev 0.3.0, une par profil

| Profil | Sketch | Aperçu |
|---|---|---|
| msp1 — station météo + tracker (18650 + solaire) | `n3-universal-msp1.fzz` | `n3-universal-msp1-apercu.png` |
| n3pp — serre (chargeur 5 V) | `n3-universal-n3pp.fzz` | `n3-universal-n3pp-apercu.png` |
| ffp5cs — aquaponie 230 V (bloc 5 V externe, modules relais) | `n3-universal-ffp5cs-230v.fzz` | `n3-universal-ffp5cs-230v-apercu.png` |
| ffp5cs — aquaponie 12 V (AGM + PWM solaire) | `n3-universal-ffp5cs-12v.fzz` | `n3-universal-ffp5cs-12v-apercu.png` |

Ouvrir un `.fzz` dans **Fritzing ≥ 1.0** (Fichier → Ouvrir), vue **Platine d'essai**. Les pièces
sont embarquées dans le fichier : rien à installer. Les vues Schéma et PCB sont vides par
construction — **le PCB de référence reste le projet KiCad** (`../kicad/`).

## Ce que montre chaque vue

- **La carte à l'échelle** (278 × 91 mm), dessinée depuis le PCB routé : chaque bornier,
  support et cavalier à sa vraie place. Composants **posés** en couleur, **non posés** sur ce
  profil en gris pointillé avec `×`, cavaliers posés en bleu avec leur position (`JP12 2-3`),
  DevKit enfiché sur ses supports. Rev 0.3 : plus de zone 230 V sur la carte.
- **Les périphériques et sources d'alimentation** du profil (boîtes étiquetées, broches
  nommées), reliés aux bons borniers par des fils de couleur : rouge +5 V, orange 3,3 V,
  noir GND, rouge sombre bus 12 V, brun phase L / bleu neutre N en 230 V, autres couleurs =
  signaux. Rev 0.3 : les **modules relais** sont des boîtes au-dessus de la carte, reliées par
  trois fils (S / +5V / GND) aux borniers J39-J44 ; la phase ou le bus 12 V arrive sur leurs COM,
  les charges sur leurs NO — rien de tout cela ne touche la carte.
- **Une légende** : chaîne d'alimentation, cavaliers, options non dessinées (BME280, INA226
  supplémentaires, microSD), règles de sécurité.

Les résistances, diodes, condensateurs et transistors sont **sur la carte** : aucun composant
discret n'est à câbler hors carte, sauf les diodes de roue libre aux bornes des pompes DC. Les
modules relais portent leur propre transistor, leur diode et leur LED.

## Régénérer / vérifier

```bash
cd hardware/n3-universal
python3 fritzing/generate_fritzing.py            # 4 .fzz + cablage.csv
python3 fritzing/generate_fritzing.py --check    # CI : échoue si les vues ne suivent plus la carte
python3 fritzing/generate_fritzing.py --render   # + aperçus PNG (nécessite fritzing, xvfb, rsvg-convert)
```

Sources : `generator/generate.py` (composants, nets), `kicad/n3-universal.kicad_pcb` (positions,
lu par `kicad_pcb_geom.py` sans KiCad), `achats/inventaire.py` (pose par profil `POP`,
périphériques `OFF`, cavaliers `JUMPER_STATE`). Le câblage externe est décrit dans `WIRING`
(ce fichier) ; `cablage.csv` en est la photographie versionnée, comparée en CI.

**Règle de propagation** (`CLAUDE.md`) : un composant, un bornier, un cavalier ou un
périphérique qui change → régénérer ces vues dans le même commit (la CI le rappelle).

## Limites

- Pièces « boîtes » génériques (pas de photo de module) : l'ordre des broches des modules du
  commerce (OLED, DS3231, INA226, BME280) varie d'un fabricant à l'autre — **vérifier sur le
  module** avant d'enficher sur un support.
- Les fils sont routés automatiquement (couloirs orthogonaux) : lisibles, pas optimaux.
- Validation : chaque `.fzz` est chargé et exporté par Fritzing 1.0 en mode sans affichage
  (`--render`) ; aucune pièce embarquée ne manque. Pas de vérification électrique au-delà des
  gardes du dépôt (`coverage_errors` : chaque périphérique de la liste d'achats est dessiné,
  chaque fil arrive sur un connecteur réellement posé sur le profil).
