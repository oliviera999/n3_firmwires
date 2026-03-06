# Audit général du firmware FFP5CS – après analyse log monitoring (wroom-test, COM4)

**Date :** 2026-02-15  
**Contexte :** Analyse post-mortem d’une session de monitoring (log 5 min du 15/02 + rapports diagnostic récents ; pas de log 30 min récent exploitable dans le dépôt).  
**Périmètre :** Règles cœur (offline-first, robustesse, mémoire/watchdog), stabilité, mails, réseau, capteurs.

---

## 1. Contexte et sources analysées

- **Rapport diagnostic complet :** `rapport_diagnostic_complet_2026-02-15_11-43-38.md` (log `monitor_5min_2026-02-15_11-38-05.log`, COM4, 300 s).
- **Rapport d’audit stabilité :** `RAPPORT_AUDIT_STABILITE.txt` (2026-02-13).
- **Analyse mails/heap/POST :** `docs/technical/ANALYSE_MONITOR_2026-02-15_MAILS_HEAP_POST.md`.
- **Documentation WDT :** `docs/technical/WROOM_TASK_WDT_30S.md`.
- **Log 30 min :** Le fichier `monitor_30min_2026-02-07_11-47-16.log` est quasi vide (monitoring terminé immédiatement). Pour un audit basé sur une vraie session 30 min sur COM4, lancer :
  - `.\erase_flash_fs_monitor_5min_analyze.ps1 -DurationMinutes 30 -Port COM4`
  - puis relancer `.\generate_diagnostic_report.ps1` sur le log généré (`monitor_30min_*.log`).

---

## 2. Synthèse globale

| Critère              | Statut   | Commentaire |
|----------------------|----------|-------------|
| Crash / reboot       | OK       | Aucun crash ni reboot dans le log 5 min. |
| Watchdog (TWDT)      | OK       | 1 événement « timeout » dans le log : **ultrason réactif** (lecture 4), pas un WDT CPU. |
| Réseau / serveur     | Stable   | GET ~12 s, POST réussis ; 1 timeout réseau signalé. |
| POST durée           | WARN     | Durée max 19474 ms > limite 18000 ms (voir § 4.1). |
| Mails (alertes)      | WARN     | 22 événements « Chauffage OFF » sans mail envoyé (voir § 4.2). |
| Ultrasonic           | Mineur   | 1 timeout lecture réactive (total: 1) sur 5 min. |
| Mémoire / heap       | OK       | Heap libre ~91–95 KB ; pas de chute ni fuite visible. |
| NVS                  | OK       | Aucune erreur NVS. |

**Verdict :** Système **stable** (pas de crash, pas de reset). Points à surveiller : **durée POST** et **non-envoi des mails** (déjà documentés et en partie adressés par la config 31K).

---

## 3. Points conformes (rappel audit stabilité)

- **Watchdog :** Feed WDT dans les boucles longues (netTask, sensorTask, automationTask, power NTP, etc.) ; TWDT 30 s (WROOM) documenté et reconfig runtime dans `app.cpp`.
- **Validation capteurs :** `isnan()`, plages MIN/MAX, valeurs par défaut (SensorConfig::DefaultValues / Fallback).
- **NVS :** Écriture uniquement si valeur changée ; load* avec fallback.
- **Réseau :** Timeouts centralisés (`config.h`), vérification WiFi avant HTTP, pas de boucle sans limite.
- **Défensif web :** Vérifications `getParam()` (null check) dans `web_server.cpp` et `web_routes_status.cpp`.
- **Offline-first :** Pas de blocage métier sur « WiFi connecté » sans timeout ; fallbacks NVS.

---

## 4. Pistes de bug / points d’attention

### 4.1 POST : durées au-dessus de 18 s

**Symptôme :** Rapport diagnostic 15/02 : « POST: Durées trop longues (max: 19474 ms, limite: 18000 ms) ».

**Constat :**  
- `HTTP_POST_TIMEOUT_MS = 18000` dans `include/config.h`.  
- Les POST **réussissent** (code 200) mais dépassent parfois 18 s ; ils restent sous le timeout RPC (26 s), donc pas de coupure côté firmware.  
- La durée vient surtout du **serveur / réseau** (latence, traitement PHP/BDD), pas d’un transfert énorme.

**Impact :** netTask reste occupée plus longtemps → intervalle effectif entre deux POST peut dépasser 30 s ; en cas de latence croissante, risque de dépasser 26 s et timeout.

**Recommandations :**  
- Conserver la limite à 18 s comme objectif ; accepter temporairement des réponses jusqu’à 26 s tant que le serveur est lent.  
- Optionnel : log explicite quand une requête dépasse 18 s (ex. « POST slow: XXX ms ») pour faciliter le suivi.  
- Côté infra : optimiser temps de réponse du serveur (iot.olution.info) si possible.

