# Documentation FFP5CS

Documentation technique du projet ESP32 Aquaponie Controller.

## Structure du projet

- `src/` — Code source C++ (implémentation)
- `include/` — Headers C++ (interfaces)
- `data/` — Fichiers web (HTML, CSS, JS)
- `test/` — Tests unitaires (framework Unity, env `native`). Exécution : `pio test -e native` ou `.\scripts\test_unit_all.ps1`. Seuls `test_nvs` et `test_config` sont exécutés (voir `platformio.ini` env `native`, `test_filter`).

## Structure de la documentation

```
docs/
├── README.md           # Ce fichier
├── technical/          # Références techniques
│   ├── VARIABLE_NAMING.md        # Contrat nommage (NVS, API, serveur, firmware)
│   └── SEUILS_SERVEUR_ESP32.md   # Seuils ESP32 vs serveur PHP
├── reports/            # Rapports et analyses
│   ├── analysis/       # Qualité, conformité, NVS
│   ├── corrections/    # Résumé des corrections appliquées
│   └── monitoring/     # Origine des problèmes (DHT22, heap, etc.)
└── references          # Référence rapide emails (32 types, conditions d'envoi)
```

### Liens utiles

- **[Convention nommage / contrat](technical/VARIABLE_NAMING.md)** — NVS, API locale, serveur distant, firmware (source : `include/gpio_mapping.h`, `include/nvs_keys.h`)
- **[Seuils ESP32 / serveur](technical/SEUILS_SERVEUR_ESP32.md)** — Différences volontaires (température, humidité, etc.)
- **`references`** — Liste des 32 types d’emails, quotas, optimisations
- **Rapports** :
  - [Origine problèmes critiques](reports/monitoring/reports/ANALYSE_ORIGINE_PROBLEMES_CRITIQUES.md) — DHT22, watchdog, mémoire, boucles de reboot
  - [Résumé corrections](reports/corrections/RESUME_CORRECTIONS_APPLIQUEES.md) — Incohérences corrigées (2026-01-21)
  - [Conformité .cursorrules](reports/analysis/compliance/RAPPORT_CONFORMITE_CURSORRULES_POST_CORRECTIONS.md) — État actuel (~92 %)
  - [Système NVS](reports/analysis/code-quality/RAPPORT_ANALYSE_SYSTEME_NVS.md) — Architecture, namespaces, usage
  - [Code mort](reports/analysis/code-quality/RAPPORT_VERIFICATION_CODE_MORT_FINALE_2026-01-25.md) — Vérification post-nettoyage
  - [Optimisations](reports/analysis/code-quality/RAPPORT_OPTIMISATIONS_PERFORMANCE.md) — NVS, String, simplicité

## Configuration

Toute la configuration est centralisée dans `include/config.h`.

## Compilation

```bash
# Environnement test
pio run -e wroom-test

# Environnement production
pio run -e wroom-prod

# Flash
pio run -e wroom-test -t upload

# Monitor série
pio device monitor
```

## Principes de développement

Voir [.cursorrules](../.cursorrules) à la racine du projet.

**Principes clés :**
1. Offline-first : le système fonctionne sans réseau
2. Simplicité : éviter la sur-ingénierie
3. Robustesse : ne jamais crasher
4. Autonomie : configuration locale prioritaire
