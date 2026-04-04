// FFP5CS Dashboard - Gestion WebSocket et WiFi

/** Traces [DEBUG] : affichées seulement si common.js a chargé LogConfig avec niveau DEBUG ou TRACE. */
function ffp5csWsDebugLog(...args) {
  try {
    if (typeof LogConfig !== 'undefined' && LogConfig.levels && typeof LogConfig.currentLevel === 'number') {
      if (LogConfig.levels.DEBUG > LogConfig.currentLevel) return;
    } else {
      return;
    }
  } catch (_e) {
    return;
  }
  console.log(...args);
}

function ffp5csWsDebugError(...args) {
  try {
    if (typeof LogConfig !== 'undefined' && LogConfig.levels && typeof LogConfig.currentLevel === 'number') {
      if (LogConfig.levels.DEBUG > LogConfig.currentLevel) return;
    } else {
      return;
    }
  } catch (_e) {
    return;
  }
  console.error(...args);
}

// WebSocket + Fallback polling avec essai de plusieurs ports
function connectWS() {
  try {
    const proto = (location.protocol === 'https:') ? 'wss' : 'ws';
    
    // Essayer d'abord le port 81 (WebSocket dédié), puis le port 80 (standard)
    const ports = [81, 80];
    let currentPortIndex = 0;
    let connectionTimeout = null;
    
    function tryConnect() {
      if (currentPortIndex >= ports.length) {
        console.log('Tous les ports WebSocket ont échoué, utilisation du mode polling');
        startPolling();
        return;
      }
      
      const port = ports[currentPortIndex];
      const wsUrl = port === 80 ? 
        proto + '://' + location.host + '/ws' : 
        proto + '://' + location.hostname + ':' + port + '/ws';
      
      logInfo(`Tentative de connexion WebSocket sur ${wsUrl} (port ${port})`, { wsUrl, port }, 'WEBSOCKET');
      ws = new WebSocket(wsUrl);
      
      // Timeout de connexion de 10 secondes
      connectionTimeout = setTimeout(() => {
        if (ws && ws.readyState !== WebSocket.OPEN) {
          console.warn(`⏱️ Timeout de connexion WebSocket sur le port ${port} (10s)`);
          ws.close();
          // Essayer le port suivant
          currentPortIndex++;
          tryConnect();
        }
      }, 10000);
      
      ws.onopen = () => {
        // Annuler le timeout
        if (connectionTimeout) {
          clearTimeout(connectionTimeout);
          connectionTimeout = null;
        }
        
        wsConnected = true;
        isOnline = true;
        updateConnectionStatus(true);
        stopPolling();
        toast(`Connexion WebSocket établie sur le port ${port}`, 'success');
        logInfo(`WebSocket connecté sur le port ${port}`, { port }, 'WEBSOCKET');
        
        // Envoyer un message de subscription pour recevoir les données (après un délai plus long)
        setTimeout(() => {
          if (ws && ws.readyState === WebSocket.OPEN) {
            try {
              ws.send(JSON.stringify({type: 'subscribe'}));
            } catch (error) {
              console.warn('[WebSocket] Erreur envoi subscription:', error);
            }
          }
        }, 500);
        
        // Démarrer le ping périodique
        startWSPing();
        
        // CORRECTION: Arrêter les tentatives de connexion sur d'autres ports
        currentPortIndex = ports.length; // Force l'arrêt des tentatives
      };
      
      ws.onclose = (event) => {
        // Nettoyer les timeouts et états
        if (connectionTimeout) {
          clearTimeout(connectionTimeout);
          connectionTimeout = null;
        }
        
        wsConnected = false;
        stopWSPing();
        
        // Log du code de fermeture pour diagnostic
        console.log(`🔌 WebSocket fermé (code: ${event.code}, raison: ${event.reason || 'N/A'})`);
        
        // Gestion spéciale pour les changements de réseau WiFi
        if (window.wifiChangePending) {
          console.log('🔄 WebSocket fermé suite à un changement de réseau (code: 1000)');
          // Ne pas essayer de reconnecter immédiatement, attendre la reconnexion automatique
          return;
        }
        
        wsConnected = false;
        isOnline = false;
        updateConnectionStatus(false);
        
        const reason = event.reason || 'aucune raison spécifiée';
        
        console.log(`❌ WebSocket fermé sur le port ${port}, code: ${event.code}, raison: ${reason}`);
        
        // Ne pas retenter si c'était une fermeture normale (code 1000)
        if (event.code === 1000) {
          console.log('Fermeture normale du WebSocket');
          return;
        }
        
        // Essayer le port suivant après un délai
        setTimeout(() => {
          currentPortIndex++;
          tryConnect();
        }, 2000);
      };
      
      ws.onerror = (error) => {
        // Annuler le timeout si présent
        if (connectionTimeout) {
          clearTimeout(connectionTimeout);
          connectionTimeout = null;
        }
        
        wsConnected = false;
        isOnline = false;
        updateConnectionStatus(false);
        stopWSPing();
        
        console.log(`⚠️ Erreur WebSocket sur le port ${port}:`, error);
        console.log(`Détails de l'erreur:`, {
          type: error.type,
          target: error.target,
          url: wsUrl,
          readyState: ws ? ws.readyState : 'undefined'
        });
        
        // L'événement onerror est toujours suivi de onclose, 
        // donc on ne fait pas de retry ici pour éviter les doublons
      };
      
      ws.onmessage = (ev) => {
        try {
          const msg = JSON.parse(ev.data);
          logDebug('Message WebSocket reçu', msg, 'WEBSOCKET');
          // Gérer différents types de messages
          if (msg && (msg.type === 'sensor_update' || msg.type === 'sensor_data')) {
            updateSensorDisplay(msg);
            if (msg.dbVars && typeof displayDbVars === 'function') {
              displayDbVars(msg.dbVars);
            }
          } else if (msg && msg.type === 'wifi_change') {
            // Changement de réseau WiFi en cours
            console.log(`🔄 Changement de réseau WiFi vers: ${msg.ssid}`);
            window.wifiChangePending = true;
            toast(`Changement de réseau vers ${msg.ssid}...`, 'info');
            
            // Le WebSocket va se fermer, préparer une reconnexion après 15 secondes
            setTimeout(() => {
              console.log('🔄 Tentative de reconnexion après changement WiFi...');
              window.wifiChangePending = false;
              
              // Méthode améliorée: essayer plusieurs fois la détection IP avec scan étendu
              const attemptReconnection = async (attempt = 1, maxAttempts = 4) => {
                console.log(`🔄 Tentative de reconnexion ${attempt}/${maxAttempts}...`);
                
                try {
                  const newIP = await detectNewESP32IP();
                  if (newIP) {
                    console.log(`🔍 Nouvelle IP ESP32 détectée: ${newIP}`);
                    window.location.hostname = newIP;
                    toast(`Nouvelle IP détectée: ${newIP}`, 'success', 5000);
                    connectWS();
                    return true;
                  }
                } catch (error) {
                  console.log(`🔍 Détection IP échouée (tentative ${attempt}):`, error.message);
                }
                
                // Si ce n'est pas la dernière tentative, attendre et réessayer
                if (attempt < maxAttempts) {
                  toast(`Recherche ESP32... (${attempt}/${maxAttempts})`, 'info', 3000);
                  setTimeout(() => attemptReconnection(attempt + 1, maxAttempts), 8000); // 8s entre tentatives
                } else {
                  // Dernière tentative échouée - essayer un scan plus agressif
                  console.log('🔍 Tentative de scan étendu...');
                  toast('Scan étendu en cours...', 'info', 5000);
                  
                  // Scan étendu sur plus d'IPs
                  const extendedScan = async () => {
                    const baseIP = window.location.hostname.substring(0, window.location.hostname.lastIndexOf('.'));
                    const extendedIPs = [];
                    
                    // Scanner de 1 à 200
                    for (let i = 1; i <= 200; i++) {
                      extendedIPs.push(`${baseIP}.${i}`);
                    }
                    
                    // Tester par batches de 10
                    for (let batch = 0; batch < Math.ceil(extendedIPs.length / 10); batch++) {
                      const batchIPs = extendedIPs.slice(batch * 10, (batch + 1) * 10);
                      const promises = batchIPs.map(ip => {
                        return new Promise((resolve) => {
                          const testWs = new WebSocket(`ws://${ip}:81/ws`);
                          const timeout = setTimeout(() => {
                            testWs.close();
                            resolve(null);
                          }, 1000);
                          
                          testWs.onopen = () => {
                            clearTimeout(timeout);
                            testWs.close();
                            console.log(`🔍 ESP32 trouvé à: ${ip}`);
                            resolve(ip);
                          };
                          
                          testWs.onerror = () => {
                            clearTimeout(timeout);
                            resolve(null);
                          };
                        });
                      });
                      
                      const results = await Promise.all(promises);
                      const foundIP = results.find(ip => ip !== null);
                      
                      if (foundIP) {
                        console.log(`🎯 ESP32 trouvé via scan étendu: ${foundIP}`);
                        window.location.hostname = foundIP;
                        toast(`ESP32 trouvé: ${foundIP}`, 'success', 5000);
                        connectWS();
                        return true;
                      }
                    }
                    
                    return false;
                  };
                  
                  const found = await extendedScan();
                  if (!found) {
                    console.log('🔍 Scan étendu échoué');
                    toast('Recherche ESP32 échouée. Veuillez recharger la page pour vous reconnecter au nouveau réseau.', 'warning', 15000);
                    console.log('💡 Suggestion: Recharger la page pour se reconnecter au nouveau réseau');
                  }
                }
              };
              
              attemptReconnection();
            }, 15000);
          } else if (msg && msg.type === 'server_closing') {
            // Serveur en cours de reconfiguration ou entrée en veille
            console.log('⚠️ Serveur fermé:', msg.message || 'Reconfiguration');
            
            // Afficher un message à l'utilisateur
            if (msg.message && msg.message.includes('sleep')) {
              toast('💤 Système en veille - Reconnexion automatique au réveil', 'info');
            } else {
              toast('⚠️ Serveur en reconfiguration - Reconnexion automatique', 'info');
            }
            
            window.wifiChangePending = true;
            
            // Reconnexion automatique après un délai plus long pour le sleep
            setTimeout(() => {
              console.log('🔄 Tentative de reconnexion après fermeture serveur...');
              window.wifiChangePending = false;
              connectWS();
            }, msg.message && msg.message.includes('sleep') ? 30000 : 10000); // 30s pour sleep, 10s pour reconfig
          }
        } catch (e) {
          console.error('Erreur parsing message WebSocket:', e);
        }
      };
    }
    
    tryConnect();
    
  } catch (e) {
    console.error('Erreur lors de l\'initialisation WebSocket:', e);
    startPolling();
  }
}

