# Documentation FFP5CS

Documentation technique du projet ESP32 Aquaponie Controller.

## Structure du projet

Le code source est organisé ainsi :
- `src/` - Code source C++ (implémentation)
- `include/` - Headers C++ (interfaces)
- `data/` - Fichiers web (HTML, CSS, JS)
- `test/` - Tests unitaires

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

Voir `.cursorrules` à la racine du projet pour les règles de développement.

**Principes clés :**
1. Offline-first : le système fonctionne sans réseau
2. Simplicité : éviter la sur-ingénierie
3. Robustesse : ne jamais crasher
4. Autonomie : configuration locale prioritaire