---

### 4.2 Mails « Chauffage OFF » non envoyés (22 attendus, 0 envoyés)

**Symptôme :** Rapport diagnostic : 22 événements « Chauffage OFF » déclencheurs, 0 mails ajoutés à la queue / envoyés.

**Cause identifiée (doc existante) :**  
- `docs/technical/ANALYSE_MONITOR_2026-02-15_MAILS_HEAP_POST.md` : le firmware exige un **bloc contigu** ≥ seuil (anciennement 32 KB) pour tenter l’envoi SMTP/TLS.  
- En session, le plus grand bloc observé restait ~31,7 KB → condition jamais remplie → « skip heap » et aucun envoi.

**Évolution config :**  
- Pour WROOM, `config.h` impose désormais `MIN_HEAP_BLOCK_FOR_MAIL_TLS = 31000` (31 KB), donc un bloc de 31 732 octets **devrait** permettre l’envoi.  
- Si le log du 15/02 11:38 a été produit avec un build encore en 32 KB, le comportement « 0 mails » est cohérent.  
- À vérifier sur un **nouveau** run (5 min ou 30 min) avec le build actuel : si le bloc dépasse 31 KB, les mails devraient être tentés.

**Recommandations :**  
- Relancer une session (5 ou 30 min) avec le firmware actuel et vérifier dans le log :  
  - présence de « [Mail] … envoi » ou « skip heap (bloc=… » ;  
  - si bloc ≥ 31K et mails toujours absents, chercher une autre cause (queue non alimentée, condition d’envoi désactivée, etc.).  
- Ne pas baisser le seuil sous 31 KB sans tests (risque d’échec TLS / abort).

---

### 4.3 Ultrasonic : 1 timeout lecture réactive

**Symptôme :** Une occurrence dans le log 5 min :  
`[Ultrasonic] Lecture réactive 4 timeout (total: 1)` (11:41:36).

**Constat :**  
- Le code (`src/sensors.cpp`) fait du rate-limiting sur les logs timeout (3 premiers, puis tous les 10, etc.) et une synthèse si > 100 timeouts.  
- 1 timeout sur des centaines de lectures en 5 min est **acceptable** (reflet, bulle, angle).  
- Comportement : la médiane est calculée sur les lectures valides ; une lecture en échec est ignorée, pas de crash.

**Recommandation :** Aucune action requise sauf si les timeouts deviennent fréquents (> 10 % des cycles) ou si un capteur est clairement absent (alors le résumé 60 s s’affichera après 100 timeouts).

---

## 5. Recommandations pour une session 30 min (COM4, wroom-test)

1. **Générer un log 30 min exploitable**  
   - `.\erase_flash_fs_monitor_5min_analyze.ps1 -DurationMinutes 30 -Port COM4`  
   - Vérifier que le fichier `monitor_30min_*.log` contient bien ~30 min de traces (et non une fin immédiate comme le log du 07/02).

2. **Relancer le diagnostic complet**  
   - `.\generate_diagnostic_report.ps1 -LogFile "monitor_30min_2026-02-15_*.log"`  
   - Vérifier : crashes, WDT, heap, POST (durées, timeouts), mails (queue, envois), NVS.

3. **Surveiller dans le log 30 min**  
   - Heap libre et plus grand bloc (messages mail « bloc=… »).  
   - Fréquence des timeouts Ultrasonic (et éventuel message « Résumé: X timeouts »).  
   - Intervalles réels entre POST et GET (cohérence avec config 30 s / 12 s).  
   - Toute ligne ERROR, ASSERT, panic, Guru, WDT (Task watchdog).

---

## 6. Prochaines étapes proposées

| Priorité | Action |
|----------|--------|
| P1       | Lancer une session **30 min** sur COM4 avec le script ci-dessus et analyser le rapport généré. |
| P2       | Confirmer que le build utilisé a bien `MIN_HEAP_BLOCK_FOR_MAIL_TLS = 31000` pour WROOM et vérifier si les mails partent après 31K. |
| P2       | Si POST > 18 s reste fréquent : ajouter un log « POST slow » au-delà de 18 s (optionnel). |
| P3       | Garder la surveillance heap/HWM (TaskMonitor) sur runs longs (24 h) comme indiqué dans RAPPORT_AUDIT_STABILITE. |

---

*Audit produit à partir des artefacts disponibles (rapports diagnostic, analyse technique, RAPPORT_AUDIT_STABILITE). Pour un audit basé sur une vraie trace 30 min, régénérer les artefacts après une session 30 min sur COM4.*