function startPolling() {
  if (pollInterval) return;
  pollInterval = setInterval(refresh, 3000);
}

function stopPolling() {
  if (pollInterval) {
    clearInterval(pollInterval);
    pollInterval = null;
  }
}

function startWSPing() {
  if (wsPingInterval) return;
  wsPingInterval = setInterval(() => {
    if (ws && wsConnected && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify({type: 'ping'}));
    }
  }, 30000); // Ping toutes les 30 secondes
}

function stopWSPing() {
  if (wsPingInterval) {
    clearInterval(wsPingInterval);
    wsPingInterval = null;
  }
}

// Fonction principale de rafraîchissement (optimisée)
window.refresh = async function refresh() {
  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000); // Timeout 5s
    
    const response = await fetch('/json', {
      cache: 'no-store',
      credentials: 'include',
      signal: controller.signal
    });
    
    clearTimeout(timeoutId);
    
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const data = await response.json();
    updateConnectionStatus(true);
    updateSensorDisplay(data);

  } catch (error) {
    if (error.name !== 'AbortError') {
      console.error('Erreur dans refresh():', error);
    }
    // CORRECTION: Ne pas mettre automatiquement hors ligne si WebSocket est connecté
    // Le WebSocket peut être actif même si HTTP échoue temporairement
    if (!wsConnected) {
      updateConnectionStatus(false);
    }
  }
}

// Gestion des onglets
function selectTab(name) {
  const isDb = name === 'DbVars';
  const isWifi = name === 'Wifi';
  
  // Mettre à jour les onglets
  $('tabDashboard').classList.toggle('active', !isDb && !isWifi);
  $('tabDbVars').classList.toggle('active', isDb);
  $('tabWifi').classList.toggle('active', isWifi);
  
  // Mettre à jour la visibilité des panneaux
  const dashboardPanel = $('panelDashboard');
  const dbVarsPanel = $('panelDbVars');
  const wifiPanel = $('panelWifi');
  
  if (dashboardPanel) {
    dashboardPanel.style.display = (isDb || isWifi) ? 'none' : 'block';
    dashboardPanel.hidden = (isDb || isWifi);
  }
  
  if (dbVarsPanel) {
    dbVarsPanel.style.display = isDb ? 'block' : 'none';
    dbVarsPanel.hidden = !isDb;
  }
  
  if (wifiPanel) {
    wifiPanel.style.display = isWifi ? 'block' : 'none';
    wifiPanel.hidden = !isWifi;
    
    // Charger les données WiFi quand l'onglet est ouvert
    if (isWifi) {
      refreshWifiStatus();
      loadSavedNetworks();
    }
  }
  
  console.log(`Onglet sélectionné: ${name}, isDb: ${isDb}, isWifi: ${isWifi}`);
  console.log('Dashboard panel hidden:', dashboardPanel ? dashboardPanel.hidden : 'N/A');
  console.log('DbVars panel hidden:', dbVarsPanel ? dbVarsPanel.hidden : 'N/A');
  console.log('Wifi panel hidden:', wifiPanel ? wifiPanel.hidden : 'N/A');
  
  if (isDb) {
    // Charger les réglages automatiquement
    loadDbVars();
    toast('Onglet Réglages sélectionné', 'info');
  }
}

// Fonction pour afficher les variables de configuration
function displayDbVars(db) {
    // Vérifier si les données sont valides (plus permissif)
    const dataOk = db && (db.ok === true || db.ok === undefined) && Object.keys(db).length > 0;
    ffp5csWsDebugLog('[DEBUG] Données valides:', dataOk, 'db.ok:', db.ok, 'keys count:', Object.keys(db).length);
    
    // Mise à jour de l'affichage des variables actuelles
    const updateDbElement = (id, value) => {
      const el = $(id);
      if (el) el.textContent = value;
    };
    
    // Utiliser les valeurs par défaut si les données ne sont pas valides
    // Harmonisation config: clés canoniques (chauffageThreshold, tempsRemplissageSec) avec fallback anciennes
    const defaultValues = {
      bouffeMatin: 8,
      bouffeMidi: 12,
      bouffeSoir: 19,
      tempsGros: 10,
      tempsPetits: 10,
      aqThreshold: 15,
      tankThreshold: 8,
      chauffageThreshold: 25.0,
      tempsRemplissageSec: 120,
      limFlood: 5,
      FreqWakeUp: 600,
      mail: 'Non configuré',
      mailNotif: ''
    };
    const heaterVal = (db.chauffageThreshold !== undefined && db.chauffageThreshold !== null) ? db.chauffageThreshold : db.heaterThreshold;
    const refillVal = (db.tempsRemplissageSec !== undefined && db.tempsRemplissageSec !== null) ? db.tempsRemplissageSec : db.refillDuration;

    // Helper pour convertir mailNotif (string "checked"/"") en boolean
    const isMailEnabled = (val) => val === 'checked' || val === '1' || val === 1 || val === true;

    updateDbElement('dbFeedMorning', dataOk && db.bouffeMatin !== undefined && db.bouffeMatin !== null ? db.bouffeMatin : defaultValues.bouffeMatin);
    updateDbElement('dbFeedNoon', dataOk && db.bouffeMidi !== undefined && db.bouffeMidi !== null ? db.bouffeMidi : defaultValues.bouffeMidi);
    updateDbElement('dbFeedEvening', dataOk && db.bouffeSoir !== undefined && db.bouffeSoir !== null ? db.bouffeSoir : defaultValues.bouffeSoir);
    updateDbElement('dbFeedBigDur', dataOk && db.tempsGros !== undefined && db.tempsGros !== null ? db.tempsGros : defaultValues.tempsGros);
    updateDbElement('dbFeedSmallDur', dataOk && db.tempsPetits !== undefined && db.tempsPetits !== null ? db.tempsPetits : defaultValues.tempsPetits);
    updateDbElement('dbAqThreshold', dataOk && db.aqThreshold !== undefined && db.aqThreshold !== null ? db.aqThreshold : defaultValues.aqThreshold);
    updateDbElement('dbTankThreshold', dataOk && db.tankThreshold !== undefined && db.tankThreshold !== null ? db.tankThreshold : defaultValues.tankThreshold);
    updateDbElement('dbHeaterThreshold', dataOk && (heaterVal !== undefined && heaterVal !== null) ? parseFloat(heaterVal).toFixed(1) : defaultValues.chauffageThreshold.toFixed(1));
    updateDbElement('dbRefillDuration', dataOk && (refillVal !== undefined && refillVal !== null) ? refillVal : defaultValues.tempsRemplissageSec);
    updateDbElement('dbFreqWakeUp', (db.FreqWakeUp ?? defaultValues.FreqWakeUp));
    updateDbElement('dbLimFlood', (db.limFlood ?? defaultValues.limFlood));
    const mailDisplay = (db.mail !== undefined && db.mail !== null && String(db.mail).trim() !== '')
      ? String(db.mail)
      : defaultValues.mail;
    updateDbElement('dbEmailAddress', mailDisplay);

    // Mise à jour du statut email - convertir mailNotif string en boolean, toujours afficher une valeur
    const emailEnabledEl = $('dbEmailEnabled');
    const emailEnabled = dataOk ? isMailEnabled(db.mailNotif) : false;
    if (emailEnabledEl) {
      emailEnabledEl.textContent = emailEnabled ? 'Activées' : 'Désactivées';
      emailEnabledEl.className = `badge ${emailEnabled ? 'bg-success' : 'bg-secondary'}`;
    }
    
    // Mise à jour du bouton de notifications dans la page contrôles (vert = actif, rouge = inactif, pas de ON/OFF)
    const btnEmailNotif = $('btnEmailNotif');
    if (btnEmailNotif) {
      btnEmailNotif.innerHTML = '🔔 Notifications Email';
      btnEmailNotif.className = emailEnabled ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
    }
    
    // Remplir automatiquement le formulaire avec les vraies valeurs (clés canoniques + fallback)
    const fields = {
      'formFeedMorning': dataOk && db.bouffeMatin !== undefined && db.bouffeMatin !== null ? db.bouffeMatin : defaultValues.bouffeMatin,
      'formFeedNoon': dataOk && db.bouffeMidi !== undefined && db.bouffeMidi !== null ? db.bouffeMidi : defaultValues.bouffeMidi,
      'formFeedEvening': dataOk && db.bouffeSoir !== undefined && db.bouffeSoir !== null ? db.bouffeSoir : defaultValues.bouffeSoir,
      'formFeedBigDur': dataOk && db.tempsGros !== undefined && db.tempsGros !== null ? db.tempsGros : defaultValues.tempsGros,
      'formFeedSmallDur': dataOk && db.tempsPetits !== undefined && db.tempsPetits !== null ? db.tempsPetits : defaultValues.tempsPetits,
      'formAqThreshold': dataOk && db.aqThreshold !== undefined && db.aqThreshold !== null ? db.aqThreshold : defaultValues.aqThreshold,
      'formTankThreshold': dataOk && db.tankThreshold !== undefined && db.tankThreshold !== null ? db.tankThreshold : defaultValues.tankThreshold,
      'formHeaterThreshold': dataOk && (heaterVal !== undefined && heaterVal !== null) ? heaterVal : defaultValues.chauffageThreshold,
      'formRefillDuration': dataOk && (refillVal !== undefined && refillVal !== null) ? refillVal : defaultValues.tempsRemplissageSec,
      'formFreqWakeUp': dataOk && db.FreqWakeUp !== undefined && db.FreqWakeUp !== null ? db.FreqWakeUp : defaultValues.FreqWakeUp,
      'formLimFlood': dataOk && db.limFlood !== undefined && db.limFlood !== null ? db.limFlood : defaultValues.limFlood,
      'formEmailAddress': dataOk && db.mail ? db.mail : ''
    };
    
    for (const [id, value] of Object.entries(fields)) {
      const el = $(id);
      if (el && value !== undefined && value !== null) {
        el.value = value;
      }
    }
    
    // Case à cocher email - utiliser la vraie valeur
    // L'activation email est maintenant gérée via le bouton dans controles.html
    // const emailCheckbox = $('formEmailEnabled');
    // if (emailCheckbox) {
    //   emailCheckbox.checked = dataOk && db.emailEnabled !== undefined ? !!db.emailEnabled : defaultValues.emailEnabled;
    // }
    
    // Mettre à jour l'indicateur de statut
  const statusBadge = $('dbvarsStatusBadge');
    if (statusBadge) {
      if (dataOk) {
        statusBadge.textContent = '✅ Données serveur';
        statusBadge.className = 'badge bg-success';
      } else {
        statusBadge.textContent = '⚠️ Valeurs par défaut';
        statusBadge.className = 'badge bg-warning text-dark';
      }
    }
  
  console.log('Réglages chargés:', dataOk ? 'avec succès' : 'avec valeurs par défaut');
  toast(dataOk ? 'Réglages chargés' : 'Réglages (valeurs par défaut)', dataOk ? 'success' : 'warning');
}

