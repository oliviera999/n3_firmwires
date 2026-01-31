# Analyse des reboots – logs de monitoring 2026-01-28

## Résumé

| Session (log) | Reboots pendant le monitoring | Cause du boot visible | Compteur NVS (reboot #) | minHeap NVS |
|---------------|-------------------------------|------------------------|--------------------------|-------------|
| 22-15-43 (avant seuils) | **0** | POWERON_RESET | #4 | 21700 bytes |
| 23-13-24 (après seuils) | **0** | POWERON_RESET | #6 | 9536 bytes |

**Conclusion** : Aucun reboot n’a eu lieu pendant les deux sessions de monitoring 10 min. Chaque log ne contient qu’un seul boot (celui du flash + démarrage du script). Le compteur « reboot #N » est un cumul persistant en NVS, pas le nombre de reboots dans la session.

---

## Détail par log

### 1. Log 23-13-24 (après ajustement des seuils TLS)

**Démarrage** : 23:16:46  
**Durée** : 600 s

- **Séquence de boot** : une seule
  - `rst:0x1 (POWERON_RESET), boot:0x13 (SPI_FAST_FLASH_BOOT)`
  - `entry 0x4008059c`
  - `=========== Before Setup Start ===========` (une seule fois)

- **Diagnostics au boot** (23:17:24) :
  - `[Diagnostics] 🚀 Initialisé - reboot #6, minHeap: 9536 bytes`
  - Pas de `[PANIC: ...]` → pas de crash précédent enregistré

- **Payload envoyé au serveur** (23:22:05) :
  - `uptime=317&free=22448&min=9536&reboots=6`
  - uptime 317 s ≈ 5 min 17 s après le boot
  - reboots=6 = compteur NVS (6ᵉ boot depuis la dernière réinitialisation du compteur / NVS)

**Reboots pendant la session** : **0**  
**Cause du boot** : POWERON_RESET (mise sous tension ou flash, pas WDT ni panic).

---

### 2. Log 22-15-43 (avant ajustement des seuils)

**Démarrage** : 22:19:25  

- **Séquence de boot** : une seule (même schéma que ci‑dessus).
- **Diagnostics** : `reboot #4, minHeap: 21700 bytes`.

**Reboots pendant la session** : **0**.

---

## Comportement du compteur de reboots (NVS)

- **Compteur** : `diag_rebootCnt` en NVS (namespace LOGS).
- Au boot : lecture du compteur, +1, sauvegarde.
- **« reboot #6 »** = ce boot est le 6ᵉ depuis la dernière remise à zéro du compteur (ou effacement NVS), pas 6 reboots pendant la capture.
- Entre les deux logs (reboot #4 → #6), il y a eu 2 autres boots (ex. autres flashs ou power‑cycle) en dehors de ces fichiers log.

---

## minHeap historique (NVS)

- **minHeap** = minimum de heap libre jamais atteint, persisté en NVS (`diag_minHeap`).
- **9536 bytes** (log 23-13-24) : valeur chargée depuis NVS, donc atteinte lors d’un run précédent (éventuellement une session où le heap était très bas, ex. avant les seuils 40 KB / 32 KB).
- **21700 bytes** (log 22-15-43) : autre session, minHeap plus haut.

Ces valeurs ne prouvent pas un reboot en session ; elles indiquent seulement qu’à un moment (passé ou courant), le heap est descendu à ce niveau.

---

## Causes de reset ESP32 (référence)

- `rst:0x1` = **POWERON_RESET** (alimentation ou flash).
- Autres causes possibles (non vues dans ces logs) : WDT (0x3, 0x4), panic (0x2), brownout (0x5), etc.

---

## Synthèse

- **Aucun reboot** pendant les deux sessions de 10 min analysées.
- Un seul boot par log : celui du début (flash + démarrage).
- **reboot #4 / #6** = compteur cumulé en NVS, pas nombre de reboots dans le log.
- **minHeap 9536 / 21700** = minimum historique NVS, pas preuve de reboot en session.
- Comportement cohérent avec un système stable pendant le monitoring ; les ajustements TLS (40 KB / 32 KB) n’ont pas introduit de reboot visible dans ces captures.
