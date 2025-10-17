# Fix de Reconnexion WiFi avec Détection IP Automatique

## 🔧 Problème résolu

Lors d'un changement de réseau WiFi, l'ESP32 change d'adresse IP, mais le client web tentait de se reconnecter à l'ancienne IP, causant des échecs de connexion WebSocket.

### Séquence d'erreur précédente :
1. ✅ Changement WiFi détecté via WebSocket
2. ✅ Fermeture propre du WebSocket (code 1000)
3. ❌ **Problème** : Tentative de reconnexion à l'ancienne IP (`192.168.0.87`)
4. ❌ Échec de connexion WebSocket sur tous les ports
5. ❌ Fallback en mode polling

## 🚀 Solution implémentée

### 1. **Détection automatique de la nouvelle IP**
- **Fonction `detectNewESP32IP()`** qui utilise deux méthodes :
  - **mDNS** : Tentative de connexion à `esp32.local`
  - **Scan réseau** : Test des IPs communes du sous-réseau local (192.168.x.1-20)

### 2. **Mise à jour automatique de l'URL**
- Détection de la nouvelle IP avant reconnexion WebSocket
- Mise à jour de `window.location.hostname` avec la nouvelle IP
- Reconnexion automatique avec la bonne adresse

### 3. **Amélioration de la gestion des timeouts**
- Timeout client aligné avec le serveur (18s vs 15s serveur)
- Messages d'erreur plus informatifs
- Gestion intelligente des tentatives de reconnexion

## 📝 Code ajouté

### Fonction de détection IP :
```javascript
async function detectNewESP32IP() {
  return new Promise((resolve, reject) => {
    // Méthode 1: mDNS (.local)
    const mdnsCheck = fetch('http://esp32.local/json', { 
      method: 'HEAD',
      cache: 'no-store',
      signal: AbortSignal.timeout(2000)
    }).then(() => 'esp32.local').catch(() => null);
    
    // Méthode 2: Scan IPs locales
    const baseIP = currentIP.substring(0, currentIP.lastIndexOf('.'));
    const ipScanPromises = [...].map(ip => 
      fetch(`http://${ip}/json`, { 
        method: 'HEAD',
        signal: AbortSignal.timeout(1000)
      }).then(() => ip).catch(() => null)
    );
    
    Promise.race([mdnsCheck, Promise.race(ipScanPromises)])
      .then(result => result ? resolve(result) : reject());
  });
}
```

### Gestion améliorée des changements WiFi :
```javascript
// Après détection du changement WiFi
setTimeout(() => {
  detectNewESP32IP().then(newIP => {
    if (newIP) {
      console.log(`🔍 Nouvelle IP ESP32 détectée: ${newIP}`);
      window.location.hostname = newIP;
    }
    connectWS();
  }).catch(() => {
    connectWS(); // Fallback sans détection
  });
}, 5000);
```

## 🎯 Résultats attendus

### Séquence corrigée :
1. ✅ Changement WiFi détecté via WebSocket
2. ✅ Fermeture propre du WebSocket (code 1000)
3. ✅ **NOUVEAU** : Détection automatique de la nouvelle IP
4. ✅ **NOUVEAU** : Mise à jour de l'URL avec la nouvelle IP
5. ✅ Reconnexion WebSocket réussie sur la nouvelle IP
6. ✅ Communication temps réel rétablie

## 🧪 Test

Un fichier `test_ip_detection.html` a été créé pour tester la fonction de détection IP indépendamment.

## 📊 Impact

- **Reconnexion automatique** après changement de réseau
- **Expérience utilisateur améliorée** (pas de rechargement manuel)
- **Robustesse accrue** du système WebSocket
- **Compatibilité** avec différents types de réseaux WiFi

## 🔄 Prochaines améliorations possibles

1. **Cache IP** : Sauvegarder les IPs précédentes pour accélérer la détection
2. **mDNS avancé** : Support de noms personnalisés (.local)
3. **Scan intelligent** : Prioriser les IPs récemment utilisées
4. **Notifications** : Informer l'utilisateur de la nouvelle IP détectée