// Fonctions pour l'onglet Réglages
window.loadDbVars = async function loadDbVars() {
  ffp5csWsDebugLog('[DEBUG] loadDbVars() appelée');
  
  // OPTIMISATION: Utiliser le cache si disponible (chargement instantané)
  if (window.cachedDbVars) {
    ffp5csWsDebugLog('[DEBUG] Utilisation du cache pour affichage immédiat');
    displayDbVars(window.cachedDbVars);
    return;
  }
  
  // Afficher l'indicateur de chargement
  const loadingStatus = $('dbvarsLoadingStatus');
  const refreshBtn = $('btnRefreshDbVars');
  const statusIndicator = $('dbvarsStatusIndicator');
  const statusBadge = $('dbvarsStatusBadge');
  
  if (loadingStatus) loadingStatus.style.display = 'block';
  if (refreshBtn) refreshBtn.disabled = true;
  if (statusIndicator) statusIndicator.style.display = 'block';
  if (statusBadge) {
    statusBadge.textContent = 'Chargement...';
    statusBadge.className = 'badge bg-warning text-dark';
  }
  
  try {
    const controller = new AbortController();
    const timeoutId = setTimeout(() => controller.abort(), 5000); // Timeout 5s pour les requêtes distantes (optimisé)
    
    ffp5csWsDebugLog('[DEBUG] Envoi requête vers /dbvars');
    const response = await fetch('/dbvars', {
      cache: 'no-store',
      credentials: 'include',
      signal: controller.signal
    });
    
    clearTimeout(timeoutId);
    
    ffp5csWsDebugLog('[DEBUG] Réponse reçue:', response.status, response.statusText);
    
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    
    const responseText = await response.text();
    ffp5csWsDebugLog('[DEBUG] Contenu brut de la réponse:', responseText);
    
    const db = JSON.parse(responseText);
    ffp5csWsDebugLog('[DEBUG] JSON parsé:', db);
    // Mettre à jour le cache
    window.cachedDbVars = db;
    
    // Afficher les données
    displayDbVars(db);
  } catch (error) {
    ffp5csWsDebugError('[DEBUG] Erreur détaillée dans loadDbVars():', error);
    ffp5csWsDebugError('[DEBUG] Type d\'erreur:', error.name);
    ffp5csWsDebugError('[DEBUG] Message d\'erreur:', error.message);
    ffp5csWsDebugError('[DEBUG] Stack trace:', error.stack);
    
    if (error.name === 'AbortError') {
      ffp5csWsDebugLog('[DEBUG] Requête annulée (timeout)');
      toast('Timeout lors du chargement des réglages', 'warning');
    } else if (error.name === 'TypeError' && error.message.includes('fetch')) {
      ffp5csWsDebugLog('[DEBUG] Erreur réseau - connexion impossible');
      toast('Erreur réseau - impossible de charger les réglages', 'warning');
    } else {
      console.error('Erreur chargement réglages:', error);
      toast(`Erreur chargement réglages: ${error.message}`, 'error');
    }
    
    // Fallback avec des valeurs par défaut (clés canoniques)
    ffp5csWsDebugLog('[DEBUG] Application des valeurs par défaut');
    const defaultValues = {
      bouffeMatin: 8,
      bouffeMidi: 12,
      bouffeSoir: 19,
      tempsGros: 10,
      tempsPetits: 10,
      aqThreshold: 15,
      tankThreshold: 8,
      chauffageThreshold: 25.0,
      tempsRemplissageSec: 120,
      limFlood: 5,
      FreqWakeUp: 600,
      mail: 'Non configuré',
      mailNotif: ''
    };

    // Appliquer les valeurs par défaut
    const updateDbElement = (id, value) => {
      const el = $(id);
      if (el) el.textContent = value;
    };

    updateDbElement('dbFeedMorning', defaultValues.bouffeMatin);
    updateDbElement('dbFeedNoon', defaultValues.bouffeMidi);
    updateDbElement('dbFeedEvening', defaultValues.bouffeSoir);
    updateDbElement('dbFeedBigDur', defaultValues.tempsGros);
    updateDbElement('dbFeedSmallDur', defaultValues.tempsPetits);
    updateDbElement('dbAqThreshold', defaultValues.aqThreshold);
    updateDbElement('dbTankThreshold', defaultValues.tankThreshold);
    updateDbElement('dbHeaterThreshold', defaultValues.chauffageThreshold.toFixed(1));
    updateDbElement('dbRefillDuration', defaultValues.tempsRemplissageSec);
    updateDbElement('dbFreqWakeUp', defaultValues.FreqWakeUp);
    updateDbElement('dbLimFlood', defaultValues.limFlood);
    updateDbElement('dbEmailAddress', defaultValues.mail);
    
    // Mise à jour du statut email (mailNotif vide = désactivé)
    const emailEnabledEl = $('dbEmailEnabled');
    if (emailEnabledEl) {
      emailEnabledEl.textContent = 'Désactivées';
      emailEnabledEl.className = 'badge bg-secondary';
    }
    
    // Mise à jour du bouton de notifications dans la page contrôles (vert = actif, rouge = inactif)
    const btnEmailNotif = $('btnEmailNotif');
    if (btnEmailNotif) {
      btnEmailNotif.innerHTML = '🔔 Notifications Email';
      btnEmailNotif.className = 'btn btn-outline-danger w-100';
    }
    
    // Remplir le formulaire avec les valeurs par défaut en cas d'erreur
    const fields = {
      'formFeedMorning': defaultValues.bouffeMatin,
      'formFeedNoon': defaultValues.bouffeMidi,
      'formFeedEvening': defaultValues.bouffeSoir,
      'formFeedBigDur': defaultValues.tempsGros,
      'formFeedSmallDur': defaultValues.tempsPetits,
      'formAqThreshold': defaultValues.aqThreshold,
      'formTankThreshold': defaultValues.tankThreshold,
      'formHeaterThreshold': defaultValues.chauffageThreshold,
      'formRefillDuration': defaultValues.tempsRemplissageSec,
      'formFreqWakeUp': defaultValues.FreqWakeUp,
      'formLimFlood': defaultValues.limFlood,
      'formEmailAddress': ''
    };
    
    for (const [id, value] of Object.entries(fields)) {
      const el = $(id);
      if (el) {
        el.value = value;
      }
    }
    
    // Case à cocher email avec valeur par défaut
    // L'activation email est maintenant gérée via le bouton dans controles.html
    // const emailCheckbox = $('formEmailEnabled');
    // if (emailCheckbox) {
    //   emailCheckbox.checked = defaultValues.emailEnabled;
    // }
    
    toast('Valeurs par défaut appliquées', 'warning');
    
    // Mettre à jour l'indicateur de statut pour l'erreur
    if (statusBadge) {
      statusBadge.textContent = '❌ Erreur de connexion';
      statusBadge.className = 'badge bg-danger';
    }
  } finally {
    // Masquer l'indicateur de chargement et réactiver le bouton
    if (loadingStatus) loadingStatus.style.display = 'none';
    if (refreshBtn) refreshBtn.disabled = false;
  }
}


