# Pertinence des pistes de stabilisation wroom-prod — 2026-03-11

**Contexte** : Plan multi-pistes pour résoudre les crashs Stack canary netTask sur wroom-prod. Piste 1 (stack 16 KB) a été appliquée et validée (0 crash en 5 min). Piste 7 (wroom-prod-debug) a été créée.

---

## Piste 1 — Augmenter stack netTask à 16 KB

**Statut** : ✅ Appliquée et validée

**Effet** : Élimination des 19 crashs en 5 min. Le système est stable. Piste principale et suffisante dans le cas actuel.

---

## Piste 7 — Env wroom-prod-debug (Serial + endpoints dev)

**Statut** : ✅ Implémentée

**Créé** : Environnement `wroom-prod-debug` dans `platformio.ini` :
- Hérite de wroom-prod (même stack 16 KB, mêmes optimisations)
- `ENABLE_SERIAL_MONITOR=1` — logs boot, WiFi, POST, OTA visibles
- `USE_TEST_ENDPOINTS` — POST vers `/ffp3/post-data-test`, OTA canal test

**Usage** :
```powershell
pio run -e wroom-prod-debug -t upload --upload-port COM8
.\monitor_5min.ps1 -Port COM8 -Environment wroom-prod-debug -DurationSeconds 300
.\analyze_log.ps1 -logFile monitor_5min_*.log
```

**Pertinence** : Pour diagnostic terrain (WiFi, ordre des opérations, POST/GET effectifs) sans déployer wroom-test. À utiliser avant validation déploiement ou pour reproduire un scénario distant.

---

## Piste 2 — Désactiver OTA au boot pour wroom-prod

**Pertinence** : ❌ Non pertinente dans l’état actuel

La Piste 1 a corrigé le crash. Désactiver l’OTA boot réduirait la fonctionnalité sans gain de stabilité. À réserver si le stack 16 KB ne suffisait pas ou si des contraintes DRAM/flash empêchaient cette augmentation.

---

## Piste 3 — Réduire les buffers sur la stack de netTask

**Pertinence** : ⚠️ Faible, à garder en réserve

Externaliser `StaticJsonDocument` ou `payload` libérerait ~1–2 KB de stack. Avec 16 KB, la marge est confortable. Intéressant seulement si :
- on visait à revenir à 12 KB (économie DRAM),
- on observait à nouveau des HWM proches de la limite.

---

## Piste 4 — Réduire MBEDTLS_SSL_MAX_CONTENT_LEN

**Pertinence** : ⚠️ Faible, à garder en réserve

Réduire à 4–8 KB dans `sdkconfig_wroom_wdt.txt` réduirait la consommation stack de mbedTLS. La stack à 16 KB rend cette optimisation secondaire. Risque de troncature sur des réponses HTTP plus grandes (metadata OTA, config).

---

## Piste 5 — Augmenter le délai avant premier TLS

**Pertinence** : ⚠️ Optionnelle

`FIRST_TLS_DELAY_MS = 2000` suffit avec la pile à 16 KB et le heap qui se stabilise. Passer à 3–4 s pourrait aider en cas de boot très chargé ou heap très bas, au prix d’un délai de config distante.

---

## Piste 6 — Supprimer les erreurs coredump au panic

**Pertinence** : ⚠️ Cosmétique

Les messages « Core dump flash config is corrupted » viennent du panic handler lisant une zone flash incorrecte ( LittleFS ). `CONFIG_ESP_COREDUMP_ENABLE_TO_NONE` est déjà dans `sdkconfig_wroom_wdt.txt`. Si les messages persistent, un composant (ex. esp_insights) force peut‑être une vérification. Peu prioritaire tant que les crashs sont résolus.

---

## Piste 8 — Inverser l’ordre OTA / config au boot

**Pertinence** : ❌ Non pertinente

L’OTA en premier est voulu (faisabilité de mise à jour). La cause du crash (stack) est corrigée. Changer l’ordre n’apporte pas de bénéfice.

---

## Synthèse

| Piste | Action | Priorité |
|-------|--------|----------|
| 1 | Appliquée | — |
| 7 | Appliquée | Utiliser pour diagnostic |
| 2, 8 | Non pertinentes | — |
| 3, 4, 5 | En réserve | Si contraintes resurgissent |
| 6 | Cosmétique | Optionnelle |

**Conclusion** : La Piste 1 est suffisante. La Piste 7 est un outil de diagnostic utile. Les autres pistes sont à conserver comme leviers de secours si la situation évolue.
