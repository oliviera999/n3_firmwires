# 🎯 Rapport Diagnostic Final - Problème de Communication Serveur

**Date:** 2026-01-26  
**Problème:** Aucun envoi POST vers le serveur distant

---

## 🔍 Diagnostic Complet

### Problème Identifié

**`pollRemoteState()` BLOQUE INDÉFINIMENT** dans `netRpc()`

### Chaîne d'Appels et Point de Blocage

```
automationTask()
  └─> Automatism::update()
      └─> _network.pollRemoteState(doc, now)  [automatism.cpp:107]
          └─> AutomatismSync::pollRemoteState()  [automatism_sync.cpp:351]
              └─> fetchRemoteState(doc)  [automatism_sync.cpp:356]
                  └─> AppTasks::netFetchRemoteState(doc, 30000)  [automatism_sync.cpp:336]
                      └─> netRpc(req)  [app_tasks.cpp:608]
                          └─> ⚠️ BLOQUE ICI (ligne 594-597)
```

### Code Problématique

**`src/app_tasks.cpp:567-599` - Fonction `netRpc()`:**

```cpp
static bool netRpc(NetRequest& req) {
  if (!g_netQueue || !g_netTaskHandle) return false;
  req.requester = xTaskGetCurrentTaskHandle();
  req.success = false;

  // Clear any pending notification
  (void)ulTaskNotifyTake(pdTRUE, 0);

  NetRequest* ptr = &req;
  if (xQueueSend(g_netQueue, &ptr, pdMS_TO_TICKS(50)) != pdTRUE) {
    return false;  // ✅ Queue pleine, retourne immédiatement
  }
  
  // ⚠️ PROBLÈME: Première boucle d'attente (jusqu'à timeoutMs)
  uint32_t waitedMs = 0;
  const uint32_t stepMs = 250;
  while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(req.timeoutMs)) == 0) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(stepMs));
    waitedMs += stepMs;
    if (waitedMs >= 60000) {
      Serial.println(F("[netRPC] ⚠️ Timeout >60s, attente forcée jusqu'à fin (sécurité mémoire)"));
      break;
    }
  }
  
  // ❌ PROBLÈME CRITIQUE: Deuxième boucle SANS LIMITE
  while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(50));
    // ⚠️ Cette boucle attend INDÉFINIMENT jusqu'à ce que netTask notifie
    // Si netTask ne notifie jamais, cette boucle bloque pour toujours
  }
  return req.success;
}
```

### Pourquoi ça Bloque

1. **La requête est envoyée dans la queue** (ligne 576) ✅
2. **`netTask` devrait recevoir la requête** (ligne 89) ✅
3. **`netTask` devrait traiter la requête** (lignes 95-113) ✅
4. **`netTask` devrait appeler `netNotifyDone()`** (ligne 116) ❓
5. **`netRpc()` attend la notification** (ligne 594) ⚠️

**Problème:** Si `netTask` ne traite pas la requête ou n'appelle pas `netNotifyDone()`, la boucle ligne 594 bloque indéfiniment.

### Observations du Log

**Messages présents:**
- `[Auto] 🔍 DEBUG: JsonDocument créé, avant pollRemoteState()` ✅
- Messages HTTP `[GET]` apparaissent (depuis `netTask`) ✅

**Messages absents:**
- `[Auto] 🔍 DEBUG: pollRemoteState() retourne true/false` ❌
- Tous les messages après `pollRemoteState()` ❌

**Interprétation:**
- `pollRemoteState()` est appelé
- La requête est probablement envoyée dans la queue
- Mais `netRpc()` ne retourne jamais (bloque dans la boucle ligne 594)
- Donc `pollRemoteState()` ne retourne jamais
- Donc le code ne continue jamais jusqu'à `_network.update()`

---

## 🔧 Solutions Proposées

### Solution 1: Ajouter un Timeout Absolu dans `netRpc()`

Modifier la deuxième boucle pour avoir un timeout absolu :

```cpp
// Attente finale avec timeout absolu (sécurité)
uint32_t finalWaitStart = millis();
const uint32_t FINAL_TIMEOUT_MS = 35000;  // 35 secondes max
while (ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(1000)) == 0) {
    esp_task_wdt_reset();
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // Timeout absolu pour éviter blocage infini
    if (millis() - finalWaitStart > FINAL_TIMEOUT_MS) {
        Serial.println(F("[netRPC] ⚠️ Timeout absolu atteint, abandon"));
        return false;
    }
}
```

### Solution 2: Ajouter des Logs dans `netRpc()` et `netTask`

Ajouter des logs pour tracer :
- L'envoi de la requête dans la queue
- La réception de la requête dans `netTask`
- Le traitement de la requête
- L'envoi de la notification
- La réception de la notification

### Solution 3: Vérifier que `netTask` Traite les Requêtes

Vérifier que :
- `netTask` est actif
- La queue `g_netQueue` fonctionne
- `netNotifyDone()` est bien appelé après traitement

### Solution 4: Appeler `_network.update()` AVANT `pollRemoteState()`

Si `pollRemoteState()` bloque, on peut appeler `_network.update()` avant pour permettre l'envoi POST même si la réception bloque.

---

## 📋 Plan d'Action Recommandé

1. **Ajouter un timeout absolu** dans la deuxième boucle de `netRpc()`
2. **Ajouter des logs** dans `netRpc()` et `netTask` pour tracer le problème
3. **Vérifier l'état de `netTask`** (est-elle active? traite-t-elle les requêtes?)
4. **Tester** avec les modifications

---

**Rapport généré le:** 2026-01-26  
**Problème identifié:** Blocage dans `netRpc()` ligne 594-597  
**Impact:** Empêche tout envoi POST vers le serveur