window.fillFormFromDb = async function fillFormFromDb() {
  try {
    const response = await fetch('/dbvars', { cache: 'no-store', credentials: 'include' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const db = await response.json();
    
    // Remplir le formulaire (clés canoniques + fallback)
    const heaterVal = db.chauffageThreshold !== undefined ? db.chauffageThreshold : db.heaterThreshold;
    const refillVal = db.tempsRemplissageSec !== undefined ? db.tempsRemplissageSec : db.refillDuration;
    const fields = {
      'formFeedMorning': db.bouffeMatin,
      'formFeedNoon': db.bouffeMidi,
      'formFeedEvening': db.bouffeSoir,
      'formFeedBigDur': db.tempsGros,
      'formFeedSmallDur': db.tempsPetits,
      'formAqThreshold': db.aqThreshold,
      'formTankThreshold': db.tankThreshold,
      'formHeaterThreshold': heaterVal,
      'formRefillDuration': refillVal,
      'formFreqWakeUp': db.FreqWakeUp,
      'formLimFlood': db.limFlood,
      'formEmailAddress': db.mail
    };
    
    for (const [id, value] of Object.entries(fields)) {
      const el = $(id);
      if (el && value !== undefined && value !== null) {
        el.value = value;
      }
    }
    
    // Case à cocher email
    // L'activation email est maintenant gérée via le bouton dans controles.html
    // const emailCheckbox = $('formEmailEnabled');
    // if (emailCheckbox) {
    //   emailCheckbox.checked = !!db.emailEnabled;
    // }
    
    toast('Formulaire rempli avec les valeurs actuelles', 'success');
  } catch (error) {
    console.error('Erreur remplissage formulaire:', error);
    toast('Erreur remplissage formulaire', 'error');
  }
}

window.submitDbVars = async function submitDbVars(ev) {
  ev.preventDefault();
  const status = $('dbvarsStatus');
  
  try {
    const params = new URLSearchParams();
    const add = (k, id) => { 
      const el = $(id); 
      if (el && el.value !== '') params.append(k, String(el.value)); 
    };
    
    // Collecter les paramètres du formulaire
    // NOTE: Les clés correspondent à ce que le serveur /dbvars/update attend (noms français)
    add('bouffeMatin', 'formFeedMorning');
    add('bouffeMidi', 'formFeedNoon');
    add('bouffeSoir', 'formFeedEvening');
    add('tempsGros', 'formFeedBigDur');
    add('tempsPetits', 'formFeedSmallDur');
    add('aqThreshold', 'formAqThreshold');
    add('tankThreshold', 'formTankThreshold');
    add('chauffageThreshold', 'formHeaterThreshold');
    add('tempsRemplissageSec', 'formRefillDuration');
    add('FreqWakeUp', 'formFreqWakeUp');
    add('limFlood', 'formLimFlood');

    const email = $('formEmailAddress');
    if (email && email.value) params.append('mail', email.value);

    // Envoyer les données
    const resp = await fetch('/dbvars/update', {
      method: 'POST',
      credentials: 'include',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString()
    });
    
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const js = await resp.json();
    
    if (status) {
      status.textContent = js.status === 'OK' ? '✅ Enregistré avec succès' : '❌ Erreur lors de l\'enregistrement';
      status.className = js.status === 'OK' ? 'text-success' : 'text-danger';
    }
    
    if (js.status === 'OK') {
      toast('Réglages mis à jour', 'success');
      // Recharger l'affichage des réglages
      setTimeout(() => loadDbVars(), 500);
    } else {
      toast('Erreur lors de la mise à jour', 'error');
    }
    
  } catch (error) {
    console.error('Erreur soumission formulaire:', error);
    if (status) {
      status.textContent = '❌ Erreur de connexion';
      status.className = 'text-danger';
    }
    toast('Erreur de connexion', 'error');
  }
}

// ========================================
// EDITION INLINE DES VARIABLES
// ========================================

// Event delegation pour les badges éditables - ciblage contentArea pour le contenu dynamique
// (page controles chargée via loadPage, les éléments .editable-var n'existent pas au chargement initial)
let editableVarsDelegationSetup = false;
function setupEditableVarsDelegation() {
  if (editableVarsDelegationSetup) return;
  editableVarsDelegationSetup = true;
  const contentArea = document.getElementById('contentArea');
  const target = contentArea || document;
  target.addEventListener('click', function(e) {
    const badge = e.target.closest('.editable-var');
    if (badge && typeof window.editVar === 'function') {
      window.editVar(badge);
    }
  });
}

// Transformer un badge en input éditable
window.editVar = function(badge) {
  // Éviter l'édition multiple simultanée
  if (badge.classList.contains('editing')) return;
  
  const key = badge.dataset.key;
  const type = badge.dataset.type || 'number';
  const currentValue = badge.textContent.trim();
  const originalClass = badge.className;
  const badgeId = badge.id;
  
  // Créer l'input
  const input = document.createElement('input');
  input.type = type === 'email' ? 'email' : 'number';
  input.value = currentValue === '--' ? '' : currentValue;
  input.className = 'form-control form-control-sm inline-edit';
  input.style.width = type === 'email' ? '180px' : '80px';
  input.style.display = 'inline-block';
  input.style.textAlign = 'center';
  
  // Ajouter min/max/step si définis
  if (badge.dataset.min) input.min = badge.dataset.min;
  if (badge.dataset.max) input.max = badge.dataset.max;
  if (badge.dataset.step) input.step = badge.dataset.step;
  
  // Stocker les infos pour la restauration
  input.dataset.originalClass = originalClass;
  input.dataset.badgeId = badgeId;
  input.dataset.key = key;
  input.dataset.type = type;
  if (badge.dataset.min) input.dataset.min = badge.dataset.min;
  if (badge.dataset.max) input.dataset.max = badge.dataset.max;
  if (badge.dataset.step) input.dataset.step = badge.dataset.step;
  
  // Flag pour éviter double save
  let saved = false;
  
  const doSave = async () => {
    if (saved) return;
    saved = true;
    await saveVar(input, key, currentValue);
  };
  
  const doCancel = () => {
    if (saved) return;
    saved = true;
    cancelEdit(input, currentValue);
  };
  
  input.onblur = () => setTimeout(doSave, 100); // Petit délai pour permettre Escape
  input.onkeydown = (e) => {
    if (e.key === 'Enter') {
      e.preventDefault();
      doSave();
    }
    if (e.key === 'Escape') {
      e.preventDefault();
      doCancel();
    }
  };
  
  // Remplacer le badge par l'input
  badge.replaceWith(input);
  input.focus();
  input.select();
};

// Sauvegarder la valeur modifiée
async function saveVar(input, key, originalValue) {
  const newValue = input.value.trim();
  
  // Si valeur inchangée ou vide, annuler
  if (newValue === '' || newValue === originalValue) {
    cancelEdit(input, originalValue);
    return;
  }
  
  // Recréer le badge
  const badge = document.createElement('span');
  badge.id = input.dataset.badgeId;
  badge.className = input.dataset.originalClass;
  badge.dataset.key = key;
  badge.dataset.type = input.dataset.type;
  if (input.dataset.min) badge.dataset.min = input.dataset.min;
  if (input.dataset.max) badge.dataset.max = input.dataset.max;
  if (input.dataset.step) badge.dataset.step = input.dataset.step;
  badge.onclick = function() { editVar(this); };
  badge.textContent = newValue;
  
  // Remplacer l'input par le badge
  input.replaceWith(badge);
  
  // Envoyer la mise à jour au serveur
  const params = new URLSearchParams();
  params.append(key, newValue);
  
  try {
    const resp = await fetch('/dbvars/update', {
      method: 'POST',
      credentials: 'include',
      headers: { 'Content-Type': 'application/x-www-form-urlencoded' },
      body: params.toString()
    });
    
    if (resp.ok) {
      const js = await resp.json();
      if (js.status === 'OK') {
        // Succès - flash vert
        badge.classList.add('save-success');
        setTimeout(() => badge.classList.remove('save-success'), 1500);
        toast(`${key} mis à jour`, 'success');
        
        // Mettre à jour le cache
        if (window.cachedDbVars) {
          window.cachedDbVars[key] = newValue;
        }
      } else {
        throw new Error('Server returned error');
      }
    } else {
      throw new Error(`HTTP ${resp.status}`);
    }
  } catch (e) {
    console.error('Erreur sauvegarde variable:', e);
    // Erreur - flash rouge et restaurer ancienne valeur
    badge.textContent = originalValue;
    badge.classList.add('save-error');
    setTimeout(() => badge.classList.remove('save-error'), 1500);
    toast(`Erreur: ${key} non mis à jour`, 'error');
  }
}

// Annuler l'édition et restaurer le badge
function cancelEdit(input, originalValue) {
  const badge = document.createElement('span');
  badge.id = input.dataset.badgeId;
  badge.className = input.dataset.originalClass;
  badge.dataset.key = input.dataset.key;
  badge.dataset.type = input.dataset.type;
  if (input.dataset.min) badge.dataset.min = input.dataset.min;
  if (input.dataset.max) badge.dataset.max = input.dataset.max;
  if (input.dataset.step) badge.dataset.step = input.dataset.step;
  badge.onclick = function() { editVar(this); };
  badge.textContent = originalValue;
  
  input.replaceWith(badge);
}

// FIX v11.31: Fonction d'initialisation exportée pour contrôle du timing
// Appelée explicitement après chargement des scripts (évite race condition DOMContentLoaded)
window.initializeDashboard = function initializeDashboard() {
  // Vérifier que pas déjà initialisé
  if (window.isInitialized) {
    console.log('[INIT] Dashboard déjà initialisé, skip');
    return;
  }
  window.isInitialized = true;
  
  console.log('[INIT] 🚀 Initialisation dashboard...');
  logInfo('Dashboard consolidé initialisé', null, 'INIT');
  
  setupEditableVarsDelegation();
  
  // Charger la version du firmware en parallèle
  const versionPromise = fetch('/version', { cache: 'no-store', credentials: 'include' })
    .then(r => r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status)))
    .then(j => {
      const v = j && j.version ? String(j.version) : '--';
      const el = $('fwVersion');
      if (el) el.textContent = v;
      try { document.title = `FFP5CS Dashboard v${v}`; } catch(e){}
    })
    .catch(e => console.warn('Version fetch failed:', e.message));
  
  // Initialiser l'historique
  addToHistory('Système initialisé', 'success');
  const initTimeEl = $('initTime');
  if (initTimeEl) {
    initTimeEl.textContent = new Date().toLocaleTimeString();
  }
  
  // Initialiser les graphiques
  initCharts();
  
  // CHARGEMENT OPTIMISÉ - WebSocket prioritaire, HTTP en fallback
  const immediateLoad = async () => {
    console.log('[INIT] Chargement optimisé - WebSocket prioritaire...');
    
    // Stratégie : Attendre le WebSocket d'abord (plus rapide), puis HTTP en fallback
    const loadWithWebSocketFallback = async () => {
      return new Promise((resolve) => {
        let resolved = false;
        
        // 1. Attendre les données WebSocket (priorité absolue)
        const wsTimeout = setTimeout(() => {
          if (!resolved) {
            console.log('[INIT] WebSocket timeout, passage au HTTP...');
            loadViaHTTP();
          }
        }, 3000); // 3s max pour WebSocket
        
        // 2. Écouter les données WebSocket
        const originalWsHandler = window.wsMessageHandler;
        window.wsMessageHandler = (msg) => {
          if (!resolved && msg && (msg.type === 'sensor_data' || msg.type === 'sensor_update')) {
            clearTimeout(wsTimeout);
            resolved = true;
            console.log('[INIT] ✅ Données capteurs reçues via WebSocket');
            updateSensorDisplay(msg);
            updateConnectionStatus(true);
            
            // Restaurer le handler original
            window.wsMessageHandler = originalWsHandler;
            resolve(true);
          }
        };
        
        // 3. Fallback HTTP si WebSocket échoue
        const loadViaHTTP = async () => {
          if (resolved) return;
          
          try {
            console.log('[INIT] Tentative de chargement HTTP...');
            const [sensorData, dbVars] = await Promise.allSettled([
              fetch('/json', {
                cache: 'no-store',
                credentials: 'include',
                signal: AbortSignal.timeout(3000)
              }).then(r => r.json()),
              fetch('/dbvars', {
                cache: 'no-store',
                credentials: 'include',
                signal: AbortSignal.timeout(3000)
              }).then(r => r.json())
            ]);
            
            // Affichage des données capteurs
            if (sensorData.status === 'fulfilled') {
              console.log('[INIT] ✅ Données capteurs chargées via HTTP');
              updateSensorDisplay(sensorData.value);
              updateConnectionStatus(true);
            } else {
              console.log('[INIT] ⚠️ Erreur chargement capteurs HTTP:', sensorData.reason);
              // Utiliser des données par défaut pour un affichage immédiat
              updateSensorDisplay({
                tempWater: 25.0, tempAir: 22.0, humidity: 65.0,
                wlAqua: 152.0, wlTank: 87.0, wlPota: 121.0,
                pumpAqua: false, pumpTank: false, heater: false, light: false
              });
              updateConnectionStatus(false);
            }
            
            // Chargement des variables de configuration
            if (dbVars.status === 'fulfilled') {
              console.log('[INIT] ✅ Variables BDD chargées via HTTP');
              window.cachedDbVars = dbVars.value;
            } else {
              console.log('[INIT] ⚠️ Erreur chargement BDD HTTP:', dbVars.reason);
              // NOTE: Les clés correspondent au serveur /dbvars (noms français), incl. Sync/Email
              window.cachedDbVars = {
                bouffeMatin: 8, bouffeMidi: 12, bouffeSoir: 19,
                tempsGros: 10, tempsPetits: 10,
                aqThreshold: 15, tankThreshold: 8, heaterThreshold: 25.0,
                tempsRemplissageSec: 120, limFlood: 5, FreqWakeUp: 600,
                mail: 'Non configuré', mailNotif: '', ok: false
              };
            }
            
            resolved = true;
            resolve(false);
            
          } catch (error) {
            console.error('[INIT] Erreur dans le chargement HTTP:', error);
            resolved = true;
            resolve(false);
          }
        };
      });
    };
    
    await loadWithWebSocketFallback();
  };
  
  // Exécution immédiate du chargement parallèle
  immediateLoad();
  
  // Connexion WebSocket avec fallback
  connectWS();
  
  // DIAGNOSTIC: Test de performance du serveur
  setTimeout(async () => {
    try {
      const start = performance.now();
      const response = await fetch('/server-status', {
        cache: 'no-store',
        credentials: 'include',
        signal: AbortSignal.timeout(3000)
      });
      const end = performance.now();
      
      if (response.ok) {
        const data = await response.json();
        console.log(`[DIAGNOSTIC] Serveur status - Latence: ${(end-start).toFixed(0)}ms, Heap: ${data.heapFree} bytes`);
        
        // Avertissement si latence élevée
        if (end - start > 1000) {
          console.warn(`[DIAGNOSTIC] ⚠️ Latence serveur élevée: ${(end-start).toFixed(0)}ms`);
        }
      }
    } catch (error) {
      console.warn('[DIAGNOSTIC] Impossible de tester les performances du serveur:', error.message);
    }
  }, 5000); // Test après 5 secondes
  
  // CORRECTION: Mettre le statut à "en ligne" par défaut si on peut charger la page
  // Cela évite d'afficher "hors ligne" quand l'appareil est en fait connecté
  setTimeout(() => {
    if (!wsConnected && !isOnline) {
      console.log('[Status] WebSocket non connecté, vérification de la connectivité HTTP...');
      // Tester une requête simple pour vérifier la connectivité
      fetch('/json', {
        method: 'HEAD',
        cache: 'no-store',
        credentials: 'include',
        signal: AbortSignal.timeout(3000)
      }).then(() => {
        console.log('[Status] Connectivité HTTP confirmée, mise à jour du statut');
        updateConnectionStatus(true);
      }).catch((error) => {
        console.log('[Status] Pas de connectivité HTTP:', error.message);
        // Le statut reste "hors ligne" seulement si vraiment pas de connexion
      });
    }
  }, 2000); // Attendre 2 secondes pour laisser le temps aux WebSockets de se connecter
  
  // OPTIMISATION: Démarrer le polling avec un intervalle plus long
  // Le WebSocket prend le relais pour les mises à jour temps réel
  if (!pollInterval) {
    pollInterval = setInterval(refresh, 3000); // 3 secondes pour meilleure réactivité en fallback
  }
  
  // Test de connectivité WebSocket avant le fallback
  setTimeout(async () => {
    if (!wsConnected) {
      // Tester la connectivité WebSocket directement plutôt que HTTP
      try {
        const testWs = new WebSocket(`ws://${location.hostname}:81/ws`);
        const testTimeout = setTimeout(() => {
          testWs.close();
        }, 2000);
        
        testWs.onopen = () => {
          clearTimeout(testTimeout);
          testWs.close();
          console.log('Port 81 WebSocket accessible');
        };
        
        testWs.onerror = () => {
          clearTimeout(testTimeout);
          console.log('Port 81 WebSocket non accessible');
        };
      } catch (error) {
        console.log('Port 81 WebSocket non accessible:', error.message);
      }
      
      startPolling();
      toast('Mode polling activé (WebSocket indisponible)', 'warning');
      console.log('Mode polling activé - WebSocket non disponible');
    }
  }, 3000);
  
  // Gestion du redimensionnement de la fenêtre pour les graphiques
  let resizeTimeout;
  window.addEventListener('resize', () => {
    clearTimeout(resizeTimeout);
    resizeTimeout = setTimeout(() => {
      resizeCharts();
    }, 150); // Délai pour éviter trop de redimensionnements
  });

  // Raccourcis clavier optimisés
  document.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey) {
      switch(e.key) {
        case 'r':
          e.preventDefault();
          refresh();
          toast('Données actualisées', 'info');
          break;
        case '1':
          e.preventDefault();
          selectTab('Dashboard');
          break;
        case '2':
          e.preventDefault();
          selectTab('DbVars');
          break;
        case 'f':
          e.preventDefault();
          // Focus sur le premier bouton d'action
          const firstBtn = document.querySelector('.btn');
          if (firstBtn) firstBtn.focus();
          break;
      }
    }
  });
  
  // Attendre la version avant d'afficher le toast final
  Promise.all([versionPromise]).then(() => {
    toast('Dashboard FFP5CS chargé avec succès', 'success');
  });
  
  // Enregistrer le Service Worker pour PWA
  if ('serviceWorker' in navigator) {
    navigator.serviceWorker.register('/sw.js')
      .then((registration) => {
        console.log('[SW] Service Worker enregistré:', registration.scope);
        // Vérifier les mises à jour
        registration.addEventListener('updatefound', () => {
          toast('Mise à jour disponible', 'info');
        });
      })
      .catch((error) => {
        console.error('[SW] Erreur enregistrement Service Worker:', error);
      });
  }
  
  // Ne pas précharger /diag (page protégée : prefetch n'envoie pas les cookies → 401)

  // Délégation d'événements WiFi (évite injection SSID/password dans onclick)
  document.addEventListener('click', (e) => {
    const btn = e.target.closest('[data-action]');
    if (!btn) return;
    const action = btn.getAttribute('data-action');
    const ssid = btn.getAttribute('data-ssid') || '';
    if (action === 'connect-saved') {
      const password = btn.getAttribute('data-password') || '';
      connectToSavedNetwork(ssid, password);
    } else if (action === 'remove-saved') {
      removeSavedNetwork(ssid);
    } else if (action === 'fill-form') {
      fillWifiForm(ssid);
    }
  });
  
  console.log('[INIT] ✅ Initialisation dashboard terminée');
};

