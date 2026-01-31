# Rapport de Diagnostic Complet - WROOM-TEST

**Date:** 2026-01-31 11:15:41  
**Fichier log:** \$LogFile\
**Durée monitoring:** Analyse terminée

---

## Résumé Exécutif

### Statut Global

❌ **SYSTÈME INSTABLE** - 469 crash(es) détecté(s)

### Statistiques Générales

- **Total lignes loggées:** 28195
- **Taille du fichier:** 2994.81 KB
- **Warnings:** 261
- **Erreurs:** 862
- **Crashes:** 469

---

## 1. Diagnostic Échanges Serveur Distant

### Résumé

- **POST:** Statistiques non disponibles

- **GET:** 0 tentatives, 0 erreurs parsing

### Détails Complets

\\\
=== DIAGNOSTIC ÉCHANGES SERVEUR DISTANT ===
Date: 2026-01-31 11:15:34
Fichier log: C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs\monitor_wroom_test_validation_2026-01-26_11-39-09.log

=== 1. ENVOI POST DONNÉES CAPTEURS ===

Statistiques:
- Débuts POST détectés: 0
- POST réussis: 0
- POST échoués: 0
- Taux de succès: 0%

Performance:
- Durée moyenne: 0 ms
- Durée min: 0 ms
- Durée max: 0 ms
- Intervalle moyen: 0 secondes (attendu: 120s)

Endpoints:
- Utilisation endpoint test: 0
- Utilisation endpoint prod: 0

Queue persistante:
- Utilisations queue: 44
- Erreurs mutex TLS: 0

=== 2. RÉCEPTION GET COMMANDES SERVEUR ===

Statistiques:
- Tentatives GET: 0
- Parsing JSON réussis: 0
- Erreurs parsing JSON: 0
- Taux de succès parsing: 0%

Performance:
- Intervalle moyen: 0 secondes (attendu: 12s)

Endpoints:
- Utilisation endpoint test: 0
- Utilisation endpoint prod: 0

ACK:
- ACK envoyés: 0

=== 3. HEARTBEAT ===

Statistiques:
- Heartbeats détectés: 0
- Heartbeats réussis: 0
- Heartbeats échoués: 0
- Taux de succès: 0%

Performance:
- Durée moyenne: 0 ms

=== 4. GESTION ERREURS RÉSEAU ===

WiFi:
- Déconnexions: 0
- Reconnexions: 123

Erreurs:
- Erreurs TLS: 0
- Erreurs HTTP: 0
- Timeouts: 174

Queue:
- Replays queue: 0

=== 5. VÉRIFICATIONS DE COHÉRENCE AVEC LE CODE ===

❌ POST: Impossible de calculer la fréquence
❌ GET: Impossible de calculer la fréquence
❌ Endpoint: Aucun endpoint détecté

=== RÉSUMÉ ===

POST:
- ❌ Aucun POST détecté

GET:
- ❌ Aucun GET détecté

Heartbeat:
- ℹ️ Aucun heartbeat détecté

Réseau:
- ✅ WiFi stable

=== FIN DU DIAGNOSTIC ===

\\\

---

## 2. Diagnostic Échanges Mail

### Résumé

- **Mails attendus:** 43
- **Mails envoyés:** 0
- **Mails échoués:** 0

### Détails Complets

\\\
=== VÉRIFICATION DES MAILS DEPUIS LE LOG ===
Fichier: C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs\monitor_wroom_test_val
idation_2026-01-26_11-39-09.log

🔍 Analyse des événements déclencheurs...
  ✅ 43 événements déclencheurs identifiés

🔍 Analyse des traces d'envoi...
  ✅ 0 mails ajoutés à la queue
  ✅ 0 mails traités
  ⚠️ 0 mails échoués
  ⚠️ 0 erreurs de queue pleine

🔍 Corrélation des événements avec les mails...

=== RAPPORT D'ANALYSE ===

📊 RÉSUMÉ
  Mails attendus: 43
  Mails ajoutés à la queue: 0
  Mails envoyés avec succès: 0
  Mails échoués: 0
  Mails non traités: 0
  Mails manquants: 43

📧 DÉTAIL PAR TYPE
  OTA: 43 attendu(s), 43 manquant(s) ⚠️

❌ MAILS MANQUANTS (43)
  - [Ligne 631] OTA (OTA)
  - [Ligne 1283] OTA (OTA)
  - [Ligne 1935] OTA (OTA)
  - [Ligne 2562] OTA (OTA)
  - [Ligne 3187] OTA (OTA)
  - [Ligne 3812] OTA (OTA)
  - [Ligne 4464] OTA (OTA)
  - [Ligne 5088] OTA (OTA)
  - [Ligne 5741] OTA (OTA)
  - [Ligne 6395] OTA (OTA)
  - [Ligne 7050] OTA (OTA)
  - [Ligne 7704] OTA (OTA)
  - [Ligne 8355] OTA (OTA)
  - [Ligne 8982] OTA (OTA)
  - [Ligne 9638] OTA (OTA)
  - [Ligne 10264] OTA (OTA)
  - [Ligne 10923] OTA (OTA)
  - [Ligne 11578] OTA (OTA)
  - [Ligne 12202] OTA (OTA)
  - [Ligne 12860] OTA (OTA)
  - [Ligne 13512] OTA (OTA)
  - [Ligne 14166] OTA (OTA)
  - [Ligne 14822] OTA (OTA)
  - [Ligne 15476] OTA (OTA)
  - [Ligne 16129] OTA (OTA)
  - [Ligne 16756] OTA (OTA)
  - [Ligne 17413] OTA (OTA)
  - [Ligne 18067] OTA (OTA)
  - [Ligne 18723] OTA (OTA)
  - [Ligne 19373] OTA (OTA)
  - [Ligne 19998] OTA (OTA)
  - [Ligne 20652] OTA (OTA)
  - [Ligne 21278] OTA (OTA)
  - [Ligne 21903] OTA (OTA)
  - [Ligne 22558] OTA (OTA)
  - [Ligne 23212] OTA (OTA)
  - [Ligne 23837] OTA (OTA)
  - [Ligne 24488] OTA (OTA)
  - [Ligne 25142] OTA (OTA)
  - [Ligne 25794] OTA (OTA)
  - [Ligne 26449] OTA (OTA)
  - [Ligne 27103] OTA (OTA)
  - [Ligne 27755] OTA (OTA)

=== ANALYSE TERMINÉE ===

\\\

---

## 3. Analyse Exhaustive des Logs

### Résumé

- **Crashes:** 476

### Détails Complets

\\\
=== ANALYSE EXHAUSTIVE DU LOG ===
Fichier: C:\Users\olivi\Mon Drive\travail\olution\Projets\prototypage\platformIO\Projects\ffp5cs\monitor_wroom_test_val
idation_2026-01-26_11-39-09.log

📊 STATISTIQUES GÉNÉRALES
  Lignes totales: 28195
  Taille: 2994.81 KB

