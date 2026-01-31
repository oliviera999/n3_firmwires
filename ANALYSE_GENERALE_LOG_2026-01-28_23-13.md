# Analyse générale du log – 2026-01-28 23:13

**Fichier** : `monitor_10min_wroom_test_2026-01-28_23-13-24.log`  
**Période** : 23:16:46 – ~23:26 (≈10 min), 1 boot (POWERON_RESET).

---

## 1. Points de vigilance

### 1.1 Mémoire
- **minHeap NVS** : 9536 bytes (historique) – à un moment le heap est descendu très bas ; à surveiller en prod.
- **Après setup** : Free 87 KB, Largest Free Block 61 KB – suffisant pour TLS avec seuils 40 KB / 32 KB, mais la fragmentation fait parfois tomber le plus grand bloc sous 32 KB → 17 reports « Plus grand bloc insuffisant » pendant la session.

### 1.2 Contention TLS (SMTP / HTTPS)
- **Timeout acquisition mutex (10000 ms)** : plusieurs fois ; la tâche mail et la tâche réseau se disputent le mutex TLS. Quand une tient le mutex longtemps (SMTP lent ou en échec), l’autre attend et peut timeouter.
- **Conséquence** : retards ou reports de requêtes HTTPS ou d’envois mail ; pas de crash, mais dégradation de réactivité.

### 1.3 Capteurs
- **DHT22 (air)** : « Capteur non détecté lors de l’initialisation » → DHT22 désactivé pour la session. À confirmer : câblage / capteur absent / environnement test.
- **DS18B20 (eau)** : quelques « Échec de lecture 1/10 » puis « Succès de lecture - reset compteur » – défaillances ponctuelles, fallback NVS OK.
- **Ultrasons** : des « Lecture réactive X timeout » et « Niveau aquarium invalide » apparaissent ; à croiser avec le hardware (portée, obstacles).

### 1.4 WiFi / light sleep
- **STA_DISCONNECTED** (Reason: 8 ASSOC_LEAVE) au moment du light sleep, puis reconnexion. Comportement attendu (économie d’énergie), mais à garder en tête pour les fenêtres où le réseau est indisponible.

---

## 2. Dysfonctionnements

### 2.1 Envoi mail (SMTP)
- **Aucun mail envoyé avec succès** pendant la session.
- **Premier envoi** : Gmail répond **550 5.4.5 Daily user sending limit exceeded** → quota quotidien du compte Gmail dépassé.
- **Ensuite** : « Reconnexion SMTP échouée - code: 550 » ou « code: 0 » à répétition (22+ occurrences) → tous les envois échouent.
- **Queue pleine** : 17+ « Queue pleine, alerte ignorée » (2 slots, pics au boot). Beaucoup d’alertes perdues côté queue.

**Conclusion** : dysfonctionnement majeur côté envoi mail : quota Gmail + queue trop petite pour les pics.

### 2.2 Servos / LEDC (boot)
- **Invalid pin: 255** et **No free timers available for freq=50, resolution=10** au démarrage : la lib servo tente d’attacher des canaux avec un pin 255 (non initialisé) avant les vrais pins 12/13. Au final les servos 12 et 13 sont bien attachés (Pin 12 / Pin 13 success), mais les erreurs encombrent les logs et peuvent indiquer un ordre d’init ou des valeurs par défaut à corriger.

**Conclusion** : dysfonctionnement mineur (init servo / LEDC), à traiter pour des logs propres et un démarrage sans erreur.

### 2.3 Fetch config au boot
- **Boot tryFetchConfigFromServer: ECHEC** : au démarrage, le plus grand bloc libre est < 32 KB (fragmentation) → requête GET HTTPS reportée, pas de config distante au boot.
- **Fetch distant fallback: KO** (3 occurrences) : quand le distant n’est pas disponible ou que la requête est reportée, le fallback local est utilisé (comportement offline-first attendu).

**Conclusion** : pas de config distante au premier boot à cause de la fragmentation ; le système reste opérationnel en local.

---

## 3. Faits notables (positifs ou neutres)

### 3.1 HTTPS / serveur distant
- **Au moins 3 requêtes HTTPS réussies** (code=200, 46 bytes) pendant la session – les seuils 40 KB / 32 KB permettent bien des échanges quand le plus grand bloc est suffisant.
- **Aucun refus « heap insuffisant »** (seuil 62 KB supprimé) : les refus sont uniquement dus à la garde « plus grand bloc < 32 KB », cohérent avec le correctif fragmentation.

### 3.2 Reboots
- **Un seul boot** dans le log (POWERON_RESET) ; **aucun reboot** pendant les ~10 min. Comportement stable.
- **reboot #6** = compteur NVS (6ᵉ boot cumulé), pas 6 reboots dans la session.

### 3.3 Mémoire au démarrage
- **Before setup** : 188 KB free, 110 KB largest block.
- **After setup** : 87 KB free, 61 KB largest block – réduction attendue après init (WiFi, OTA, capteurs, web, etc.).

### 3.4 Watchdog / tâches
- Pas de panic, pas de reboot WDT dans le log. Watchdog configuré (300 s) ; les timeouts mutex TLS sont gérés (abandon puis retry plus tard).

### 3.5 Offline-first
- En absence de config distante ou en cas de report HTTPS, le système continue avec la config / le fallback local. Pas de blocage lié au réseau.

---

## 4. Synthèse

| Domaine            | État        | Commentaire |
|--------------------|------------|-------------|
| HTTPS / distant   | Partiel    | Succès quand bloc ≥ 32 KB ; reports si fragmentation. |
| Mail SMTP         | Défaillant | Quota Gmail + queue pleine ; 0 envoi réussi. |
| Reboots           | OK         | Aucun reboot en session. |
| Mémoire           | Vigilance  | minHeap historique 9536 B ; fragmentation encore présente. |
| Servos / LEDC     | Mineur     | Erreurs init (pin 255, timers) puis attache OK. |
| Capteurs          | Vigilance  | DHT22 absent ; WaterTemp échecs ponctuels ; ultrason timeouts. |
| TLS mutex         | Vigilance  | Timeouts mutex (SMTP vs HTTPS) → à surveiller. |

**Recommandations prioritaires**  
1. **Mail** : Réduire/regrouper les envois au boot ; augmenter la queue (ex. 3–4 slots) ; côté compte : respecter le quota Gmail ou utiliser un relais dédié.  
2. **Servos** : Corriger l’init (éviter pin 255, ordre d’attach ou valeurs par défaut) pour supprimer les erreurs LEDC au boot.  
3. **Fragmentation** : Les correctifs (seuils 40/32 KB, bloc réservé, gardes) sont en place ; continuer à surveiller minHeap et « Plus grand bloc insuffisant » en conditions réelles.