// ========================================
// GESTIONNAIRE WIFI - FONCTIONS JAVASCRIPT
// ========================================

// Actualiser le statut WiFi
window.refreshWifiStatus = async function refreshWifiStatus() {
  try {
    const response = await fetch('/wifi/status', { credentials: 'include' });
    const data = await response.json();
    
    // Mettre à jour le statut STA dans l'onglet WiFi
    // Clés JSON harmonisées avec le serveur: wifiSta*, wifiAp*
    // IDs HTML harmonisés avec wifi.html: wifiStaStatus, wifiStaSSID, etc.
    const staStatus = $('wifiStaStatus');
    const staSSID = $('wifiStaSSID');
    const staIP = $('wifiStaIP');
    const staRSSI = $('wifiStaRSSI');
    
    if (data.wifiStaConnected) {
      if (staStatus) {
        staStatus.textContent = 'Connecté';
        staStatus.className = 'badge bg-success';
      }
      if (staSSID) staSSID.textContent = data.wifiStaSSID || '--';
      if (staIP) staIP.textContent = data.wifiStaIP || '--';
      if (staRSSI) staRSSI.textContent = (data.wifiStaRSSI != null ? `${data.wifiStaRSSI} dBm` : '--');
    } else {
      if (staStatus) {
        staStatus.textContent = 'Déconnecté';
        staStatus.className = 'badge bg-danger';
      }
      if (staSSID) staSSID.textContent = '--';
      if (staIP) staIP.textContent = '--';
      if (staRSSI) staRSSI.textContent = '--';
    }
    
    // Mettre à jour le statut AP dans l'onglet WiFi
    const apStatus = $('wifiApStatus');
    const apSSID = $('wifiApSSID');
    const apIP = $('wifiApIP');
    const apClients = $('wifiApClients');
    
    if (data.wifiApActive) {
      if (apStatus) {
        apStatus.textContent = 'Actif';
        apStatus.className = 'badge bg-info';
      }
      if (apSSID) apSSID.textContent = data.wifiApSSID || '--';
      if (apIP) apIP.textContent = data.wifiApIP || '--';
      if (apClients) apClients.textContent = (data.wifiApClients != null ? String(data.wifiApClients) : '--');
    } else {
      if (apStatus) {
        apStatus.textContent = 'Inactif';
        apStatus.className = 'badge bg-secondary';
      }
      if (apSSID) apSSID.textContent = '--';
      if (apIP) apIP.textContent = '--';
      if (apClients) apClients.textContent = '--';
    }
    
    // Mode WiFi (STA / AP / les deux)
    const modeEl = $('wifiMode');
    if (modeEl) {
      const sta = !!data.wifiStaConnected;
      const ap = !!data.wifiApActive;
      if (sta && ap) {
        modeEl.textContent = 'Client + Point d\'accès';
        modeEl.className = 'badge bg-primary';
      } else if (sta) {
        modeEl.textContent = 'Client (STA)';
        modeEl.className = 'badge bg-success';
      } else if (ap) {
        modeEl.textContent = 'Point d\'accès (AP)';
        modeEl.className = 'badge bg-info';
      } else {
        modeEl.textContent = 'Déconnecté';
        modeEl.className = 'badge bg-secondary';
      }
    }
    
    // Synchroniser le cache WiFi pour que les mises à jour WebSocket périodiques
    // n'écrasent pas l'affichage avec '--'
    if (typeof window.syncLastWifiData === 'function') window.syncLastWifiData(data);
    
  } catch (error) {
    console.error('Erreur lors de la récupération du statut WiFi:', error);
    toast('Erreur lors de la récupération du statut WiFi', 'error');
  }
}