🔴 CRASHES ET ERREURS CRITIQUES
  Nombre de crashes: 476
    - 11:39:15.824 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:29.567 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_hasPanic = 1
    - 11:39:29.578 > [NVS] âœ… SauvegardÃ©:[37m [37mlogs::diag_panicAddr = 0
    - 11:39:29.586 > [NVS] âŒ saveString: [37mW[37mrite failed (ns: logs) (key: di[37ma[37mg_panicTask)
    - 11:39:29.592 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicCore = 0
    - [37m11:39:29.598 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_panicInfo = ESP reset reason: [37m4[37m, 
RTC reason: 12
    - 11:39:29.604 > [Diagnostics] [37m[37mðŸ’¾ Informations de panic sauve[37mg[37mardÃ©es
    - 11:39:29.651 > [Diagnostics][37m [37mðŸš€ InitialisÃ© - reboot #2, m[37mi[37mnHeap: 47780 bytes [PANIC: 
Watc[37mh[37mdog Timeout (RTC code 12)]
    - 11:39:35.928 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:49.758 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Time[37mo[37mut (RTC code 12) (Core 0)
    - 11:39:49.777 > [NVS] âœ… Sauveg[37ma[37mrdÃ©: logs::diag_hasPanic = 1
    - 11:39:49.780 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicCause = Watchdog Timeout 
([37mR[37mTC code 12)
    - 11:39:49.788 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_panicAddr = 0
    - 11:39:49.797 > [NVS] âŒ saveStrin[37mg[37m: Write failed (ns: logs) (key:[37m [37mdiag_panicTask)
    - 11:39:49.802 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicCore = [37m0[37m
    - 11:39:49.808 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:39:49.813 > [Diagnostics[37m][37m ðŸ’¾ Informations de panic sau[37mv[37megardÃ©es
    - 11:39:49.864 > [Diagnostics] ðŸš€ In[37mi[37mtialisÃ© - reboot #3, minHeap: [37m4[37m7780 bytes [PANIC: 
Watchdog Tim[37me[37mout (RTC code 12)]
    - 11:39:56.290 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:10.107 > [Diagnostics] ðŸ” Panic cap[37mt[37murÃ©: Watchdog Timeout (RTC cod[37me[37m 12) (Core 0)
    - 11:40:10.126 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_hasPanic = 1
    - 11:40:10.129 > [NVS] âœ… Sau[37mv[37megardÃ©: logs::diag_panicCause [37m=[37m Watchdog Timeout (RTC code 
12)[37m
    - [37m11:40:10.137 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicAddr = 0
    - 11:40:10.140 > [NVS] âœ… Sauve[37mg[37mardÃ©: logs::diag_panicExcv = 0[37m
    - [37m11:40:10.145 > [NVS] âŒ saveString: Write fai[37ml[37med (ns: logs) (key: diag_panicT[37ma[37msk)
    - 11:40:10.151 > [NVS] âœ… SauvegardÃ©: log[37ms[37m::diag_panicCore = 0
    - 11:40:10.153 > [NVS] âœ… [37mS[37mauvegardÃ©: logs::diag_panicInf[37mo[37m = ESP reset reason: 4, RTC 
rea[37ms[37mon: 12
    - 11:40:10.162 > [Diagnostics] ðŸ’¾ Infor[37mm[37mations de panic sauvegardÃ©es
    - 11:40:10.210 > [[37mD[37miagnostics] ðŸš€ InitialisÃ© - [37mr[37meboot #4, minHeap: 47780 bytes 
[37m[[37mPANIC: Watchdog Timeout (RTC co[37md[37me 12)]
    - 11:40:11.858 > Backtrace: 0x40091977:0x3f[37mf[37mb0330 0x400929a0:0x3ffb0360 0x4[37m0[37m091ccc:0x3ffb0390 
0x40091c7c:0x[37ma[37m5a5a5a5 |<-CORRUPTED
    - 11:40:12.409 > abort() was called at PC 0x401[37mb[37m436b on core 1
    - 11:40:12.412 > Backtrace: [37m0[37mx40083210:0x3ffafee0 0x4009071d[37m:[37m0x3ffaff00 
0x40097065:0x3ffaff2[37m0[37m 0x401b436b:0x3ffaffa0 0x401b49[37ma[37m1:0x3ffaffd0 0x401b4a2d:0x3ffb0[37m0[37m10 
0x401b4e89:0x3ffb0040 0x401b[37m5[37m3db:0x3ffb00e0 0x401b4273:0x3ff[37mb[37m01b0 0x40176f4c:0x3ffb01d0 
0x40[37m1[37m7615d:0x3ffb0200 0x400827c9:0x3[37mf[37mfb0250 0x40085bd3:0x3ffb0270 0x[37m0[37m0040021:0x3ffb0330 
|<-CORRUPTED[37m
    - 11:40:12.445 > Re-entered core dump! Ex[37mc[37meption happened during core dum[37mp[37m!
    - 11:40:14.360 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:27.369 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Time[37mo[37mut (RTC code 12) (Core 0)
    - 11:40:27.388 > [NVS] âœ… Sauveg[37ma[37mrdÃ©: logs::diag_hasPanic = 1
    - 11:40:27.391 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicCause = Watchdog Timeout 
([37mR[37mTC code 12)
    - 11:40:27.399 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_panicAddr = 0
    - 11:40:27.407 > [NVS] âŒ saveStrin[37mg[37m: Write failed (ns: logs) (key:[37m [37mdiag_panicTask)
    - 11:40:27.413 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicCore = [37m0[37m
    - 11:40:27.418 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:40:27.424 > [Diagnostics[37m][37m ðŸ’¾ Informations de panic sau[37mv[37megardÃ©es
    - 11:40:27.471 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#5, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - 11:40:33.708 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:47.115 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Time[37mo[37mut (RTC code 12) (Core 0)
    - 11:40:47.135 > [NVS] âœ… Sauveg[37ma[37mrdÃ©: logs::diag_hasPanic = 1
    - 11:40:47.137 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicCause = Watchdog Timeout 
([37mR[37mTC code 12)
    - 11:40:47.146 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_panicAddr = 0
    - 11:40:47.154 > [NVS] âŒ saveStrin[37mg[37m: Write failed (ns: logs) (key:[37m [37mdiag_panicTask)
    - 11:40:47.160 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicCore = [37m0[37m
    - 11:40:47.165 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:40:47.171 > [Diagnostics[37m][37m ðŸ’¾ Informations de panic sau[37mv[37megardÃ©es
    - 11:40:47.219 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#6, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - 11:40:53.545 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:06.566 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Time[37mo[37mut (RTC code 12) (Core 0)
    - 11:41:06.585 > [NVS] âœ… Sauveg[37ma[37mrdÃ©: logs::diag_hasPanic = 1
    - 11:41:06.588 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicCause = Watchdog Timeout 
([37mR[37mTC code 12)
    - 11:41:06.596 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_panicAddr = 0
    - 11:41:06.605 > [NVS] âŒ saveStrin[37mg[37m: Write failed (ns: logs) (key:[37m [37mdiag_panicTask)
    - 11:41:06.610 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicCore = [37m0[37m
    - 11:41:06.616 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:41:06.621 > [Diagnostics[37m][37m ðŸ’¾ Informations de panic sau[37mv[37megardÃ©es
    - 11:41:06.672 > [Diagnostics] ðŸš€ In[37mi[37mtialisÃ© - reboot #7, minHeap: [37m4[37m7780 bytes [PANIC: 
Watchdog Tim[37me[37mout (RTC code 12)]
    - 11:41:12.952 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:26.672 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Time[37mo[37mut (RTC code 12) (Core 0)
    - 11:41:26.691 > [NVS] âœ… Sauveg[37ma[37mrdÃ©: logs::diag_hasPanic = 1
    - 11:41:26.694 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicCause = Watchdog Timeout 
([37mR[37mTC code 12)
    - 11:41:26.703 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_panicAddr = 0
    - 11:41:26.711 > [NVS] âŒ saveStrin[37mg[37m: Write failed (ns: logs) (key:[37m [37mdiag_panicTask)
    - 11:41:26.716 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicCore = [37m0[37m
    - 11:41:26.722 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:41:26.727 > [Diagnostics[37m][37m ðŸ’¾ Informations de panic sau[37mv[37megardÃ©es
    - 11:41:26.774 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#8, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - 11:41:33.097 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:48.610 > [Diagnostics] ðŸ” Panic cap[37mt[37murÃ©: Watchdog Timeout (RTC cod[37me[37m 12) (Core 0)
    - 11:41:48.630 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_hasPanic = 1
    - 11:41:48.633 > [NVS] âœ… Sau[37mv[37megardÃ©: logs::diag_panicCause [37m=[37m Watchdog Timeout (RTC code 
12)[37m
    - [37m11:41:48.641 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicAddr = 0
    - 11:41:48.643 > [NVS] âœ… Sauve[37mg[37mardÃ©: logs::diag_panicExcv = 0[37m
    - [37m11:41:48.649 > [NVS] âŒ saveString: Write fai[37ml[37med (ns: logs) (key: diag_panicT[37ma[37msk)
    - 11:41:48.654 > [NVS] âœ… SauvegardÃ©: log[37ms[37m::diag_panicCore = 0
    - 11:41:48.657 > [NVS] âœ… [37mS[37mauvegardÃ©: logs::diag_panicInf[37mo[37m = ESP reset reason: 4, RTC 
rea[37ms[37mon: 12
    - 11:41:48.665 > [Diagnostics] ðŸ’¾ Infor[37mm[37mations de panic sauvegardÃ©es
    - 11:41:48.713 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #9, minHeap[37m:[37m 47780 bytes [PANIC: 
Watchdog T[37mi[37mmeout (RTC code 12)]
    - 11:41:55.082 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:08.799 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Tim[37me[37mout (RTC code 12) (Core 0)
    - 11:42:08.819 > [NVS] âœ… Sauve[37mg[37mardÃ©: logs::diag_hasPanic = 1
    - [37m11:42:08.824 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_panicCause = Watchdog Timeout 
[37m([37mRTC code 12)
    - 11:42:08.830 > [NVS] âœ… Sauvegar[37md[37mÃ©: logs::diag_panicAddr = 0
    - 11:42:08.838 > [NVS] âŒ saveStri[37mn[37mg: Write failed (ns: logs) (key[37m:[37m diag_panicTask)
    - 11:42:08.844 > [NVS] âœ… Sau[37mv[37megardÃ©: logs::diag_panicCore =[37m [37m0
    - 11:42:08.849 > [NVS] âœ… SauvegardÃ©: logs::[37md[37miag_panicInfo = ESP reset reaso[37mn[37m: 4, RTC 
reason: 12
    - 11:42:08.855 > [Diagnostic[37ms[37m] ðŸ’¾ Informations de panic sa[37mu[37mvegardÃ©es
    - 11:42:08.905 > [Diagnostics] ðŸš€ I[37mn[37mitialisÃ© - reboot #10, minHeap[37m:[37m 47780 bytes [PANIC: 
Watchdog T[37mi[37mmeout (RTC code 12)]
    - 11:42:15.190 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:29.013 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:42:29.032 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:42:29.035 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:42:29.043 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:42:29.046 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:42:29.051 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:42:29.057 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:42:29.060 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:42:29.068 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:42:29.118 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #11, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:42:35.416 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:49.339 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:42:49.359 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:42:49.361 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:42:49.370 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:42:49.373 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:42:49.378 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:42:49.384 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:42:49.386 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:42:49.395 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:42:49.445 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #12, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:42:55.743 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:12.066 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:43:12.085 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:43:12.091 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:43:12.096 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:43:12.099 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:43:12.104 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:43:12.110 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:43:12.115 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:43:12.121 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:43:12.171 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #13, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:43:18.488 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:32.012 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:43:32.032 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:43:32.034 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:43:32.043 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:43:32.045 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:43:32.051 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:43:32.057 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:43:32.059 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:43:32.068 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:43:32.118 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #14, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:43:38.609 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:51.617 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:43:51.636 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:43:51.639 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:43:51.647 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:43:51.650 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:43:51.656 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:43:51.661 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:43:51.664 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:43:51.672 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:43:51.722 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #15, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:43:58.004 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:11.727 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:44:11.746 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:44:11.749 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:44:11.757 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:44:11.760 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:44:11.766 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:44:11.771 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:44:11.774 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:44:11.782 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:44:11.832 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #16, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:44:18.135 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:31.643 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:44:31.662 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:44:31.668 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:44:31.673 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:44:31.676 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:44:31.681 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:44:31.687 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:44:31.692 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:44:31.698 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:44:31.748 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #17, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:44:38.047 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:51.771 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:44:51.791 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:44:51.794 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:44:51.802 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:44:51.805 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:44:51.810 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:44:51.816 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:44:51.818 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:44:51.827 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:44:51.877 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #18, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:44:58.407 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:45:14.524 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:45:14.543 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:45:14.546 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:45:14.554 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:45:14.557 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:45:14.562 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:45:14.568 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:45:14.571 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:45:14.579 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - 11:45:14.630 > [Diagnostics] ðŸš€ Initiali[37ms[37mÃ© - reboot #19, minHeap: 47780[37m [37mbytes [PANIC: 
Watchdog Timeout [37m([37mRTC code 12)]
    - 11:45:20.889 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:45:33.788 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:45:33.807 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:45:33.813 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:45:33.818 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:45:33.821 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:45:33.827 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:45:33.832 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:45:33.838 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:45:33.843 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:45:33.893 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #20, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:45:40.190 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:45:54.211 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:45:54.231 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:45:54.233 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:45:54.242 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:45:54.244 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:45:54.250 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:45:54.255 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:45:54.258 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:45:54.267 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:45:54.316 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #21, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:46:00.629 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:14.251 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:46:14.271 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:46:14.273 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:46:14.282 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:46:14.284 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:46:14.290 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:46:14.295 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:46:14.298 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:46:14.307 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:46:14.356 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #22, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:46:20.660 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:34.590 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:46:34.610 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:46:34.615 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:46:34.620 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:46:34.623 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:46:34.629 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:46:34.634 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:46:34.640 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:46:34.645 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:46:34.695 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #23, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:46:41.078 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:55.099 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:46:55.119 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:46:55.121 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:46:55.130 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:46:55.132 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:46:55.138 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:46:55.143 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:46:55.146 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:46:55.154 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:46:55.204 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #24, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:47:01.451 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:15.268 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:47:15.287 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:47:15.292 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:47:15.298 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:47:15.301 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:47:15.306 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:47:15.312 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:47:15.317 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:47:15.323 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:47:15.373 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #25, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:47:21.718 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:37.938 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:47:37.957 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:47:37.962 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:47:37.968 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:47:37.971 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:47:37.976 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:47:37.982 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:47:37.987 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:47:37.993 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:47:38.042 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #26, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:47:44.342 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:57.652 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:47:57.672 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:47:57.677 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:47:57.683 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:47:57.686 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:47:57.691 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:47:57.697 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:47:57.702 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:47:57.708 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:47:57.757 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #27, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:48:04.093 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:48:20.316 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:48:20.335 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:48:20.341 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:48:20.346 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:48:20.349 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:48:20.355 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:48:20.360 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:48:20.366 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:48:20.371 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:48:20.421 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #28, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:48:26.904 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:48:40.822 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:48:40.841 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:48:40.844 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:48:40.853 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:48:40.855 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:48:40.861 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:48:40.866 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:48:40.869 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:48:40.877 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:48:40.928 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #29, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:48:47.183 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:01.302 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:49:01.321 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:49:01.327 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:49:01.332 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:49:01.335 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:49:01.341 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:49:01.346 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:49:01.352 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:49:01.357 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:49:01.408 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #30, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:49:07.752 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:22.570 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:49:22.590 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:49:22.595 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:49:22.601 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:49:22.603 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:49:22.609 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:49:22.614 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:49:22.620 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:49:22.625 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:49:22.675 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #31, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:49:28.978 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:44.687 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:49:44.706 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:49:44.712 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:49:44.717 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:49:44.720 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:49:44.726 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:49:44.731 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:49:44.737 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:49:44.742 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:49:44.792 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #32, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:49:51.196 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:05.029 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:50:05.049 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:50:05.054 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:50:05.060 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:50:05.063 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:50:05.068 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:50:05.074 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:50:05.079 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:50:05.084 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:50:05.134 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #33, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:50:06.718 > Guru Meditation Error: Core  1 p[37ma[37mnic'ed (Unhandled debug excepti[37mo[37mn).
    - 11:50:07.358 > abort() was called at PC 0x401b436b on core 1
    - 11:50:07.358 > Backtrace: [37m0x40083210:0x3ffafee0 0x4009071d[37m:[37m0x3ffaff00 
0x40097065:0x3ffaff2[37m0[37m 0x401b436b:0x3ffaffa0 0x401b49[37ma[37m1:0x3ffaffd0 0x401b4a2d:0x3ffb0[37m0[37m10 
0x401b4e89:0x3ffb0040 0x401b[37m5[37m3db:0x3ffb00e0 0x401b4273:0x3ff[37mb[37m01b0 0x40176f4c:0x3ffb01d0 
0x40[37m1[37m7615d:0x3ffb0200 0x400827c9:0x3[37mf[37mfb0250 0x40085bd3:0x3ffb0270 0x[37m0[37m0040021:0x3ffb0330 
|<-CORRUPTED[37m
    - 11:50:07.389 > Re-entered core dump! Ex[37mc[37meption happened during core dum[37mp[37m!
    - 11:50:09.306 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:24.914 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:50:24.933 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:50:24.936 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:50:24.944 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:50:24.947 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:50:24.952 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:50:24.958 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:50:24.960 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:50:24.969 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - 11:50:25.017 > [Diagnostics][37m [37mðŸš€ InitialisÃ© - reboot #34, [37mm[37minHeap: 47780 bytes [PANIC: 
Wat[37mc[37mhdog Timeout (RTC code 12)]
    - 11:50:27.156 > abort() was called at PC 0x401[37mb[37m436b on core 1
    - 11:50:27.159 > Backtrace: [37m0[37mx40083210:0x3ffafee0 0x4009071d[37m:[37m0x3ffaff00 
0x40097065:0x3ffaff2[37m0[37m 0x401b436b:0x3ffaffa0 0x401b49[37ma[37m1:0x3ffaffd0 0x401b4a2d:0x3ffb0[37m0[37m10 
0x401b4e89:0x3ffb0040 0x401b[37m5[37m3db:0x3ffb00e0 0x401b4273:0x3ff[37mb[37m01b0 0x40176f4c:0x3ffb01d0 
0x40[37m1[37m7615d:0x3ffb0200 0x400827c9:0x3[37mf[37mfb0250 0x40085bd3:0x3ffb0270 0x[37m0[37m0040021:0x3ffb0330 
|<-CORRUPTED[37m
    - 11:50:27.192 > Re-entered core dump! Ex[37mc[37meption happened during core dum[37mp[37m!
    - 11:50:29.108 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:44.717 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:50:44.736 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:50:44.739 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:50:44.747 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:50:44.750 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:50:44.756 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:50:44.761 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:50:44.764 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:50:44.772 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:50:44.823 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #35, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:50:51.068 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:04.883 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:51:04.902 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:51:04.905 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:51:04.913 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:51:04.916 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:51:04.922 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:51:04.927 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:51:04.930 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:51:04.938 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - 11:51:04.985 > [Diagnostics] ðŸš€[37m [37mInitialisÃ© - reboot #36, minHe[37ma[37mp: 47780 bytes [PANIC: 
Watchdog[37m [37mTimeout (RTC code 12)]
    - 11:51:11.392 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:27.513 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:51:27.532 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:51:27.535 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:51:27.543 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:51:27.546 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:51:27.552 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:51:27.557 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:51:27.560 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:51:27.568 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:51:27.617 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #37, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:51:33.898 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:47.305 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:51:47.325 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:51:47.330 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:51:47.336 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:51:47.338 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:51:47.344 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:51:47.350 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:51:47.355 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:51:47.360 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:51:47.410 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #38, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:51:53.744 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:07.665 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:52:07.685 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:52:07.690 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:52:07.696 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:52:07.698 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:52:07.704 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:52:07.709 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:52:07.715 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:52:07.721 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:52:07.770 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #39, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:52:14.135 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:30.251 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:52:30.270 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:52:30.276 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:52:30.281 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:52:30.284 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:52:30.289 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:52:30.295 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:52:30.300 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:52:30.306 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:52:30.356 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #40, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:52:36.643 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:50.561 > [Diagnostics] [37m[37mðŸ” Panic capturÃ©: Watchdog Ti[37mm[37meout (RTC code 12) (Core 0)
    - 11:52:50.580 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_hasPanic = 1[37m
    - [37m11:52:50.586 > [NVS] âœ… SauvegardÃ©: logs::di[37ma[37mg_panicCause = Watchdog Timeout[37m [37m(RTC 
code 12)
    - 11:52:50.591 > [NVS] âœ… Sauvega[37mr[37mdÃ©: logs::diag_panicAddr = 0
    - 11:52:50.594 > [[37mN[37mVS] âœ… SauvegardÃ©: logs::diag[37m_[37mpanicExcv = 0
    - 11:52:50.600 > [NVS] âŒ saveStr[37mi[37mng: Write failed (ns: logs) (ke[37my[37m: diag_panicTask)
    - 11:52:50.605 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCore [37m=[37m 0
    - 11:52:50.611 > [NVS] âœ… SauvegardÃ©: logs:[37m:[37mdiag_panicInfo = ESP reset reas[37mo[37mn: 4, RTC 
reason: 12
    - 11:52:50.616 > [Diagnosti[37mc[37ms] ðŸ’¾ Informations de panic s[37ma[37muvegardÃ©es
    - 11:52:50.666 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #41, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:52:56.962 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:53:10.786 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:53:10.806 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:53:10.808 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:53:10.817 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:53:10.819 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:53:10.825 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:53:10.831 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:53:10.833 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:53:10.842 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:53:10.891 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #42, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:53:17.177 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:53:30.999 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:53:31.018 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:53:31.021 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:53:31.029 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:53:31.032 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:53:31.037 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:53:31.043 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:53:31.046 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:53:31.054 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:53:31.103 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #43, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:53:37.433 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:53:53.558 > [Diagnostics] ðŸ” Panic ca[37mp[37mturÃ©: Watchdog Timeout (RTC co[37md[37me 12) (Core 0)
    - 11:53:53.577 > [NVS] âœ… SauvegardÃ©: logs[37m:[37m:diag_hasPanic = 1
    - 11:53:53.580 > [NVS] âœ… Sa[37mu[37mvegardÃ©: logs::diag_panicCause[37m [37m= Watchdog Timeout (RTC code 
12[37m)[37m
    - 11:53:53.588 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicAddr = 0
    - 11:53:53.591 > [NVS] âœ… Sauv[37me[37mgardÃ©: logs::diag_panicExcv = [37m0[37m
    - 11:53:53.596 > [NVS] âŒ saveString: Write fa[37mi[37mled (ns: logs) (key: diag_panic[37mT[37mask)
    - 11:53:53.602 > [NVS] âœ… SauvegardÃ©: lo[37mg[37ms::diag_panicCore = 0
    - 11:53:53.604 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:53:53.613 > [Diagnostics] ðŸ’¾ Info[37mr[37mmations de panic sauvegardÃ©es
    - [37m11:53:53.662 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #44, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - 11:54:00.148 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)

⚠️ WATCHDOG ET TIMEOUTS
  Nombre d'occurrences: 303
    - 11:39:15.832 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:39:23.232 > [O[37mT[37mA] âœ… SÃ©curitÃ©: Watchdog dÃ©[37ms[37mactivÃ©, validation complÃ¨te
    - 11:39:29.242 > [Web] Timeout ser[37mv[37meur: 2000 ms
    - 11:39:29.547 > [Diagnostics] ðŸ” [37mP[37manic capturÃ©: Watchdog Timeout[37m [37m(RTC code 12) (Core 0)
    - 11:39:29.569 > [NVS[37m][37m âœ… SauvegardÃ©: logs::diag_pa[37mn[37micCause = Watchdog Timeout (RTC[37m 
[37mcode 12)
    - 11:39:29.651 > [Diagnostics][37m [37mðŸš€ InitialisÃ© - reboot #2, m[37mi[37mnHeap: 47780 bytes [PANIC: 
Watc[37mh[37mdog Timeout (RTC code 12)]
    - 11:39:29.816 > [Pow[37me[37mr] Watchdog prÃªt (gÃ©rÃ© par T[37mW[37mDT natif)
    - 11:39:35.937 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:39:43.443 > [O[37mT[37mA] âœ… SÃ©curitÃ©: Watchdog dÃ©[37ms[37mactivÃ©, validation complÃ¨te
    - 11:39:49.453 > [Web] Timeout ser[37mv[37meur: 2000 ms

💾 PROBLÈMES MÉMOIRE
  Nombre d'alertes mémoire: 128
    - 11:39:13.460 > [[37mM[37mEM_DIAG]   Fragmentation: 24%
    - 11:39:15.817 > [MEM_DIA[37mG[37m]   Fragmentation: 47%
    - 11:39:29.219 > [M[37mE[37mM_DIAG]   Fragmentation: 21%
    - 11:39:33.576 > [[37mM[37mEM_DIAG]   Fragmentation: 27%
    - 11:39:35.920 > [MEM_DIA[37mG[37m]   Fragmentation: 47%
    - 11:39:49.429 > [M[37mE[37mM_DIAG]   Fragmentation: 21%
    - 11:39:53.885 > [[37mM[37mEM_DIAG]   Fragmentation: 27%
    - 11:39:56.282 > [MEM_DIA[37mG[37m]   Fragmentation: 47%
    - 11:40:09.776 > [M[37mE[37mM_DIAG]   Fragmentation: 21%
    - 11:40:14.352 > [MEM_DIA[37mG[37m]   Fragmentation: 47%
  Heap minimum détecté:
    - [37m11:39:15.280 >  [37m Minimum Free Bytes:   206680 B[37m [37m( 201.8 KB)
    - 11:39:29.469 > [NVS] ðŸ“– ChargÃ©:[37m [37mlogs::diag_minHeap = 47780
    - [37m11:39:35.385 >  [37m Minimum Free Bytes:   206680 B[37m [37m( 201.8 KB)
    - 11:39:49.680 > [NVS] ðŸ“– ChargÃ©:[37m [37mlogs::diag_minHeap = 47780
    - 11:39:49.864 > [Diagnostics] ðŸš€ In[37mi[37mtialisÃ© - reboot #3, minHeap: [37m4[37m7780 bytes [PANIC: 
Watchdog Tim[37me[37mout (RTC code 12)]

🌐 PROBLÈMES RÉSEAU
  ✅ Aucun problème réseau détecté

📦 DATAQUEUE ET ROTATION
  Nombre d'occurrences: 215
    - [37m11:39:11.137 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:39:11.992 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:39:12.282 > [[37mDataQueue] âœ“ Payload enregistrÃ© (455 bytes, total: 41 entrÃ©es)
    - [37m11:39:30.218 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:39:30.435 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:39:31.243 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:39:32.091 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:39:32.399 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)
    - [37m11:39:50.432 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:39:50.651 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:39:51.461 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:39:52.330 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:39:52.707 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:40:10.772 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:40:10.987 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:40:11.791 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:40:28.033 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:40:28.247 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:40:29.051 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:40:29.868 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:40:30.166 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:40:47.782 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:40:47.997 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:40:48.802 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:40:49.633 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:40:49.932 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:41:07.236 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:41:07.453 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:41:08.261 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:41:09.045 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:41:09.350 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:41:27.343 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:41:27.562 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:41:28.371 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:41:29.247 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:41:29.524 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:41:49.282 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:41:49.503 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:41:50.314 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:41:51.239 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:41:51.537 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:42:09.468 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:42:09.683 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:42:10.489 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:42:11.315 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:42:11.611 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:42:29.680 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:42:29.896 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:42:30.702 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:42:31.504 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:42:31.802 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - 11:42:31.808 > [DataQueue] ðŸ“Š Heap delta [37mp[37mush: -12 bytes (avant=108020, a[37mp[37mrÃ¨s=108008)
    - [37m11:42:50.010 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:42:50.227 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:42:51.036 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:42:51.839 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:42:52.139 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)
    - [37m11:43:12.739 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:43:12.959 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:43:13.768 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:43:14.635 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:43:14.940 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:43:32.687 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:43:32.907 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:43:33.718 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:43:34.767 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:43:35.065 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)
    - [37m11:43:52.284 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:43:52.499 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:43:53.304 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:43:54.128 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:43:54.428 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:44:12.395 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:44:12.610 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:44:13.416 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:44:14.223 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:44:14.524 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:44:32.314 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:44:32.532 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:44:33.341 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:44:34.144 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:44:34.442 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:44:52.444 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:44:52.664 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:44:53.612 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:44:54.489 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:44:54.806 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)
    - [37m11:45:15.190 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:45:15.403 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:45:16.210 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:45:17.034 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:45:17.337 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:45:34.457 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:45:34.672 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:45:35.484 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:45:36.309 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:45:36.608 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:45:54.880 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:45:55.096 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:45:55.903 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:45:56.717 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:45:57.019 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:46:14.922 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:46:15.140 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:46:15.949 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:46:16.753 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:46:17.056 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:46:35.264 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:46:35.484 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:46:36.296 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:46:37.228 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:46:37.526 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:46:55.764 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:46:55.977 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:46:56.781 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:46:57.600 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:46:57.900 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:47:15.937 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:47:16.152 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:47:16.959 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:47:17.799 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:47:18.099 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:47:38.609 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:47:38.826 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:47:39.634 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:47:40.426 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:47:40.735 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - 11:47:40.740 > [DataQueue] ðŸ“Š Heap delta [37mp[37mush: -24 bytes (avant=108096, a[37mp[37mrÃ¨s=108072)
    - [37m11:47:58.326 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:47:58.545 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:47:59.355 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:48:00.232 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:48:00.511 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - 11:48:00.517 > [DataQueue] ðŸ“Š Heap delta [37mp[37mush: 128 bytes (avant=108068, a[37mp[37mrÃ¨s=108196)
    - [37m11:48:20.990 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:48:21.210 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:48:22.021 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:48:23.064 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:48:23.359 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:48:41.489 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:48:41.704 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:48:42.510 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:48:43.334 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:48:43.636 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:49:01.973 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:49:02.190 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:49:02.997 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:49:03.837 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:49:04.135 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:49:23.241 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:49:23.459 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:49:24.270 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:49:25.066 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:49:25.373 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)
    - [37m11:49:45.360 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:49:45.579 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:49:46.394 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:49:47.271 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:49:47.590 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:50:05.695 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:50:05.909 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:50:06.712 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:50:25.578 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:50:25.792 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:50:26.596 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:50:45.383 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:50:45.596 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:50:46.403 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:50:47.218 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:50:47.511 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:51:05.548 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:51:05.762 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:51:06.639 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:51:07.472 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:51:07.770 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:51:28.182 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:51:28.398 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:51:29.206 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:51:29.993 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:51:30.299 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:51:47.977 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:51:48.195 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:51:49.005 > [[37mDataQueue] âš ï¸ Queue pleine,[37m rotation...
    - [37m11:51:49.881 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:51:50.158 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:52:08.340 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:52:08.560 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:52:09.371 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:52:10.286 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:52:10.585 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:52:30.919 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:52:31.134 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:52:31.941 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:52:32.761 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:52:33.059 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:52:51.230 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:52:51.445 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:52:52.251 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:52:53.056 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:52:53.350 > [[37mDataQueue] âœ“ Payload enregist[37mr[37mÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:53:11.456 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:53:11.673 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:53:12.481 > [[37mDataQueue] âš ï¸ Queue pleine, rotation...
    - [37m11:53:13.277 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:53:13.575 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:53:31.671 > [[37mDataQueue] âœ“ InitialisÃ©e: 41[37m [37mentrÃ©es en attente
    - [37m11:53:31.891 > [[37mSync] âœ“ DataQueue initialisÃ©[37me[37m (41 entrÃ©es)
    - [37m11:53:32.701 > [[37mDataQueue] âš ï¸ Queue pleine,[37m [37mrotation...
    - [37m11:53:33.572 > [[37mDataQueue] âš ï¸ Rotation effe[37mc[37mtuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:53:33.881 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37me[37ms)
    - [37m11:53:54.230 > [[37mDataQueue] âœ“ InitialisÃ©e: 41 entrÃ©es en attente
    - [37m11:53:54.450 > [[37mSync] âœ“ DataQueue initialisÃ©e (41 entrÃ©es)
    - [37m11:53:55.260 > [[37mDataQueue] âš ï¸ Queue pleine,[37m rotation...
    - [37m11:53:56.298 > [[37mDataQueue] âš ï¸ Rotation effectuÃ©e: 1 entrÃ©es supprimÃ©es
    - [37m11:53:56.596 > [[37mDataQueue] âœ“ Payload enregistrÃ© (454 bytes, total: 41 entrÃ©[37mes)

🔄 REBOOTS ET RESETS
  Nombre de reboots: 551
    - [37m11:39:13.922 > R[37mebooting...
    - 11:39:13.925 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:39:15.528 >  [37m CDC On Boot       : 0
    - [37m11:39:15.804 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:39:15.821 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:39:15.824 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:15.832 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:39:23.200 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:39:23.216 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:39:29.466 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 1
    - 11:39:29.520 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 2
    - [37m11:39:29.598 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_panicInfo = ESP reset reason: [37m4[37m, 
RTC reason: 12
    - 11:39:29.651 > [Diagnostics][37m [37mðŸš€ InitialisÃ© - reboot #2, m[37mi[37mnHeap: 47780 bytes [PANIC: 
Watc[37mh[37mdog Timeout (RTC code 12)]
    - [37m11:39:34.026 > R[37mebooting...
    - 11:39:34.029 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:39:35.633 >  [37m CDC On Boot       : 0
    - [37m11:39:35.909 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:39:35.925 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:39:35.928 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:35.937 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:39:43.410 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:39:43.426 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:39:49.676 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 2
    - 11:39:49.731 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 3
    - 11:39:49.808 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:39:49.864 > [Diagnostics] ðŸš€ In[37mi[37mtialisÃ© - reboot #3, minHeap: [37m4[37m7780 bytes [PANIC: 
Watchdog Tim[37me[37mout (RTC code 12)]
    - [37m11:39:54.388 > R[37mebooting...
    - 11:39:54.391 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:39:55.994 >  [37m CDC On Boot       : 0
    - [37m11:39:56.270 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:39:56.287 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:39:56.290 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:56.298 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:40:03.756 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:40:03.773 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:40:10.024 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 3
    - [37m11:40:10.079 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 4
    - 11:40:10.153 > [NVS] âœ… [37mS[37mauvegardÃ©: logs::diag_panicInf[37mo[37m = ESP reset reason: 4, RTC 
rea[37ms[37mon: 12
    - 11:40:10.210 > [[37mD[37miagnostics] ðŸš€ InitialisÃ© - [37mr[37meboot #4, minHeap: 47780 bytes 
[37m[[37mPANIC: Watchdog Timeout (RTC co[37md[37me 12)]
    - 11:40:12.451 > Rebooting...
    - 11:40:12.454 > rst:0xc (SW_CPU_RE[37mS[37mET),boot:0x13 (SPI_FAST_FLASH_B[37mO[37mOT)
    - [37m11:40:14.064 >  [37m CDC On Boot       : 0
    - [37m11:40:14.340 > [[37mMEM_DIAG] Snapshot: boot at 104[37m1[37m ms
    - 11:40:14.357 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:40:14.360 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:14.368 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:40:21.020 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:40:21.037 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:40:27.287 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 4
    - 11:40:27.341 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 5
    - 11:40:27.418 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:40:27.471 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#5, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - [37m11:40:31.806 > R[37mebooting...
    - 11:40:31.811 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:40:33.412 >  [37m CDC On Boot       : 0
    - [37m11:40:33.688 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:40:33.705 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:40:33.708 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:33.716 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:40:40.766 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:40:40.782 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:40:47.034 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 5
    - 11:40:47.088 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 6
    - 11:40:47.165 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:40:47.219 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#6, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - [37m11:40:51.643 > R[37mebooting...
    - 11:40:51.646 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:40:53.250 >  [37m CDC On Boot       : 0
    - [37m11:40:53.526 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:40:53.543 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:40:53.545 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:40:53.554 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:41:00.217 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:41:00.233 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:41:06.485 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 6
    - 11:41:06.538 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 7
    - 11:41:06.616 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:41:06.672 > [Diagnostics] ðŸš€ In[37mi[37mtialisÃ© - reboot #7, minHeap: [37m4[37m7780 bytes [PANIC: 
Watchdog Tim[37me[37mout (RTC code 12)]
    - [37m11:41:11.049 > R[37mebooting...
    - 11:41:11.054 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:41:12.656 >  [37m CDC On Boot       : 0
    - [37m11:41:12.932 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:41:12.949 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:41:12.952 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:12.960 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:41:20.325 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:41:20.342 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:41:26.591 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 7
    - 11:41:26.645 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 8
    - 11:41:26.722 > [NVS] âœ… SauvegardÃ©: logs::d[37mi[37mag_panicInfo = ESP reset reason[37m:[37m 4, RTC 
reason: 12
    - 11:41:26.774 > [Diagno[37ms[37mtics] ðŸš€ InitialisÃ© - reboot[37m [37m#8, minHeap: 47780 bytes 
[PANIC[37m:[37m Watchdog Timeout (RTC code 12)[37m][37m
    - [37m11:41:31.194 > R[37mebooting...
    - 11:41:31.197 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:41:32.801 >  [37m CDC On Boot       : 0
    - [37m11:41:33.077 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:41:33.094 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:41:33.097 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:33.105 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:41:42.260 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:41:42.277 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:41:48.527 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 8
    - [37m11:41:48.582 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 9
    - 11:41:48.657 > [NVS] âœ… [37mS[37mauvegardÃ©: logs::diag_panicInf[37mo[37m = ESP reset reason: 4, RTC 
rea[37ms[37mon: 12
    - 11:41:48.713 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #9, minHeap[37m:[37m 47780 bytes [PANIC: 
Watchdog T[37mi[37mmeout (RTC code 12)]
    - [37m11:41:53.180 > R[37mebooting...
    - 11:41:53.185 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:41:54.786 >  [37m CDC On Boot       : 0
    - [37m11:41:55.062 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:41:55.079 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:41:55.082 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:41:55.090 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:42:02.452 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:42:02.468 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:42:08.718 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 9
    - 11:42:08.772 > [NVS] âœ… Sauvegard[37mÃ©[37m: logs::diag_rebootCnt = 10
    - 11:42:08.905 > [Diagnostics] ðŸš€ I[37mn[37mitialisÃ© - reboot #10, minHeap[37m:[37m 47780 bytes [PANIC: 
Watchdog T[37mi[37mmeout (RTC code 12)]
    - [37m11:42:13.288 > R[37mebooting...
    - 11:42:13.291 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:42:14.894 >  [37m CDC On Boot       : 0
    - [37m11:42:15.170 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:42:15.187 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:42:15.190 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:15.198 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:42:22.662 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:42:22.679 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:42:28.929 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 10
    - [37m11:42:28.985 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 11
    - 11:42:29.060 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:42:29.118 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #11, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:42:33.513 > R[37mebooting...
    - 11:42:33.517 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:42:35.120 >  [37m CDC On Boot       : 0
    - [37m11:42:35.396 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:42:35.413 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:42:35.416 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:35.424 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:42:42.989 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:42:43.005 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:42:49.256 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 11
    - [37m11:42:49.312 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 12
    - 11:42:49.386 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:42:49.445 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #12, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:42:53.841 > R[37mebooting...
    - 11:42:53.846 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:42:55.447 >  [37m CDC On Boot       : 0
    - [37m11:42:55.724 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:42:55.740 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:42:55.743 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:42:55.751 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:43:05.717 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:43:05.734 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:43:11.984 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 12
    - 11:43:12.038 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 13
    - 11:43:12.171 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #13, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:43:16.586 > R[37mebooting...
    - 11:43:16.591 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:43:18.192 >  [37m CDC On Boot       : 0
    - [37m11:43:18.468 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:43:18.485 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:43:18.488 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:18.496 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:43:25.661 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:43:25.678 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:43:31.929 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 13
    - [37m11:43:31.985 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 14
    - 11:43:32.059 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:43:32.118 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #14, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:43:36.707 > R[37mebooting...
    - 11:43:36.710 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:43:38.314 >  [37m CDC On Boot       : 0
    - [37m11:43:38.590 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:43:38.607 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:43:38.609 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:38.618 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:43:45.266 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:43:45.282 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:43:51.534 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 14
    - [37m11:43:51.589 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 15
    - 11:43:51.664 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:43:51.722 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #15, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:43:56.102 > R[37mebooting...
    - 11:43:56.105 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:43:57.709 >  [37m CDC On Boot       : 0
    - [37m11:43:57.985 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:43:58.001 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:43:58.004 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:43:58.013 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:44:05.376 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:44:05.393 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:44:11.643 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 15
    - [37m11:44:11.699 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 16
    - 11:44:11.774 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:44:11.832 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #16, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:44:16.233 > R[37mebooting...
    - 11:44:16.236 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:44:17.840 >  [37m CDC On Boot       : 0
    - [37m11:44:18.116 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:44:18.132 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:44:18.135 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:18.143 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:44:25.295 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:44:25.312 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:44:31.561 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 16
    - 11:44:31.615 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 17
    - 11:44:31.748 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #17, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:44:36.144 > R[37mebooting...
    - 11:44:36.149 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:44:37.751 >  [37m CDC On Boot       : 0
    - [37m11:44:38.027 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:44:38.044 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:44:38.047 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:38.055 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:44:45.420 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:44:45.437 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:44:51.688 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 17
    - [37m11:44:51.744 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 18
    - 11:44:51.818 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:44:51.877 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #18, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:44:56.504 > R[37mebooting...
    - 11:44:56.509 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:44:58.111 >  [37m CDC On Boot       : 0
    - [37m11:44:58.388 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:44:58.404 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:44:58.407 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:44:58.416 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:45:08.174 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:45:08.191 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:45:14.441 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 18
    - [37m11:45:14.496 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 19
    - 11:45:14.571 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:45:14.630 > [Diagnostics] ðŸš€ Initiali[37ms[37mÃ© - reboot #19, minHeap: 47780[37m [37mbytes [PANIC: 
Watchdog Timeout [37m([37mRTC code 12)]
    - [37m11:45:18.986 > R[37mebooting...
    - 11:45:18.989 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:45:20.594 >  [37m CDC On Boot       : 0
    - [37m11:45:20.870 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:45:20.887 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:45:20.889 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:45:20.898 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:45:27.439 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:45:27.455 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:45:33.706 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 19
    - 11:45:33.760 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 20
    - 11:45:33.893 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #20, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:45:38.287 > R[37mebooting...
    - 11:45:38.289 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:45:39.894 >  [37m CDC On Boot       : 0
    - [37m11:45:40.170 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:45:40.187 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:45:40.190 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:45:40.198 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:45:47.861 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:45:47.878 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:45:54.128 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 20
    - [37m11:45:54.184 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 21
    - 11:45:54.258 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:45:54.316 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #21, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:45:58.725 > R[37mebooting...
    - 11:45:58.731 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:46:00.333 >  [37m CDC On Boot       : 0
    - [37m11:46:00.609 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:46:00.626 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:46:00.629 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:00.637 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:46:07.901 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:46:07.918 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:46:14.168 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 21
    - [37m11:46:14.223 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 22
    - 11:46:14.298 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:46:14.356 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #22, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:46:18.756 > R[37mebooting...
    - 11:46:18.759 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:46:20.364 >  [37m CDC On Boot       : 0
    - [37m11:46:20.640 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:46:20.657 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:46:20.660 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:20.668 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:46:28.240 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:46:28.257 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:46:34.508 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 22
    - 11:46:34.563 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 23
    - 11:46:34.695 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #23, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:46:39.175 > R[37mebooting...
    - 11:46:39.178 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:46:40.782 >  [37m CDC On Boot       : 0
    - [37m11:46:41.059 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:46:41.076 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:46:41.078 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:46:41.086 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:46:48.748 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:46:48.764 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:46:55.016 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 23
    - [37m11:46:55.071 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 24
    - 11:46:55.146 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:46:55.204 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #24, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:46:59.548 > R[37mebooting...
    - 11:46:59.553 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:47:01.155 >  [37m CDC On Boot       : 0
    - [37m11:47:01.431 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:47:01.448 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:47:01.451 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:01.459 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:47:08.918 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:47:08.934 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:47:15.186 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 24
    - 11:47:15.240 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 25
    - 11:47:15.373 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #25, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:47:19.815 > R[37mebooting...
    - 11:47:19.820 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:47:21.422 >  [37m CDC On Boot       : 0
    - [37m11:47:21.698 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:47:21.715 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:47:21.718 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:21.726 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:47:31.588 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:47:31.605 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:47:37.856 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 25
    - 11:47:37.910 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 26
    - 11:47:38.042 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #26, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:47:42.439 > R[37mebooting...
    - 11:47:42.442 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:47:44.046 >  [37m CDC On Boot       : 0
    - [37m11:47:44.322 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:47:44.339 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:47:44.342 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:47:44.350 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:47:51.304 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:47:51.320 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:47:57.571 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 26
    - 11:47:57.625 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 27
    - 11:47:57.757 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #27, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:48:02.190 > R[37mebooting...
    - 11:48:02.196 > rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:48:03.797 >  [37m CDC On Boot       : 0
    - [37m11:48:04.074 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:48:04.091 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:48:04.093 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:48:04.102 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:48:13.967 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:48:13.984 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:48:20.234 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 27
    - 11:48:20.288 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 28
    - 11:48:20.421 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #28, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:48:25.001 > R[37mebooting...
    - 11:48:25.004 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:48:26.609 >  [37m CDC On Boot       : 0
    - [37m11:48:26.885 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:48:26.902 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:48:26.904 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:48:26.913 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:48:34.472 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:48:34.488 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:48:40.739 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 28
    - [37m11:48:40.794 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 29
    - 11:48:40.869 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:48:40.928 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #29, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:48:45.279 > R[37mebooting...
    - 11:48:45.282 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:48:46.887 >  [37m CDC On Boot       : 0
    - [37m11:48:47.163 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:48:47.180 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:48:47.183 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:48:47.191 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:48:54.954 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:48:54.971 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:49:01.220 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 29
    - 11:49:01.275 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 30
    - 11:49:01.408 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #30, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:49:05.849 > R[37mebooting...
    - 11:49:05.853 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:49:07.456 >  [37m CDC On Boot       : 0
    - [37m11:49:07.732 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:49:07.749 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:49:07.752 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:07.760 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:49:16.222 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:49:16.239 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:49:22.488 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 30
    - 11:49:22.543 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 31
    - 11:49:22.675 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #31, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:49:27.075 > R[37mebooting...
    - 11:49:27.078 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:49:28.683 >  [37m CDC On Boot       : 0
    - [37m11:49:28.959 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:49:28.976 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:49:28.978 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:28.987 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:49:38.339 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:49:38.356 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:49:44.605 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 31
    - 11:49:44.659 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 32
    - 11:49:44.792 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #32, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:49:49.293 > R[37mebooting...
    - 11:49:49.298 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:49:50.900 >  [37m CDC On Boot       : 0
    - [37m11:49:51.176 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:49:51.193 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:49:51.196 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:49:51.204 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:49:58.678 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:49:58.695 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:50:04.948 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 32
    - 11:50:05.002 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 33
    - 11:50:05.134 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #33, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - 11:50:07.395 > Rebooting...
    - 11:50:07.398 > rst:0xc (SW_CPU_RE[37mS[37mET),boot:0x13 (SPI_FAST_FLASH_B[37mO[37mOT)
    - [37m11:50:09.009 >  [37m CDC On Boot       : 0
    - [37m11:50:09.286 > [[37mMEM_DIAG] Snapshot: boot at 104[37m2[37m ms
    - 11:50:09.303 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:50:09.306 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:09.314 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:50:18.564 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:50:18.582 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:50:24.830 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 33
    - [37m11:50:24.886 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 34
    - 11:50:24.960 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:50:25.017 > [Diagnostics][37m [37mðŸš€ InitialisÃ© - reboot #34, [37mm[37minHeap: 47780 bytes [PANIC: 
Wat[37mc[37mhdog Timeout (RTC code 12)]
    - 11:50:27.198 > Rebooting...
    - 11:50:27.201 > rst:0xc (SW_CPU_RE[37mS[37mET),boot:0x13 (SPI_FAST_FLASH_B[37mO[37mOT)
    - [37m11:50:28.812 >  [37m CDC On Boot       : 0
    - [37m11:50:29.089 > [[37mMEM_DIAG] Snapshot: boot at 104[37m2[37m ms
    - 11:50:29.106 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:50:29.108 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:29.117 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:50:38.364 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:50:38.381 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:50:44.634 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 34
    - [37m11:50:44.689 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 35
    - 11:50:44.764 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:50:44.823 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #35, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:50:49.165 > R[37mebooting...
    - 11:50:49.170 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:50:50.773 >  [37m CDC On Boot       : 0
    - [37m11:50:51.049 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:50:51.066 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:50:51.068 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:50:51.077 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:50:58.534 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:50:58.551 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:51:04.800 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 35
    - [37m11:51:04.855 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 36
    - 11:51:04.930 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - 11:51:04.985 > [Diagnostics] ðŸš€[37m [37mInitialisÃ© - reboot #36, minHe[37ma[37mp: 47780 bytes [PANIC: 
Watchdog[37m [37mTimeout (RTC code 12)]
    - [37m11:51:09.489 > R[37mebooting...
    - 11:51:09.494 > rst:0xc (SW_CPU_RESET),boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:51:11.096 >  [37m CDC On Boot       : 0
    - [37m11:51:11.372 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:51:11.389 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:51:11.392 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:11.400 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:51:21.163 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:51:21.180 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:51:27.430 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 36
    - [37m11:51:27.485 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 37
    - 11:51:27.560 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:51:27.617 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #37, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:51:31.995 > R[37mebooting...
    - 11:51:32.000 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:51:33.602 >  [37m CDC On Boot       : 0
    - [37m11:51:33.878 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:51:33.895 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:51:33.898 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:33.906 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:51:40.958 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:51:40.974 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:51:47.224 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 37
    - 11:51:47.278 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 38
    - 11:51:47.410 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #38, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:51:51.841 > R[37mebooting...
    - 11:51:51.846 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:51:53.448 >  [37m CDC On Boot       : 0
    - [37m11:51:53.724 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:51:53.741 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:51:53.744 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:51:53.752 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:52:01.317 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:52:01.335 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:52:07.584 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 38
    - 11:52:07.638 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 39
    - 11:52:07.770 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #39, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:52:12.232 > R[37mebooting...
    - 11:52:12.237 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:52:13.839 >  [37m CDC On Boot       : 0
    - [37m11:52:14.115 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:52:14.132 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:52:14.135 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:14.143 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:52:23.900 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:52:23.917 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:52:30.169 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 39
    - 11:52:30.223 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 40
    - 11:52:30.356 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #40, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:52:34.740 > R[37mebooting...
    - 11:52:34.745 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:52:36.348 >  [37m CDC On Boot       : 0
    - [37m11:52:36.624 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:52:36.641 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:52:36.643 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:36.652 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:52:44.212 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:52:44.229 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:52:50.479 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 40
    - 11:52:50.533 > [NVS] âœ… Sauvegard[37m[37mÃ©: logs::diag_rebootCnt = 41
    - 11:52:50.666 > [Diagnostics] ðŸš€ [37mI[37mnitialisÃ© - reboot #41, minHea[37mp[37m: 47780 bytes [PANIC: 
Watchdog [37mT[37mimeout (RTC code 12)]
    - [37m11:52:55.059 > R[37mebooting...
    - 11:52:55.063 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:52:56.666 >  [37m CDC On Boot       : 0
    - [37m11:52:56.942 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:52:56.959 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:52:56.962 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:52:56.970 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:53:04.435 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:53:04.452 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:53:10.703 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 41
    - [37m11:53:10.759 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 42
    - 11:53:10.833 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:53:10.891 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #42, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:53:15.274 > R[37mebooting...
    - 11:53:15.278 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:53:16.881 >  [37m CDC On Boot       : 0
    - [37m11:53:17.158 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:53:17.174 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:53:17.177 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:53:17.185 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:53:24.649 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:53:24.665 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:53:30.915 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 42
    - [37m11:53:30.971 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 43
    - 11:53:31.046 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:53:31.103 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #43, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:53:35.530 > R[37mebooting...
    - 11:53:35.535 > rst:0xc (SW_CPU_RESET)[37m,boot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:53:37.137 >  [37m CDC On Boot       : 0
    - [37m11:53:37.413 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:53:37.430 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:53:37.433 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:53:37.441 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:53:47.210 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:53:47.224 > [Boot] initMailQueue retourne: [37mO[37mK
    - [37m11:53:53.475 > [[37mNVS] ðŸ“– ChargÃ©: logs::diag_r[37me[37mbootCnt = 43
    - [37m11:53:53.530 > [[37mNVS] âœ… SauvegardÃ©: logs::dia[37mg[37m_rebootCnt = 44
    - 11:53:53.604 > [NVS] âœ…[37m [37mSauvegardÃ©: logs::diag_panicIn[37mf[37mo = ESP reset reason: 4, RTC 
re[37ma[37mson: 12
    - [37m11:53:53.662 > [[37mDiagnostics] ðŸš€ InitialisÃ© -[37m [37mreboot #44, minHeap: 47780 byte[37ms[37m 
[PANIC: Watchdog Timeout (RTC [37mc[37mode 12)]
    - [37m11:53:58.245 > R[37mebooting...
    - 11:53:58.248 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:53:59.853 >  [37m CDC On Boot       : 0
    - [37m11:54:00.129 > [[37mMEM_DIAG] Snapshot: boot at 103[37m5[37m ms
    - 11:54:00.146 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:54:00.148 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:54:00.157 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - 11:54:07.823 > [Boot] In[37mi[37mtialisation queue mail sÃ©quent[37mi[37melle...
    - [37m11:54:07.840 > [Boot] initMailQueue retourne: [37mO[37mK

📋 VERSION ET BUILD INFO
    - [37m11:39:15.466 >  [37m Compile Date/Time : Jan 26 202[37m6[37m 10:26:47
    - [37m11:39:15.472 >  [37m ESP-IDF Version   : v5.5-1-gb6[37m6[37mb5448e0
    - [37m11:39:15.479 >  [37m Arduino Version   : 3.3.0
    - 11:39:15.821 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:39:17.150 > [INFO] DÃ©marrage [37mF[37mFP5CS v11.156

💿 ERREURS NVS
  Nombre d'erreurs NVS: 427
    - 11:39:29.471 > [ 14[37m6[37m96][E][Preferences.cpp:506] get[37mS[37mtring(): nvs_get_str len fail: 
[37md[37miag_httpOk NOT_FOUND
    - 11:39:29.496 > [ 14723][E][Preferences.cp[37mp[37m:506] getString(): nvs_get_str [37ml[37men fail: 
diag_otaOk NOT_FOUND
    - 11:39:29.508 > [ 14734][E][Preferences[37m.[37mcpp:506] getString(): nvs_get_s[37mt[37mr len fail: 
diag_otaKo NOT_FOUN[37mD[37m
    - 11:39:29.586 > [NVS] âŒ saveString: [37mW[37mrite failed (ns: logs) (key: di[37ma[37mg_panicTask)
    - 11:39:29.841 > [ 1[37m5[37m069][V][Preferences.cpp:375] ge[37mt[37mUChar(): nvs_get_u8 fail: 
bouff[37me[37m_matin NOT_FOUND
    - 11:39:29.877 > [ 15[37m1[37m09][V][Preferences.cpp:375] get[37mU[37mChar(): nvs_get_u8 fail: 
bf_for[37mc[37me_wk NOT_FOUND
    - 11:39:29.908 > [ 15128][V][Preferences.cpp:37[37m5[37m] getUChar(): nvs_get_u8 fail: [37mn[37met_send_en 
NOT_FOUND
    - 11:39:29.914 > [ 15145][[37mV[37m][Preferences.cpp:375] getUChar[37m([37m): nvs_get_u8 fail: 
net_recv_en[37m [37mNOT_FOUND
    - 11:39:29.939 > [ 15164][E][Prefer[37me[37mnces.cpp:506] getString(): nvs_[37mg[37met_str len fail: 
remote_json NO[37mT[37m_FOUND
    - 11:39:30.438 > [ 15665][V][Pref[37me[37mrences.cpp:375] getUChar(): nvs[37m_[37mget_u8 fail: forceWakeUp 
NOT_FO[37mU[37mND

🚀 SÉQUENCE DE DÉMARRAGE
  Premières étapes de démarrage:
    - [37m11:39:13.922 > R[37mebooting...
    - 11:39:13.925 > rst:0xc (SW_CPU_RESET)[37m,[37mboot:0x13 (SPI_FAST_FLASH_BOOT)[37m
    - [37m11:39:15.162 > =[37m========== Before Setup Start =[37m=[37m=========
    - [37m11:39:15.528 >  [37m CDC On Boot       : 0
    - [37m11:39:15.532 > =[37m=========== Before Setup End ==[37m=[37m=========
    - [37m11:39:15.636 > [[37m   866][V][esp32-hal-uart.c:707[37m][37m uartBegin(): UART0 baud(115200[37m)[37m 
Mode(800001c) rxPin(3) txPin(1[37m)[37m
    - [37m11:39:15.647 > [[37m   875][V][esp32-hal-uart.c:805[37m][37m uartBegin(): UART0 not install[37me[37md. 
Starting installation
    - [37m11:39:15.657 > [[37m   885][V][esp32-hal-uart.c:815[37m][37m uartBegin(): UART0 RX FIFO ful[37ml[37m 
threshold set to 120 (value re[37mq[37muested: 120 || FIFO Max = 128)
    - [37m11:39:15.671 > [[37m   898][V][esp32-hal-uart.c:845[37m][37m uartBegin(): Setting UART0 to [37mu[37mse 
REF_TICK clock
    - [37m11:39:15.687 > [[37m   909][V][esp32-hal-uart.c:905[37m][37m uartBegin(): UART0 initializat[37mi[37mon 
done.
    - [37m11:39:15.804 > [[37mMEM_DIAG] Snapshot: boot at 103[37m4[37m ms
    - 11:39:15.821 > ==[37m=[37m BOOT FFP5CS v11.156 ===
    - 11:39:15.824 > [BOOT][37m [37mReset reason: 4 - âš ï¸ PANIC [37mr[37meset (crash)
    - 11:39:15.832 > [BOOT] Watchdog [37mc[37monfigurÃ©: timeout=300000 ms
    - [37m11:39:17.094 > [[37mApp] ðŸš€ Initialisation du ges[37mt[37mionnaire NVS centralisÃ©
    - 11:39:17.097 > [NVS][37m [37mðŸš€ Initialisation du gestionn[37ma[37mire NVS centralisÃ©
    - 11:39:17.183 > [10:39:16][INF[37mO[37m][TIME] Initialisation de la ge[37ms[37mtion du temps
    - 11:39:17.238 > [INFO] Initialisation d[37mu[37m moniteur de temps (SimplifiÃ©)[37m
    - 11:39:17.246 > [DEBUG] Avan[37mt[37m oled.begin()
    - 11:39:17.421 > [  2650][W][Wire.cpp:300[37m][37m begin(): Bus already started i[37mn[37m Master Mode.

=== RÉSUMÉ FINAL ===
  Mémoire: ⚠️ 128 alerte(s)
  Reboots: 🔄 551 reboot(s)
  NVS: ⚠️ 427 erreur(s)
  Watchdog: ⚠️ 303 alerte(s)
  Crashes: 🔴 476 crash(es)
  Réseau: ✅ OK
  DataQueue: ℹ️ 215 événement(s)

=== ANALYSE TERMINÉE ===

\\\

---

## 4. Vérifications de Cohérence avec le Code

### Serveur Distant

- ❌ POST: Impossible de calculer la fréquence - ❌ GET: Impossible de calculer la fréquence - ❌ Endpoint: Aucun endpoint détecté

### Mail

- ⚠️ 43 mail(s) manquant(s) détecté(s)

---

## 5. Recommandations

### ❌ ACTION REQUISE - Crashes Détectés

1. **Analyser les core dumps** si disponibles
2. **Vérifier la configuration matérielle**
3. **Examiner les logs détaillés** pour identifier la cause
4. **Refaire un test** après corrections


---

## Fichiers Générés

- **Log complet:** \$LogFile\
- **Diagnostic serveur:** \$serverDiagFile\
- **Diagnostic mails:** \$emailDiagFile\
- **Analyse exhaustive:** \$exhaustiveDiagFile\
- **Rapport complet:** \$reportFile\

---

*Rapport généré automatiquement par generate_diagnostic_report.ps1*
