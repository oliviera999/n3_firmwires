---
name: firmware-native-tests
description: Run the native Unity test suites of the n3_firmwires repo (shared/ libraries and ffp5cs), the same ones the Firmware CI runs. Use before pushing changes to shared code or ffp5cs, when asked to "run the firmware tests / unit tests", or to reproduce a CI "Tests natifs (Unity)" failure.
---

# Tests natifs Unity (équivalent CI)

Les firmwares sont testés via des suites **Unity** compilées en natif (plateforme `native`,
sans matériel). C'est l'étape `Tests natifs (Unity)` de `.github/workflows/firmware-ci.yml`.

## Lancer une suite

```bash
# Librairies partagées
cd shared/tests_native
pio test -c platformio-native.ini -e native -f test_analog
pio test -c platformio-native.ini -e native -f test_hmac

# ffp5cs (nombreuses suites : test_nvs, test_config, test_ota_select, test_feeding_slots, …)
cd ffp5cs
pio test -c platformio-native.ini -e native -f test_nvs
```

`-f <filtre>` sélectionne la suite (nom du dossier de test). Pour reproduire un échec CI,
relancer **exactement** la suite nommée dans le log (`::group::<suite>`).

## Bonnes pratiques

- Après modification d'une lib `shared/` (ex. `n3_hmac`, `n3_data`, `n3_common`), lancer au moins
  les suites natives concernées **avant de pousser** — la CI les exécute toutes, une par une.
- Une logique testable doit rester découplée du matériel (pas d'appel Arduino direct) pour être
  couverte en natif ; suivre le découpage existant des modules (`*_automation`, `*_sensors`…).
- Ces tests ne nécessitent **pas** de carte ni de secrets.