// Charger les réseaux sauvegardés
window.loadSavedNetworks = async function loadSavedNetworks() {
  const savedList = $('savedNetworksList');
  savedList.innerHTML = '<div class="text-center text-muted"><div class="spinner-border spinner-border-sm" role="status"><span class="visually-hidden">Chargement...</span></div> Chargement des réseaux...</div>';
  
  try {
    const response = await fetch('/wifi/saved', { credentials: 'include' });
    const data = await response.json();
    
    if (data.success && data.networks.length > 0) {
      let html = '';
      data.networks.forEach(network => {
        const isStatic = network.source === 'static';
        const sourceBadge = isStatic ? 
          '<span class="badge bg-info me-2">Statique</span>' : 
          '<span class="badge bg-secondary me-2">Sauvegardé</span>';
        
        const deleteButton = isStatic ? 
          '<button class="btn btn-outline-secondary btn-sm" disabled title="Réseau statique - non supprimable">🗑️ Supprimer</button>' :
          `<button class="btn btn-outline-danger btn-sm" data-action="remove-saved" data-ssid="${escapeForAttr(network.ssid)}">🗑️ Supprimer</button>`;
        
        html += `
          <div class="list-group-item d-flex justify-content-between align-items-center">
            <div>
              ${sourceBadge}
              <strong>${escapeHtml(network.ssid)}</strong>
              <br><small class="text-muted">Mot de passe: ${network.password ? '••••••••' : 'Aucun'}</small>
            </div>
            <div class="btn-group" role="group">
              <button class="btn btn-outline-primary btn-sm" data-action="connect-saved" data-ssid="${escapeForAttr(network.ssid)}" data-password="${escapeForAttr(network.password || '')}">
                🔗 Connecter
              </button>
              ${deleteButton}
            </div>
          </div>
        `;
      });
      savedList.innerHTML = html;
    } else {
      savedList.innerHTML = '<div class="text-center text-muted">Aucun réseau sauvegardé</div>';
    }
  } catch (error) {
    console.error('Erreur lors du chargement des réseaux sauvegardés:', error);
    savedList.innerHTML = '<div class="text-center text-danger">Erreur lors du chargement</div>';
    toast('Erreur lors du chargement des réseaux sauvegardés', 'error');
  }
}

// Scanner les réseaux WiFi disponibles
window.scanWifiNetworks = async function scanWifiNetworks() {
  const availableList = $('availableNetworksList');
  availableList.innerHTML = '<div class="text-center text-muted"><div class="spinner-border spinner-border-sm" role="status"><span class="visually-hidden">Scan en cours...</span></div> Scan en cours...</div>';
  
  try {
    const response = await fetch('/wifi/scan', { credentials: 'include' });
    const data = await response.json();
    
    if (data.success && data.networks.length > 0) {
      let html = '';
      data.networks.forEach(network => {
        const rssiClass = network.rssi > -50 ? 'text-success' : network.rssi > -70 ? 'text-warning' : 'text-danger';
        const encryptionIcon = network.encryption === 'open' ? '🔓' : '🔒';
        
        html += `
          <div class="list-group-item d-flex justify-content-between align-items-center">
            <div>
              <strong>${escapeHtml(network.ssid)}</strong>
              <br><small class="text-muted">${encryptionIcon} ${network.encryption} • Canal ${network.channel}</small>
            </div>
            <div>
              <span class="badge ${rssiClass}">${network.rssi} dBm</span>
              <button class="btn btn-outline-primary btn-sm ms-2" data-action="fill-form" data-ssid="${escapeForAttr(network.ssid)}">
                🔗 Sélectionner
              </button>
            </div>
          </div>
        `;
      });
      availableList.innerHTML = html;
    } else {
      availableList.innerHTML = '<div class="text-center text-muted">Aucun réseau trouvé</div>';
    }
  } catch (error) {
    console.error('Erreur lors du scan WiFi:', error);
    availableList.innerHTML = '<div class="text-center text-danger">Erreur lors du scan</div>';
    toast('Erreur lors du scan des réseaux WiFi', 'error');
  }
}

