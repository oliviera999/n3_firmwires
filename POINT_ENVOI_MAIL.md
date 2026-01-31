# Point niveau envoi mail

## État actuel

### Activation
- **FEATURE_MAIL** = 1 (activé, `platformio.ini`).
- SMTP : `smtp.gmail.com:465` (TLS), connexion différée au premier envoi.

### Architecture
- **Queue** : 2 slots (`TaskConfig::MAIL_QUEUE_SIZE = 2`), traitement séquentiel.
- **Envoi asynchrone** : `sendAlert()` / `send()` ajoutent en queue et retournent tout de suite.
- **Traitement** : `automationTask` appelle `processOneMailSync()` quand `hasPendingMails() && TLSMutex::canConnect()`, avec acquisition mutex TLS 3 s ; puis `sendAlertSync()` acquiert le mutex TLS 10 s pour SMTP.
- **Partage TLS** : même mutex que HTTPS (SMTP et requêtes HTTP(S) ne peuvent pas tourner en parallèle).

### Sources d’envoi
- Boot : mail « Démarrage système » (system_boot), mail « OTA interface web » (system_boot).
- Automatisme : alertes niveau (aquarium trop plein, réserve basse, chauffage ON/OFF, pompe bloquée, etc.).
- OTA : « OTA début / fin - Serveur distant » (ota_manager).
- Web : envoi manuel depuis l’interface (web_server).

---

## Comportement observé (log 23-13-24)

### Queue
- Au boot : 2 alertes mises en queue (OTA interface web, Démarrage système v11.156).
- Puis plusieurs « Queue pleine, alerte ignorée » (Heater ON/OFF, etc.) car la queue n’a que 2 slots et le traitement est lent (échecs SMTP).

### Envois réels
- **Aucun mail envoyé avec succès** pendant la session (~10 min).
- **6 échecs envoi mail** enregistrés (`_mailsFailed`).

### Cause du premier échec (23:17:44)
- Gmail répond **550 5.4.5 Daily user sending limit exceeded**.
- Log : `send body failed` → `ERREUR sendMail code=550 err=-109 reason=send body failed`.
- Donc **quota d’envoi Gmail dépassé** pour le compte utilisé (limite quotidienne).

### Ensuite
- « Reconnexion SMTP échouée - code: 550 » ou « code: 0 » à chaque tentative.
- Pas de « Message envoyé avec succès » dans le log : tous les envois échouent (quota / refus Gmail ou session fermée).

---

## Points techniques

### Garde-fous côté firmware
- Heap : si &lt; 40 KB, log « Heap bas - tentative d’envoi quand même » (pas de blocage).
- Mutex TLS : timeout 10 s pour SMTP ; si non acquis, « Envoi annulé: impossible d’acquérir le mutex TLS ».
- Light sleep : si `g_enteringLightSleep`, « Envoi annulé: système en transition vers light sleep ».
- Réseau : si non prêt, « Réseau non prêt, abandon envoi mail ».

### Queue pleine
- Taille 2 : dès que 2 mails sont en attente et non encore traités, tout nouvel appel à `sendAlert()` / `send()` donne « Queue pleine, alerte ignorée » (ou « mail ignoré »).
- Beaucoup d’alertes au boot (OTA, Démarrage, Heater, etc.) remplissent la queue et les suivantes sont perdues.

---

## Synthèse

| Élément              | État |
|----------------------|------|
| Code / architecture  | OK (queue, mutex TLS, traitement séquentiel). |
| Connexion SMTP (TLS) | OK (connexion établie, envoi tenté). |
| Réponse Gmail        | 550 Daily limit exceeded → quota dépassé. |
| Envois réussis       | 0 pendant le test. |
| Alertes ignorées     | Oui (queue pleine, 2 slots). |

**Conclusion** : Côté firmware, l’envoi mail est en place et tente bien d’envoyer. Les échecs viennent de **Gmail (quota quotidien)** et du fait que la **queue à 2 slots** sature au boot, ce qui fait ignorer d’autres alertes. Pour améliorer :  
1) réduire le nombre d’envois au boot ou les regrouper ;  
2) augmenter un peu la taille de la queue si on veut moins d’alertes ignorées (en restant raisonnable en RAM) ;  
3) côté compte Gmail : respecter les limites d’envoi ou utiliser un compte / relais dédié pour les alertes.
