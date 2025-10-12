<!-- a7b1f7e7-c934-4799-8534-8ed72f58d67f 618f567b-2b01-4855-b666-90961a15b628 -->
# Plan: Compléter Phase 2 à 100% - 6% Restant

## État Actuel (94%)

**automatism.cpp**: 2405 lignes (depuis 3421, -1016 lignes)

**Modules**:

- ✅ Persistence: 100%
- ✅ Actuators: 100%
- ✅ Feeding: 100%
- ⏳ Network: 70% (pollRemoteState, handleResetCommand, parseRemoteConfig, handleRemoteFeedingCommands, handleRemoteActuators-simplifié)
- ⏸️ Sleep: 85%

## Restant 6% - 4 Actions

### 1. Compléter handleRemoteActuators() - GPIO 0-116

**Actuellement**: Version simplifiée (light seulement, 46 lignes)
**Objectif**: Version complète avec TOUS les GPIO

**Code à migrer** depuis l'ancienne version (fichiers .backup):

- GPIO 0-39 physiques: POMPE_AQUA, POMPE_RESERV, RADIATEURS, LUMIERE
- IDs spéciaux 100-116: email, thresholds, feeding, reset, wakeup
- Logique WakeUp complexe (3 occurrences: clé "WakeUp", clé "115", GPIO 115)
- Protection _wakeupProtectionEnd
- Helpers isTrue/isFalse déjà implémentés

**Fichiers**:

- `src/automatism/automatism_network.cpp` - handleRemoteActuators()
- Code source: lignes 1516-1766 de l'ancien automatism.cpp (voir .backup)

**Durée estimée**: 3-4 heures

### 2. Incrémenter VERSION

Mettre à jour `include/project_config.h`:

- VERSION actuelle: v11.04
- VERSION cible: v11.05 (+0.01)

### 3. Compiler + Flash + Monitor

- Compiler wroom-prod
- Flasher ESP32 (firmware + SPIFFS)
- Monitor 90 secondes
- Analyser logs

**Durée estimée**: 30 minutes

### 4. Documentation Finale + Commit

- Créer PHASE_2_100_PERCENT_COMPLETE.md
- Statistiques finales
- Gains totaux
- Commit + push

**Durée estimée**: 30 minutes

## Total Estimation

**4-5 heures** pour atteindre 100%

## Stratégie

1. Priorité: Compléter handleRemoteActuators() avec TOUT le code GPIO
2. Tester compilation après chaque étape
3. Incrémenter version
4. Flash + monitor complet
5. Documentation finale

## Gains Attendus

- handleRemoteActuators(): 46L → ~280L (complet)
- Network module: 70% → 100%
- Phase 2: 94% → 100%
- automatism.cpp: potentiellement ~2100 lignes finales

## Notes Importantes

- Le code GPIO 0-116 est complexe et critique
- WakeUp/ForceWakeup nécessite protection _wakeupProtectionEnd
- Gestion manuelle POMPE_RESERV avec _manualTankOverride
- Logs détaillés pour debug

On y va ! 🚀

### To-dos

- [ ] Compléter handleRemoteActuators() avec GPIO 0-116 et IDs spéciaux
- [ ] Incrémenter VERSION v11.04 → v11.05
- [ ] Compiler wroom-prod + Flash ESP32
- [ ] Monitor 90s + Analyser logs
- [ ] Créer PHASE_2_100_COMPLETE.md
- [ ] Commit + push Phase 2 @ 100%