// Connecter à un réseau WiFi
window.connectToWifi = async function connectToWifi(event) {
  event.preventDefault();
  
  const ssid = $('wifiSSID').value.trim();
  const password = $('wifiPassword').value;
  const save = $('wifiSave').checked;
  const statusDiv = $('wifiConnectStatus');
  
  if (!ssid) {
    toast('Veuillez saisir un nom de réseau', 'error');
    return;
  }
  
  statusDiv.innerHTML = '<div class="spinner-border spinner-border-sm" role="status"><span class="visually-hidden">Connexion...</span></div> Tentative de connexion en cours...';
  
  // Marquer qu'un changement de WiFi est en cours
  window.wifiChangePending = true;
  
  try {
    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('password', password);
    formData.append('save', save ? 'true' : 'false');
    
    const response = await fetch('/wifi/connect', {
      method: 'POST',
      credentials: 'include',
      body: formData,
      // Timeout aligné avec le serveur (15s + marge)
      signal: AbortSignal.timeout(18000)
    });
    
    const data = await response.json();
    
    if (data.success) {
      // La connexion est en cours, afficher un message de progression
      statusDiv.innerHTML = `<div class="alert alert-info">
        <div class="spinner-border spinner-border-sm" role="status"><span class="visually-hidden">Connexion...</span></div>
        🔄 Connexion à <strong>${escapeHtml(data.ssid)}</strong> en cours...<br>
        <small>La page se reconnectera automatiquement dans quelques instants.</small>
      </div>`;
      
      toast(`Connexion à ${data.ssid} en cours...`, 'info');
      
      // Attendre un peu puis essayer de se reconnecter au WebSocket
      let reconnectAttempts = 0;
      const maxAttempts = 20; // 20 tentatives sur 20 secondes
      
      const checkConnection = () => {
        reconnectAttempts++;
        
        // Essayer de refaire une requête pour vérifier la connexion
        fetch('/json', {
          method: 'GET',
          cache: 'no-cache',
          credentials: 'include',
          signal: AbortSignal.timeout(2000)
        })
        .then(response => response.json())
        .then(jsonData => {
          // Connexion réussie !
          if (jsonData.wifiStaConnected && jsonData.wifiStaSSID === ssid) {
            window.wifiChangePending = false; // Réinitialiser le flag
            statusDiv.innerHTML = `<div class="alert alert-success">
              ✅ Connecté avec succès à <strong>${escapeHtml(jsonData.wifiStaSSID)}</strong><br>
              IP: ${escapeHtml(jsonData.wifiStaIP)} • RSSI: ${jsonData.wifiStaRSSI} dBm
            </div>`;
            toast(`Connecté à ${jsonData.wifiStaSSID}`, 'success');
            
            // Actualiser les données
            setTimeout(() => {
              refreshWifiStatus();
              loadSavedNetworks();
              clearWifiForm();
              
              // Reconnecter le WebSocket si nécessaire
              connectWS();
            }, 1000);
          } else if (reconnectAttempts < maxAttempts) {
            // Pas encore connecté au bon réseau, réessayer
            statusDiv.innerHTML = `<div class="alert alert-info">
              <div class="spinner-border spinner-border-sm" role="status"></div>
              🔄 Connexion en cours... (tentative ${reconnectAttempts}/${maxAttempts})
            </div>`;
            setTimeout(checkConnection, 1000);
          } else {
            // Timeout
            window.wifiChangePending = false; // Réinitialiser le flag
            statusDiv.innerHTML = `<div class="alert alert-warning">
              ⚠️ Connexion au réseau prend plus de temps que prévu.<br>
              Vérifiez l'état de la connexion manuellement.
            </div>`;
            toast('Connexion au réseau prend du temps', 'warning');
            
            // Essayer de rafraîchir quand même
            setTimeout(() => {
              refreshWifiStatus();
              loadSavedNetworks();
            }, 2000);
          }
        })
        .catch(error => {
          if (reconnectAttempts < maxAttempts) {
            // Erreur réseau, l'ESP32 est peut-être en train de changer de réseau
            statusDiv.innerHTML = `<div class="alert alert-info">
              <div class="spinner-border spinner-border-sm" role="status"></div>
              🔄 ESP32 en cours de reconnexion... (${reconnectAttempts}/${maxAttempts})
            </div>`;
            setTimeout(checkConnection, 1000);
          } else {
            // Timeout définitif
            window.wifiChangePending = false; // Réinitialiser le flag
            statusDiv.innerHTML = `<div class="alert alert-danger">
              ❌ Impossible de se reconnecter à l'ESP32.<br>
              <small>Vérifiez que l'ESP32 est accessible sur le réseau.</small>
            </div>`;
            toast('Échec de reconnexion à l\'ESP32', 'error');
          }
        });
      };
      
      // Attendre 2 secondes avant de commencer à vérifier (laisser le temps à l'ESP32 de se déconnecter)
      setTimeout(checkConnection, 2000);
      
    } else {
      window.wifiChangePending = false; // Réinitialiser le flag
      statusDiv.innerHTML = `<div class="alert alert-danger">❌ Échec: ${escapeHtml(data.error)}</div>`;
      toast(`Échec: ${data.error}`, 'error');
    }
  } catch (error) {
    console.error('Erreur lors de la connexion WiFi:', error);
    
    // Distinguer les différents types d'erreurs
    if (error.name === 'AbortError' || error.name === 'TimeoutError') {
      statusDiv.innerHTML = `<div class="alert alert-info">
        <div class="spinner-border spinner-border-sm" role="status"></div>
        🔄 ESP32 en cours de connexion au réseau WiFi...<br>
        <small>Cette opération peut prendre jusqu'à 15 secondes. Veuillez patienter.</small>
      </div>`;
      toast('Connexion WiFi en cours...', 'info');
      
      // Lancer automatiquement la vérification de connexion avec détection IP
      setTimeout(async () => {
        let attempts = 0;
        const maxAttempts = 12; // 12 tentatives sur 24 secondes (2s par tentative)
        
        // D'abord essayer de détecter la nouvelle IP
        try {
          const newIP = await detectNewESP32IP();
          if (newIP) {
            console.log(`🔍 Nouvelle IP ESP32 détectée: ${newIP}`);
            window.location.hostname = newIP;
          }
        } catch (error) {
          console.log('🔍 Détection IP automatique échouée, continuation avec IP actuelle');
        }
        
        const checkConnection = () => {
          attempts++;
          fetch('/json', {
            method: 'GET',
            cache: 'no-cache',
            credentials: 'include',
            signal: AbortSignal.timeout(3000)
          })
          .then(response => response.json())
          .then(jsonData => {
            if (jsonData.wifiStaConnected && jsonData.wifiStaSSID === ssid) {
              window.wifiChangePending = false;
              statusDiv.innerHTML = `<div class="alert alert-success">
                ✅ Connecté avec succès à <strong>${escapeHtml(jsonData.wifiStaSSID)}</strong><br>
                IP: ${escapeHtml(jsonData.wifiStaIP)} • RSSI: ${jsonData.wifiStaRSSI} dBm
              </div>`;
              toast(`Connecté à ${jsonData.wifiStaSSID}`, 'success');
              setTimeout(() => {
                refreshWifiStatus();
                loadSavedNetworks();
                clearWifiForm();
                connectWS();
              }, 1000);
            } else if (attempts < maxAttempts) {
              statusDiv.innerHTML = `<div class="alert alert-info">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                🔄 Connexion en cours... (${attempts}/${maxAttempts})
              </div>`;
              setTimeout(checkConnection, 2000);
            } else {
              window.wifiChangePending = false;
      statusDiv.innerHTML = `<div class="alert alert-warning">
                ⚠️ La connexion prend plus de temps que prévu.<br>
                <small>Vérifiez manuellement l'état de la connexion WiFi.</small>
      </div>`;
              toast('Connexion prolongée', 'warning');
              setTimeout(() => {
                refreshWifiStatus();
                loadSavedNetworks();
              }, 2000);
            }
          })
          .catch(() => {
            if (attempts < maxAttempts) {
              statusDiv.innerHTML = `<div class="alert alert-info">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                🔄 ESP32 en cours de reconnexion... (${attempts}/${maxAttempts})
              </div>`;
              setTimeout(checkConnection, 2000);
            } else {
              window.wifiChangePending = false;
              statusDiv.innerHTML = `<div class="alert alert-danger">
                ❌ Impossible de se reconnecter à l'ESP32.<br>
                <small>Vérifiez que l'ESP32 est accessible sur le réseau.</small>
              </div>`;
              toast('Échec de reconnexion', 'error');
            }
          });
        };
        checkConnection();
      }, 3000);
    } else if (error.message.includes('Failed to fetch') || error.message.includes('NetworkError')) {
      // Erreur réseau - l'ESP32 est probablement en train de changer de réseau
      statusDiv.innerHTML = `<div class="alert alert-info">
        <div class="spinner-border spinner-border-sm" role="status"></div>
        🔄 ESP32 en cours de reconnexion au nouveau réseau...<br>
        <small>Cette page se reconnectera automatiquement.</small>
      </div>`;
      toast('Reconnexion en cours...', 'info');
      
      // Lancer la vérification de connexion après un délai
      setTimeout(() => {
        let attempts = 0;
        const checkReconnect = () => {
          attempts++;
        fetch('/json', {
          method: 'GET',
          cache: 'no-cache',
          credentials: 'include',
          signal: AbortSignal.timeout(2000)
        })
          .then(response => response.json())
          .then(jsonData => {
            window.wifiChangePending = false; // Réinitialiser le flag
            statusDiv.innerHTML = `<div class="alert alert-success">✅ Reconnexion réussie !</div>`;
            toast('Reconnecté à l\'ESP32', 'success');
            setTimeout(() => {
              refreshWifiStatus();
              loadSavedNetworks();
              clearWifiForm();
              connectWS();
            }, 1000);
          })
          .catch(() => {
            if (attempts < 15) {
              statusDiv.innerHTML = `<div class="alert alert-info">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                🔄 Attente de reconnexion... (${attempts}/15)
              </div>`;
              setTimeout(checkReconnect, 2000);
            } else {
              window.wifiChangePending = false; // Réinitialiser le flag
              statusDiv.innerHTML = `<div class="alert alert-danger">
                ❌ Impossible de se reconnecter à l'ESP32.
              </div>`;
              toast('Échec de reconnexion', 'error');
            }
          });
        };
        checkReconnect();
      }, 3000);
    } else {
      window.wifiChangePending = false; // Réinitialiser le flag
      statusDiv.innerHTML = `<div class="alert alert-danger">❌ Erreur: ${escapeHtml(error.message)}</div>`;
      toast('Erreur lors de la connexion', 'error');
    }
  }
}

// Connecter à un réseau sauvegardé (connexion asynchrone comme connectToWifi)
window.connectToSavedNetwork = async function connectToSavedNetwork(ssid, password) {
  const statusDiv = $('wifiConnectStatus');
  statusDiv.innerHTML = '<div class="spinner-border spinner-border-sm" role="status"><span class="visually-hidden">Connexion...</span></div> Tentative de connexion en cours...';

  window.wifiChangePending = true;

  try {
    const formData = new FormData();
    formData.append('ssid', ssid);
    formData.append('password', password || '');
    formData.append('save', 'false');

    const response = await fetch('/wifi/connect', {
      method: 'POST',
      credentials: 'include',
      body: formData,
      signal: AbortSignal.timeout(18000)
    });

    const data = await response.json();

    if (data.success) {
      statusDiv.innerHTML = `<div class="alert alert-info">
        <div class="spinner-border spinner-border-sm" role="status"></div>
        🔄 Connexion à <strong>${escapeHtml(data.ssid)}</strong> en cours...<br>
        <small>La page se reconnectera automatiquement dans quelques instants.</small>
      </div>`;
      toast(`Connexion à ${data.ssid} en cours...`, 'info');

      let attempts = 0;
      const maxAttempts = 20;

      const checkConnection = () => {
        attempts++;
        fetch('/json', { method: 'GET', cache: 'no-cache', credentials: 'include', signal: AbortSignal.timeout(2000) })
          .then(r => r.json())
          .then(jsonData => {
            if (jsonData.wifiStaConnected && jsonData.wifiStaSSID === ssid) {
              window.wifiChangePending = false;
              statusDiv.innerHTML = `<div class="alert alert-success">
                ✅ Connecté avec succès à <strong>${escapeHtml(jsonData.wifiStaSSID)}</strong><br>
                IP: ${escapeHtml(jsonData.wifiStaIP)} • RSSI: ${jsonData.wifiStaRSSI} dBm
              </div>`;
              toast(`Connecté à ${data.ssid}`, 'success');
              setTimeout(() => {
                refreshWifiStatus();
                loadSavedNetworks();
                connectWS();
              }, 1000);
            } else if (attempts < maxAttempts) {
              statusDiv.innerHTML = `<div class="alert alert-info">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                🔄 Connexion en cours... (${attempts}/${maxAttempts})
              </div>`;
              setTimeout(checkConnection, 1000);
            } else {
              window.wifiChangePending = false;
              statusDiv.innerHTML = `<div class="alert alert-warning">
                ⚠️ Connexion au réseau prend plus de temps que prévu.<br>
                Vérifiez l'état de la connexion manuellement.
              </div>`;
              toast('Connexion au réseau prend du temps', 'warning');
              setTimeout(() => {
                refreshWifiStatus();
                loadSavedNetworks();
              }, 2000);
            }
          })
          .catch(() => {
            if (attempts < maxAttempts) {
              statusDiv.innerHTML = `<div class="alert alert-info">
                <div class="spinner-border spinner-border-sm" role="status"></div>
                🔄 ESP32 en cours de reconnexion... (${attempts}/${maxAttempts})
              </div>`;
              setTimeout(checkConnection, 1000);
            } else {
              window.wifiChangePending = false;
              statusDiv.innerHTML = `<div class="alert alert-danger">
                ❌ Impossible de se reconnecter à l'ESP32.<br>
                <small>Vérifiez que l'ESP32 est accessible sur le réseau.</small>
              </div>`;
              toast('Échec de reconnexion à l\'ESP32', 'error');
            }
          });
      };

      setTimeout(checkConnection, 2000);
    } else {
      window.wifiChangePending = false;
      statusDiv.innerHTML = `<div class="alert alert-danger">❌ Échec: ${escapeHtml(data.error || 'Erreur inconnue')}</div>`;
      toast(`Échec: ${data.error}`, 'error');
    }
  } catch (error) {
    window.wifiChangePending = false;
    console.error('Erreur lors de la connexion WiFi:', error);
    if (error.name === 'AbortError' || error.name === 'TimeoutError') {
      statusDiv.innerHTML = `<div class="alert alert-info">
        <div class="spinner-border spinner-border-sm" role="status"></div>
        🔄 ESP32 en cours de connexion au réseau WiFi...<br>
        <small>Cette opération peut prendre jusqu'à 15 secondes.</small>
      </div>`;
      toast('Connexion WiFi en cours...', 'info');
      setTimeout(() => {
        let attempts = 0;
        const check = () => {
          attempts++;
          fetch('/json', { method: 'GET', cache: 'no-cache', credentials: 'include', signal: AbortSignal.timeout(3000) })
            .then(r => r.json())
            .then(jsonData => {
              if (jsonData.wifiStaConnected && jsonData.wifiStaSSID === ssid) {
                window.wifiChangePending = false;
                statusDiv.innerHTML = `<div class="alert alert-success">
                  ✅ Connecté à <strong>${escapeHtml(jsonData.wifiStaSSID)}</strong><br>
                  IP: ${escapeHtml(jsonData.wifiStaIP)} • RSSI: ${jsonData.wifiStaRSSI} dBm
                </div>`;
                toast(`Connecté à ${ssid}`, 'success');
                setTimeout(() => { refreshWifiStatus(); loadSavedNetworks(); connectWS(); }, 1000);
              } else if (attempts < 12) {
                statusDiv.innerHTML = `<div class="alert alert-info"><div class="spinner-border spinner-border-sm"></div> Connexion... (${attempts}/12)</div>`;
                setTimeout(check, 2000);
              } else {
                window.wifiChangePending = false;
                statusDiv.innerHTML = `<div class="alert alert-warning">⚠️ La connexion prend plus de temps que prévu.</div>`;
                setTimeout(() => { refreshWifiStatus(); loadSavedNetworks(); }, 2000);
              }
            })
            .catch(() => {
              if (attempts < 12) {
                statusDiv.innerHTML = `<div class="alert alert-info"><div class="spinner-border spinner-border-sm"></div> Reconnexion... (${attempts}/12)</div>`;
                setTimeout(check, 2000);
              } else {
                window.wifiChangePending = false;
                statusDiv.innerHTML = `<div class="alert alert-danger">❌ Impossible de se reconnecter à l'ESP32.</div>`;
              }
            });
        };
        check();
      }, 3000);
    } else {
      statusDiv.innerHTML = `<div class="alert alert-danger">❌ Erreur: ${escapeHtml(error.message)}</div>`;
      toast('Erreur lors de la connexion', 'error');
    }
  }
}

// Supprimer un réseau sauvegardé
window.removeSavedNetwork = async function removeSavedNetwork(ssid) {
  // Vérifier si c'est un réseau statique (ne devrait pas arriver grâce au bouton désactivé)
  if (!confirm(`Êtes-vous sûr de vouloir supprimer le réseau "${ssid}" ?`)) {
    return;
  }
  
  try {
    const formData = new FormData();
    formData.append('ssid', ssid);
    
    const response = await fetch('/wifi/remove', {
      method: 'POST',
      credentials: 'include',
      body: formData
    });
    
    const data = await response.json();
    
    if (data.success) {
      toast(`Réseau "${ssid}" supprimé`, 'success');
      loadSavedNetworks(); // Recharger la liste
    } else {
      toast(`Erreur lors de la suppression: ${data.error}`, 'error');
    }
  } catch (error) {
    console.error('Erreur lors de la suppression du réseau:', error);
    toast('Erreur lors de la suppression du réseau', 'error');
  }
}

// Remplir le formulaire avec un réseau scanné
window.fillWifiForm = function fillWifiForm(ssid) {
  $('wifiSSID').value = ssid;
  $('wifiPassword').focus();
}

// Effacer le formulaire WiFi
window.clearWifiForm = function clearWifiForm() {
  $('wifiSSID').value = '';
  $('wifiPassword').value = '';
  $('wifiConnectStatus').innerHTML = '';
}

// Fonction utilitaire pour échapper le HTML
function escapeHtml(text) {
  const div = document.createElement('div');
  div.textContent = text;
  return div.innerHTML;
}

// Échapper pour usage dans attributs HTML data-* (évite XSS dans onclick)
function escapeForAttr(str) {
  if (str == null) return '';
  return String(str)
    .replace(/&/g, '&amp;')
    .replace(/"/g, '&quot;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');
}