// FFP3 Dashboard - Fonctions communes et utilitaires

// Dashboard JavaScript consolidé et optimisé pour FFP3
let tempChart, waterChart;
let isOnline = false;
let ws = null;
let wsConnected = false;
let pollInterval = null;
let wsPingInterval = null;
let actionHistory = [];
const maxHistoryItems = 10;
let isInitialized = false;
let lastDataUpdate = 0;
const DATA_UPDATE_THROTTLE = 1000; // Throttle les mises à jour à 1s max

// Cache des dernières valeurs WiFi connues pour éviter qu'elles disparaissent
// Clés harmonisées avec les endpoints /json et /wifi/status
let lastWifiData = {
  wifiStaConnected: false,
  wifiStaSSID: '--',
  wifiStaIP: '--',
  wifiStaRSSI: '--',
  wifiApActive: false,
  wifiApSSID: '--',
  wifiApIP: '--',
  wifiApClients: '--'
};

/**
 * Synchronise le cache WiFi (lastWifiData) avec une réponse /wifi/status ou équivalent.
 * À appeler après refreshWifiStatus() pour que les mises à jour WebSocket périodiques
 * n'écrasent pas l'affichage avec '--'.
 */
window.syncLastWifiData = function syncLastWifiData(data) {
  if (!data) return;
  if (data.wifiStaConnected !== undefined) {
    lastWifiData.wifiStaConnected = !!data.wifiStaConnected;
    if (data.wifiStaSSID !== undefined) lastWifiData.wifiStaSSID = data.wifiStaSSID || '--';
    if (data.wifiStaIP !== undefined) lastWifiData.wifiStaIP = data.wifiStaIP || '--';
    if (data.wifiStaRSSI !== undefined) lastWifiData.wifiStaRSSI = data.wifiStaRSSI;
  }
  if (data.wifiApActive !== undefined) {
    lastWifiData.wifiApActive = !!data.wifiApActive;
    if (data.wifiApSSID !== undefined) lastWifiData.wifiApSSID = data.wifiApSSID || '--';
    if (data.wifiApIP !== undefined) lastWifiData.wifiApIP = data.wifiApIP || '--';
    if (data.wifiApClients !== undefined) lastWifiData.wifiApClients = data.wifiApClients || '0';
  }
};

// Fonction utilitaire pour formater les valeurs
function formatValue(value, decimals = 1) {
  if (value === null || value === undefined || isNaN(value)) {
    return '--';
  }
  return Number(value).toFixed(decimals);
}

// Fonctions utilitaires
function $(id) { return document.getElementById(id); }
function fmt(v, d=1) { if(v===undefined||v===null||isNaN(v)) return '--'; return Number(v).toFixed(d); }

// Fonction pour détecter la nouvelle IP de l'ESP32 après changement de réseau
async function detectNewESP32IP() {
  return new Promise((resolve, reject) => {
    console.log('🔍 Début de la détection IP...');
    
    // Méthode 1: Essayer mDNS (.local)
    const mdnsCheck = fetch('http://esp32.local/json', { 
      method: 'HEAD',
      cache: 'no-store',
      signal: AbortSignal.timeout(3000)
    }).then(() => {
      console.log('🔍 ESP32 détecté via mDNS: esp32.local');
      return 'esp32.local';
    }).catch(() => null);
    
    // Méthode 1.5: Essayer aussi esp32.local avec WebSocket
    const mdnsWsCheck = new Promise((resolve) => {
      const testWs = new WebSocket('ws://esp32.local:81/ws');
      const timeout = setTimeout(() => {
        testWs.close();
        resolve(null);
      }, 3000);
      
      testWs.onopen = () => {
        clearTimeout(timeout);
        testWs.close();
        console.log('🔍 ESP32 détecté via mDNS WebSocket: esp32.local');
        resolve('esp32.local');
      };
      
      testWs.onerror = () => {
        clearTimeout(timeout);
        resolve(null);
      };
    });
    
    // Méthode 2: Scanner les IPs communes du réseau local
    const commonIPs = [];
    const currentIP = window.location.hostname;
    const baseIP = currentIP.substring(0, currentIP.lastIndexOf('.'));
    
    // Générer des IPs à scanner (priorité aux IPs communes)
    const priorityIPs = ['1', '100', '101', '102', '103', '104', '105', '106', '107', '108', '109', '110'];
    priorityIPs.forEach(ip => commonIPs.push(`${baseIP}.${ip}`));
    
    // Ajouter d'autres IPs communes (étendre la plage)
    for (let i = 2; i <= 150; i++) {
      if (!priorityIPs.includes(i.toString())) {
        commonIPs.push(`${baseIP}.${i}`);
      }
    }
    
    // Méthode simplifiée: tester directement les WebSockets sur les IPs prioritaires
    const wsTestPromises = priorityIPs.slice(0, 8).map(ip => {
      return new Promise((resolve) => {
        const testIP = `${baseIP}.${ip}`;
        console.log(`🔍 Test WebSocket sur ${testIP}...`);
        
        const testWs = new WebSocket(`ws://${testIP}:81/ws`);
        
        const timeout = setTimeout(() => {
          testWs.close();
          resolve(null);
        }, 3000);
        
        testWs.onopen = () => {
          clearTimeout(timeout);
          testWs.close();
          console.log(`✅ ESP32 détecté via WebSocket à: ${testIP}`);
          resolve(testIP);
        };
        
        testWs.onerror = () => {
          clearTimeout(timeout);
          resolve(null);
        };
      });
    });
    
    // Essayer mDNS (HTTP + WebSocket) puis scan IP WebSocket
    Promise.race([
      mdnsCheck,
      mdnsWsCheck,
      Promise.race(wsTestPromises.filter(p => p !== null))
    ]).then(result => {
      if (result) {
        console.log(`🎯 IP ESP32 trouvée: ${result}`);
        resolve(result);
      } else {
        console.log('❌ Aucune IP ESP32 trouvée');
        reject(new Error('Aucune IP trouvée'));
      }
    }).catch(reject);
  });
}

// Toast notifications améliorées
window.toast = function toast(msg, type = 'info', duration = 3000) {
  const t = $('toast');
  if (!t) return;
  t.textContent = msg;
  t.className = `toast ${type} show`;
  setTimeout(() => t.classList.remove('show'), duration);
}

// Fonction utilitaire pour mettre à jour le statut de connexion
window.updateConnectionStatus = function updateConnectionStatus(online) {
  isOnline = online;
  const statusEl = $('statusDot');
  const lastUpdateEl = $('lastUpdate');

  if (online) {
    statusEl.className = 'status-indicator status-online';
    lastUpdateEl.textContent = `MAJ: ${new Date().toLocaleTimeString()}`;
  } else {
    statusEl.className = 'status-indicator status-offline';
    lastUpdateEl.textContent = 'Hors ligne';
  }
}

// Historique des actions
window.addToHistory = function addToHistory(action, status, details = '') {
  const now = new Date();
  const timeStr = now.toLocaleTimeString();
  
  actionHistory.unshift({
    action,
    status,
    details,
    time: timeStr,
    timestamp: now.getTime()
  });
  
  // Limiter l'historique
  if (actionHistory.length > maxHistoryItems) {
    actionHistory = actionHistory.slice(0, maxHistoryItems);
  }
  
  updateHistoryDisplay();
}

function updateHistoryDisplay() {
  const historyEl = $('actionHistory');
  if (!historyEl) return;
  
  let html = '';
  actionHistory.forEach(item => {
    html += `
      <div class="action-item">
        <span>${item.action}</span>
        <div>
          <span class="action-time">${item.time}</span>
          <span class="action-status ${item.status}">${item.status.toUpperCase()}</span>
        </div>
      </div>
    `;
  });
  
  historyEl.innerHTML = html || '<div class="action-item"><span>Aucune action récente</span></div>';
}

// ========================================
// SYSTÈME DE LOGS AVANCÉ POUR LA CONSOLE WEB
// ========================================

// Configuration du système de logs
const LogConfig = {
  enabled: true,
  levels: {
    ERROR: 0,
    WARN: 1,
    INFO: 2,
    DEBUG: 3,
    TRACE: 4
  },
  currentLevel: 3, // DEBUG par défaut
  maxHistory: 1000,
  showTimestamp: true,
  showLevel: true,
  showEmoji: true
};

// Historique des logs pour affichage dans l'interface
let logHistory = [];

// Fonction de logging avancée
function log(level, message, data = null, category = 'GENERAL') {
  if (!LogConfig.enabled) return;
  
  const timestamp = new Date().toLocaleTimeString('fr-FR');
  const levelNames = ['ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE'];
  const levelEmojis = ['❌', '⚠️', 'ℹ️', '🔍', '🔬'];
  
  if (LogConfig.levels[level] > LogConfig.currentLevel) return;
  
  const logEntry = {
    timestamp,
    level,
    message,
    data,
    category,
    id: Date.now() + Math.random()
  };
  
  // Ajouter à l'historique
  logHistory.push(logEntry);
  if (logHistory.length > LogConfig.maxHistory) {
    logHistory.shift();
  }
  
  // Construire le message de log
  let logMessage = '';
  if (LogConfig.showTimestamp) logMessage += `[${timestamp}] `;
  if (LogConfig.showLevel) logMessage += `${levelNames[LogConfig.levels[level]]} `;
  if (LogConfig.showEmoji) logMessage += `${levelEmojis[LogConfig.levels[level]]} `;
  logMessage += `[${category}] ${message}`;
  
  // Afficher dans la console avec la méthode appropriée
  switch (LogConfig.levels[level]) {
    case 0: // ERROR
      console.error(logMessage, data || '');
      break;
    case 1: // WARN
      console.warn(logMessage, data || '');
      break;
    default:
      console.log(logMessage, data || '');
      break;
  }
  
  // Notifier les listeners de logs
  if (window.logListeners) {
    window.logListeners.forEach(listener => {
      try {
        listener(logEntry);
      } catch (e) {
        console.error('Erreur dans le listener de logs:', e);
      }
    });
  }
}

// Fonctions de logging spécialisées
function logError(message, data = null, category = 'ERROR') {
  log('ERROR', message, data, category);
}

function logWarn(message, data = null, category = 'WARN') {
  log('WARN', message, data, category);
}

function logInfo(message, data = null, category = 'INFO') {
  log('INFO', message, data, category);
}

function logDebug(message, data = null, category = 'DEBUG') {
  log('DEBUG', message, data, category);
}

function logTrace(message, data = null, category = 'TRACE') {
  log('TRACE', message, data, category);
}

// Fonction pour afficher les erreurs (compatibilité)
function showError(message) {
  logError(message, null, 'DASHBOARD');
}

// ========================================
// FONCTIONS DE GESTION DU PANNEAU DE LOGS
// ========================================

let logsVisible = false;
let logListeners = [];

// Initialiser les listeners de logs
window.logListeners = logListeners;

// Fonction pour basculer l'affichage des logs
window.toggleLogs = function toggleLogs() {
  const logPanel = document.getElementById('logPanel');
  const logToggle = document.getElementById('logToggle');
  
  logsVisible = !logsVisible;
  
  if (logsVisible) {
    logPanel.style.display = 'block';
    logToggle.style.display = 'none';
    updateLogDisplay();
    
    // Ajouter un listener pour les nouveaux logs
    if (!logListeners.includes(updateLogDisplay)) {
      logListeners.push(updateLogDisplay);
    }
    
    logInfo('Panneau de logs activé', null, 'LOGS');
  } else {
    logPanel.style.display = 'none';
    logToggle.style.display = 'block';
    
    // Retirer le listener
    const index = logListeners.indexOf(updateLogDisplay);
    if (index > -1) {
      logListeners.splice(index, 1);
    }
    
    logInfo('Panneau de logs désactivé', null, 'LOGS');
  }
}

// Fonction pour effacer les logs
window.clearLogs = function clearLogs() {
  logHistory = [];
  updateLogDisplay();
  logInfo('Logs effacés', null, 'LOGS');
}

// Fonction pour changer le niveau de log
window.changeLogLevel = function changeLogLevel() {
  const levelSelect = document.getElementById('logLevelSelect');
  if (!levelSelect) return;
  
  const level = parseInt(levelSelect.value);
  LogConfig.currentLevel = level;
  logInfo(`Niveau de log changé à ${['ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE'][level]}`, { level }, 'LOGS');
  updateLogDisplay();
}

// Fonction pour exporter les logs
window.exportLogs = function exportLogs() {
  const logsText = logHistory.map(entry => {
    const timestamp = entry.timestamp;
    const level = ['ERROR', 'WARN', 'INFO', 'DEBUG', 'TRACE'][LogConfig.levels[entry.level]];
    const emoji = ['❌', '⚠️', 'ℹ️', '🔍', '🔬'][LogConfig.levels[entry.level]];
    const category = entry.category;
    const message = entry.message;
    const data = entry.data ? JSON.stringify(entry.data) : '';
    
    return `[${timestamp}] ${level} ${emoji} [${category}] ${message} ${data}`;
  }).join('\n');
  
  const blob = new Blob([logsText], { type: 'text/plain' });
  const url = URL.createObjectURL(blob);
  const a = document.createElement('a');
  a.href = url;
  a.download = `ffp3-logs-${new Date().toISOString().slice(0, 19).replace(/:/g, '-')}.txt`;
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);
  
  logInfo('Logs exportés', { count: logHistory.length }, 'LOGS');
}

// Fonction pour mettre à jour l'affichage des logs
function updateLogDisplay() {
  const logContent = document.getElementById('logContent');
  if (!logContent) return;
  
  const currentLevel = LogConfig.currentLevel;
  const filteredLogs = logHistory.filter(entry => LogConfig.levels[entry.level] <= currentLevel);
  
  const html = filteredLogs.slice(-100).map(entry => {
    const levelClass = `log-level-${['error', 'warn', 'info', 'debug', 'trace'][LogConfig.levels[entry.level]]}`;
    const emoji = ['❌', '⚠️', 'ℹ️', '🔍', '🔬'][LogConfig.levels[entry.level]];
    const dataHtml = entry.data ? `<span class="log-data">${JSON.stringify(entry.data)}</span>` : '';
    
    return `
      <div class="log-entry">
        <span class="log-timestamp">[${entry.timestamp}]</span>
        <span class="${levelClass}">${emoji}</span>
        <span class="log-category">[${entry.category}]</span>
        <span class="log-message">${entry.message}</span>
        ${dataHtml}
      </div>
    `;
  }).join('');
  
  logContent.innerHTML = html || '<div class="log-entry"><span class="log-message">Aucun log à afficher</span></div>';
  
  // Scroll vers le bas
  logContent.scrollTop = logContent.scrollHeight;
}

// Fonctions de contrôle manuel avec feedback visuel amélioré et optimiste
window.action = async function action(cmd) {
  const btn = document.getElementById(`btn${cmd.charAt(0).toUpperCase() + cmd.slice(1)}`);
  const actionName = cmd === 'feedSmall' ? 'Nourrir Petits' : 'Nourrir Gros';
  
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-success w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> En cours...';
  }
  addToHistory(actionName, 'loading');
  
  // FEEDBACK OPTIMISTE : Afficher immédiatement l'état "en cours" et attendre les données WebSocket
  let feedbackReceived = false;
  let websocketTimeout;
  
  // Timeout de sécurité pour le feedback WebSocket (8 secondes pour les actions de nourrissage)
  const timeoutDuration = (cmd === 'feedBig' || cmd === 'feedSmall') ? 10000 : 6000;
  websocketTimeout = setTimeout(() => {
    if (!feedbackReceived) {
      logWarn(`Timeout WebSocket pour l'action ${cmd}, utilisation du feedback HTTP`, { cmd }, 'WEBSOCKET');
      logInfo(`Action ${cmd} exécutée mais pas de confirmation WebSocket dans les ${timeoutDuration/1000}s`, null, 'ACTION');
      feedbackReceived = true;
    }
  }, timeoutDuration);
  
  // Écouter les mises à jour WebSocket pour un feedback immédiat
  const originalUpdateSensorDisplay = window.updateSensorDisplay;
  const actionStartTime = Date.now();
  
  window.updateSensorDisplay = function(data) {
    // Appeler la fonction originale
    originalUpdateSensorDisplay(data);
    
    // Vérifier si c'est une mise à jour suite à notre action
    // Pour les actions de nourrissage, on accepte toute mise à jour WebSocket dans les 3 secondes
    // car les actions de nourrissage n'ont pas d'état visuel direct dans les données
    if (!feedbackReceived && (data.type === 'sensor_update' || data.type === 'sensor_data')) {
      const timeSinceAction = Date.now() - actionStartTime;
      
      // Accepter le feedback si on est dans le délai approprié après l'action
      // Plus généreux pour les actions de nourrissage qui n'ont pas d'état visuel
      const feedbackWindow = (cmd === 'feedBig' || cmd === 'feedSmall') ? 10000 : 6000;
      if (timeSinceAction <= feedbackWindow) {
        feedbackReceived = true;
        clearTimeout(websocketTimeout);
        
        logInfo(`Action ${cmd} confirmée via WebSocket après ${timeSinceAction}ms`, { cmd, timeSinceAction }, 'WEBSOCKET');
        
        if (btn) {
          btn.className = 'btn btn-outline-danger w-100';
          btn.innerHTML = cmd === 'feedSmall' ? '🐟 Nourrir Petits' : '🐠 Nourrir Gros';
          btn.disabled = false;
        }
        addToHistory(actionName, 'success', 'Action exécutée via WebSocket');
        toast(`Action ${actionName} exécutée avec succès`, 'success');
        
        // Restaurer la fonction originale après utilisation
        window.updateSensorDisplay = originalUpdateSensorDisplay;
      }
    }
  };
  
  try {
    logDebug(`Envoi requête HTTP pour action ${cmd}`, { cmd, actionStartTime }, 'HTTP');
    const response = await fetch(`/action?cmd=${cmd}`, { cache: 'no-store' });
    const result = await response.text();
    logInfo(`Action ${cmd}: ${result}`, { cmd, result, responseTime: Date.now() - actionStartTime }, 'HTTP');
    console.log(`Action ${cmd}: ${result}`);
    
    // Si le WebSocket n'a pas encore donné de feedback, utiliser la réponse HTTP
    if (!feedbackReceived) {
      feedbackReceived = true;
      clearTimeout(websocketTimeout);
      
      addToHistory(actionName, 'success', `${result} (via HTTP)`);
      toast(`Action ${actionName} exécutée avec succès`, 'success');
      
      if (btn) {
        btn.className = 'btn btn-outline-danger w-100';
        btn.innerHTML = cmd === 'feedSmall' ? '🐟 Nourrir Petits' : '🐠 Nourrir Gros';
        btn.disabled = false;
      }
    }
    window.updateSensorDisplay = originalUpdateSensorDisplay;
  } catch (error) {
    feedbackReceived = true;
    clearTimeout(websocketTimeout);
    logError(`Erreur action ${cmd}`, error, 'ACTION');
    addToHistory(actionName, 'error', error.message);
    toast(`Erreur lors de l'action ${actionName}`, 'error');
    if (btn) {
      btn.className = 'btn btn-outline-danger w-100';
      btn.innerHTML = cmd === 'feedSmall' ? '🐟 Nourrir Petits' : '🐠 Nourrir Gros';
      btn.disabled = false;
    }
    
    // Restaurer la fonction originale
    window.updateSensorDisplay = originalUpdateSensorDisplay;
  }
}

window.toggleRelay = async function toggleRelay(name) {
  const btn = document.getElementById(`btn${name.charAt(0).toUpperCase() + name.slice(1)}`);
  const relayNames = {
    light: 'Lumière',
    pumpTank: 'Pompe Réserve',
    pumpAqua: 'Pompe Aqua',
    heater: 'Chauffage'
  };
  const actionName = relayNames[name] || name;
  
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-success w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> En cours...';
  }
  addToHistory(`Toggle ${actionName}`, 'loading');
  let feedbackReceived = false;
  let websocketTimeout;
  const labels = { light: '💡 Lumière', pumpTank: '📦 Pompe Réserve', pumpAqua: '🐠 Pompe Aqua', heater: '🔥 Chauffage' };
  const label = labels[name] || name;

  // Timeout de sécurité pour le feedback WebSocket (6 secondes pour les relais)
  websocketTimeout = setTimeout(() => {
    if (!feedbackReceived) {
      logWarn(`Timeout WebSocket pour le relay ${name}, utilisation du feedback HTTP`, { name }, 'WEBSOCKET');
      feedbackReceived = true;
      if (btn) {
        btn.disabled = false;
        btn.innerHTML = label;
        btn.className = 'btn btn-outline-danger w-100';
      }
      window.updateSensorDisplay = originalUpdateSensorDisplay;
    }
  }, 6000);

  // Écouter les mises à jour WebSocket pour un feedback immédiat
  const originalUpdateSensorDisplay = window.updateSensorDisplay;
  window.updateSensorDisplay = function(data) {
    originalUpdateSensorDisplay(data);
    if (!feedbackReceived && data.type === 'action_confirm' && data.action === name) {
      feedbackReceived = true;
      clearTimeout(websocketTimeout);
      const isOn = data.result.includes('ON');
      if (btn) {
        btn.disabled = false;
        btn.innerHTML = label;
        btn.className = isOn ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
      }
      addToHistory(`Toggle ${actionName}`, 'success', data.result);
      toast(`${actionName}: ${data.result}`, 'success');
      window.updateSensorDisplay = originalUpdateSensorDisplay;
    } else if (!feedbackReceived && (data.type === 'sensor_update' || data.type === 'sensor_data') && data[name] !== undefined) {
      feedbackReceived = true;
      clearTimeout(websocketTimeout);
      const isOn = data[name];
      if (btn) {
        btn.disabled = false;
        btn.innerHTML = label;
        btn.className = isOn ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
      }
      addToHistory(`Toggle ${actionName}`, 'success', isOn ? 'ON' : 'OFF');
      toast(`${actionName}: ${isOn ? 'ON' : 'OFF'}`, 'success');
      window.updateSensorDisplay = originalUpdateSensorDisplay;
    }
  };

  try {
    const response = await fetch(`/action?relay=${name}`, { cache: 'no-store' });
    const result = await response.text();
    console.log(`Relay ${name}: ${result}`);
    if (!feedbackReceived) {
      feedbackReceived = true;
      clearTimeout(websocketTimeout);
      addToHistory(`Toggle ${actionName}`, 'success', result);
      toast(`${actionName}: ${result}`, 'success');
      const isOn = result.includes('ON');
      if (btn) {
        btn.innerHTML = label;
        btn.className = isOn ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
        btn.disabled = false;
      }
    }
    window.updateSensorDisplay = originalUpdateSensorDisplay;
  } catch (error) {
    feedbackReceived = true;
    clearTimeout(websocketTimeout);
    console.error(`Erreur relay ${name}:`, error);
    addToHistory(`Toggle ${actionName}`, 'error', error.message);
    toast(`Erreur ${actionName}`, 'error');
    if (btn) {
      btn.innerHTML = label;
      btn.className = 'btn btn-outline-danger w-100';
      btn.disabled = false;
    }
    window.updateSensorDisplay = originalUpdateSensorDisplay;
  }
}

window.mailTest = async function mailTest() {
  const btn = $('btnMailTest');
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-success w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> Test...';
  }
  addToHistory('Test Mail', 'loading');
  try {
    const response = await fetch('/mailtest', { cache: 'no-store' });
    const result = await response.text();
    addToHistory('Test Mail', 'success', result);
    toast(`Test Mail: ${result}`, 'success');
    if (btn) {
      btn.className = 'btn btn-outline-danger w-100';
      btn.innerHTML = '📧 Test Mail';
      btn.disabled = false;
    }
  } catch (error) {
    console.error('Erreur test mail:', error);
    addToHistory('Test Mail', 'error', error.message);
    toast(`Erreur test mail: ${error.message}`, 'error');
    if (btn) {
      btn.className = 'btn btn-outline-danger w-100';
      btn.innerHTML = '📧 Test Mail';
      btn.disabled = false;
    }
  }
};

window.toggleEmailNotifications = async function toggleEmailNotifications() {
  const btn = $('btnEmailNotif');
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-success w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> En cours...';
  }
  addToHistory('Toggle Email Notifications', 'loading');
  try {
    const response = await fetch('/action?cmd=toggleEmail', { cache: 'no-store' });
    const result = await response.text();
    addToHistory('Toggle Email Notifications', 'success', result);
    toast(`Notifications Email: ${result}`, 'success');
    if (btn) {
      btn.disabled = false;
      const on = result.toLowerCase().includes('activé') || result.toLowerCase().includes('on');
      btn.className = on ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
      btn.innerHTML = '🔔 Notifications Email';
    }
    
    // Rafraîchir les données pour mettre à jour l'affichage (badge Notifications)
    // Invalider le cache client pour forcer un GET /dbvars et afficher la nouvelle valeur
    if (typeof loadDbVars === 'function') {
      window.cachedDbVars = null;
      loadDbVars();
    }
  } catch (error) {
    console.error('Erreur toggle email:', error);
    addToHistory('Toggle Email Notifications', 'error', error.message);
    toast('Erreur lors du changement de statut des notifications', 'error');
    if (btn) {
      btn.disabled = false;
      btn.innerHTML = '🔔 Notifications Email';
    }
  }
};

window.toggleForceWakeup = async function toggleForceWakeup() {
  const btn = $('btnForceWakeup');
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-success w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> En cours...';
  }
  addToHistory('Toggle Force Wakeup', 'loading');
  try {
    const response = await fetch('/action?cmd=forceWakeUp', { cache: 'no-store' });
    const result = await response.text();
    addToHistory('Toggle Force Wakeup', 'success', result);
    toast(`Force Wakeup: ${result}`, 'success');
    if (btn) {
      btn.className = 'btn btn-outline-danger w-100';
      btn.innerHTML = '⏰ Force Wakeup';
      btn.disabled = false;
    }
    // L'état ON/OFF est mis à jour par le message WebSocket (setRelay dans le handler)
  } catch (error) {
    console.error('Erreur Force Wakeup:', error);
    addToHistory('Toggle Force Wakeup', 'error', error.message);
    toast(`Erreur Force Wakeup: ${error.message}`, 'error');
    if (btn) {
      btn.className = 'btn btn-outline-danger w-100';
      btn.innerHTML = '⏰ Force Wakeup';
      btn.disabled = false;
    }
  }
};

/**
 * Redémarre l'ESP32 (équivalent au bouton reset du serveur distant).
 * L'ESP envoie la réponse puis redémarre ; la connexion peut se couper avant la réponse.
 */
window.resetEsp = async function resetEsp() {
  const btn = $('btnResetEsp');
  if (btn) {
    btn.disabled = true;
    btn.className = 'btn btn-warning w-100';
    btn.innerHTML = '<span class="loading-spinner"></span> Redémarrage...';
  }
  addToHistory('Reset ESP', 'loading');
  try {
    const response = await fetch('/action?cmd=resetMode', { cache: 'no-store' });
    const result = await response.text();
    addToHistory('Reset ESP', 'success', result);
    toast('ESP redémarre. Reconnexion dans quelques secondes.', 'success');
  } catch (error) {
    addToHistory('Reset ESP', 'success', 'Commande envoyée (connexion perdue au redémarrage)');
    toast('ESP redémarre. Reconnexion dans quelques secondes.', 'success');
  }
  if (btn) {
    setTimeout(() => {
      btn.className = 'btn btn-outline-warning w-100';
      btn.innerHTML = '🔄 Reset ESP';
      btn.disabled = false;
    }, 15000);
  }
};

// Initialisation des graphiques avec uPlot
window.initCharts = function initCharts() {
  const tempEl = $('chartTemp');
  const waterEl = $('chartWater');
  
  // Si les éléments n'existent pas, ne rien faire (on n'est pas sur la page dashboard)
  if (!tempEl || !waterEl) {
    console.log('[Charts] Éléments graphiques non trouvés, skip initialisation');
    return;
  }
  
  // Vérifier que uPlot est chargé
  if (typeof uPlot === 'undefined') {
    console.warn('[Charts] uPlot non chargé, utilisation des graphiques désactivée');
    return;
  }
  
  console.log('[Charts] Initialisation avec uPlot...');
  
  // Options communes pour les graphiques
  const commonOpts = {
    width: tempEl.clientWidth,
    height: 200,
    cursor: { drag: { x: false, y: false } },
    legend: { show: true },
    scales: { 
      x: { 
        time: false // Désactiver le mode temporel automatique pour gérer nous-mêmes le formatage
      } 
    },
    axes: [
      { 
        stroke: '#94a3b8', 
        grid: { stroke: 'rgba(255,255,255,0.1)', width: 1 }, 
        ticks: { stroke: 'rgba(255,255,255,0.1)', width: 1 },
        values: (u, vals, space) => vals.map(v => {
          const d = new Date(v);
          const h = String(d.getHours()).padStart(2, '0');
          const m = String(d.getMinutes()).padStart(2, '0');
          const s = String(d.getSeconds()).padStart(2, '0');
          return `${h}:${m}:${s}`;
        })
      },
      { stroke: '#94a3b8', grid: { stroke: 'rgba(255,255,255,0.1)', width: 1 }, ticks: { stroke: 'rgba(255,255,255,0.1)', width: 1 } }
    ]
  };
  
  // Graphique des températures
  const tempOpts = Object.assign({}, commonOpts, {
    series: [
      {},
      { label: 'Air °C', stroke: '#f59e0b', width: 2, fill: 'rgba(245, 158, 11, 0.1)' },
      { label: 'Eau °C', stroke: '#06b6d4', width: 2, fill: 'rgba(6, 182, 212, 0.1)' }
    ]
  });
  tempChart = new uPlot(tempOpts, [[], [], []], tempEl);
  
  // Graphique des niveaux d'eau
  const waterOpts = Object.assign({}, commonOpts, {
    series: [
      {},
      { label: 'Aquarium cm', stroke: '#10b981', width: 2, fill: 'rgba(16, 185, 129, 0.1)' },
      { label: 'Réservoir cm', stroke: '#8b5cf6', width: 2, fill: 'rgba(139, 92, 246, 0.1)' },
      { label: 'Potager cm', stroke: '#f59e0b', width: 2, fill: 'rgba(245, 158, 11, 0.1)' }
    ]
  });
  waterChart = new uPlot(waterOpts, [[], [], [], []], waterEl);
  
  console.log('[Charts] uPlot initialisé avec succès');
}

// Fonction pour redimensionner les graphiques
window.resizeCharts = function resizeCharts() {
  if (tempChart && tempChart.setSize) {
    const tempEl = $('chartTemp');
    if (tempEl) tempChart.setSize({ width: tempEl.clientWidth, height: 200 });
  }
  if (waterChart && waterChart.setSize) {
    const waterEl = $('chartWater');
    if (waterEl) waterChart.setSize({ width: waterEl.clientWidth, height: 200 });
  }
}


// Mise à jour des graphiques uPlot
window.updateCharts = function updateCharts(data) {
  if (!tempChart || !waterChart) {
    console.warn('[Charts] Graphiques non initialisés, skip update');
    return;
  }
  
  const now = Date.now(); // Timestamp en millisecondes pour notre formateur personnalisé
  const maxDataPoints = 20;
  
  try {
    // Données températures - créer de nouveaux tableaux
    const tempData = [
      [...(tempChart.data[0] || []), now],
      [...(tempChart.data[1] || []), data.tempAir !== null ? data.tempAir : null],
      [...(tempChart.data[2] || []), data.tempWater !== null ? data.tempWater : null]
    ];
    
    // Données niveaux d'eau - créer de nouveaux tableaux
    const waterData = [
      [...(waterChart.data[0] || []), now],
      [...(waterChart.data[1] || []), data.wlAqua !== null ? data.wlAqua : null],
      [...(waterChart.data[2] || []), data.wlTank !== null ? data.wlTank : null],
      [...(waterChart.data[3] || []), data.wlPota !== null ? data.wlPota : null]
    ];
    
    // Limiter le nombre de points affichés
    if (tempData[0].length > maxDataPoints) {
      tempData[0].shift();
      tempData[1].shift();
      tempData[2].shift();
    }
    
    if (waterData[0].length > maxDataPoints) {
      waterData[0].shift();
      waterData[1].shift();
      waterData[2].shift();
      waterData[3].shift();
    }
    
    // Mettre à jour les graphiques uPlot
    tempChart.setData(tempData);
    waterChart.setData(waterData);
    
  } catch (error) {
    console.error('[Charts] Erreur mise à jour graphiques:', error);
  }
}

// Réapplique le dernier état des relais sur les boutons (page Contrôles) : vert = actif, rouge = inactif, pas de mention ON/OFF
window.applyRelayStateToButtons = function applyRelayStateToButtons() {
  const state = window.lastRelayState;
  if (!state) return;
  const btn = (id) => document.getElementById(id);
  const setRelay = (b, label, on) => {
    if (b) {
      b.innerHTML = label;
      b.className = on ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
    }
  };
  if (state.pumpTank !== undefined) setRelay(btn('btnPumpTank'), '📦 Pompe Réserve', state.pumpTank);
  if (state.pumpAqua !== undefined) setRelay(btn('btnPumpAqua'), '🐠 Pompe Aqua', state.pumpAqua);
  if (state.heater !== undefined) setRelay(btn('btnHeater'), '🔥 Chauffage', state.heater);
  if (state.light !== undefined) setRelay(btn('btnLight'), '💡 Lumière', state.light);
  if (state.forceWakeUp !== undefined) setRelay(btn('btnForceWakeup'), '⏰ Force Wakeup', state.forceWakeUp);
  if (state.mailNotif !== undefined) setRelay(btn('btnEmailNotif'), '🔔 Notifications Email', !!state.mailNotif);
};

// Charge l'état des actionneurs depuis /json et l'affiche (badges vert/rouge à l'ouverture Contrôles)
window.ensureActuatorStateDisplay = function ensureActuatorStateDisplay() {
  fetch('/json', { method: 'GET', cache: 'no-cache', signal: AbortSignal.timeout(5000) })
    .then(r => r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status)))
    .then(doc => {
      if (!window.lastRelayState) window.lastRelayState = {};
      if (doc.pumpTank !== undefined) window.lastRelayState.pumpTank = !!doc.pumpTank;
      if (doc.pumpAqua !== undefined) window.lastRelayState.pumpAqua = !!doc.pumpAqua;
      if (doc.heater !== undefined) window.lastRelayState.heater = !!doc.heater;
      if (doc.light !== undefined) window.lastRelayState.light = !!doc.light;
      if (doc.forceWakeUp !== undefined) window.lastRelayState.forceWakeUp = !!doc.forceWakeUp;
      if (doc.mailNotif !== undefined) window.lastRelayState.mailNotif = !!doc.mailNotif;
      if (typeof window.applyRelayStateToButtons === 'function') window.applyRelayStateToButtons();
    })
    .catch(() => { if (typeof window.applyRelayStateToButtons === 'function') window.applyRelayStateToButtons(); });
};

// Fonction de mise à jour des capteurs avec KPIs (optimisée)
window.updateSensorDisplay = function updateSensorDisplay(data) {
  const now = Date.now();
  
  // Vérifier si des données WiFi sont présentes - toujours les mettre à jour
  const hasWifiData = data.wifiStaConnected !== undefined || data.wifiApActive !== undefined;
  // Ne pas throttler quand le message contient des états actionneurs (page Contrôles à jour)
  const hasActuatorData = data.pumpAqua !== undefined || data.pumpTank !== undefined || data.heater !== undefined || data.light !== undefined;
  
  // Throttle les mises à jour SAUF pour les données WiFi et les états actionneurs
  if (!hasWifiData && !hasActuatorData && now - lastDataUpdate < DATA_UPDATE_THROTTLE) {
    return; // Throttle les mises à jour sauf WiFi et actionneurs
  }
  lastDataUpdate = now;
  
  // Utiliser requestAnimationFrame pour des mises à jour fluides
  requestAnimationFrame(() => {
    // Mesures détaillées avec vérifications de sécurité
    const updateElement = (id, value, decimals = 1) => {
      const el = $(id);
      if (el) {
        if (value === null || value === undefined) {
          el.textContent = '--';
        } else {
          el.textContent = fmt(value, decimals);
        }
      }
    };
    
    updateElement('tWater', data.tempWater);
    updateElement('tAir', data.tempAir);
    updateElement('humid', data.humidity);
    updateElement('wlAqua', data.wlAqua, 0);
    updateElement('wlTank', data.wlTank, 0);
    updateElement('wlPota', data.wlPota, 0);
    updateElement('lumi', data.luminosite, 0);
    
    // États des actionneurs : affichage direct dans les boutons (comme Notifications)
    // Mettre en cache pour réappliquer au chargement de la page Contrôles (contenu dynamique)
    if (!window.lastRelayState) window.lastRelayState = {};
    if (data.pumpTank !== undefined) window.lastRelayState.pumpTank = data.pumpTank;
    if (data.pumpAqua !== undefined) window.lastRelayState.pumpAqua = data.pumpAqua;
    if (data.heater !== undefined) window.lastRelayState.heater = data.heater;
    if (data.light !== undefined) window.lastRelayState.light = data.light;
    if (data.forceWakeUp !== undefined) window.lastRelayState.forceWakeUp = data.forceWakeUp;
    if (data.mailNotif !== undefined) window.lastRelayState.mailNotif = !!data.mailNotif;

    const setRelay = (id, label, on) => {
      const b = $(id);
      if (b) {
        b.innerHTML = label;
        b.className = on ? 'btn btn-success w-100' : 'btn btn-outline-danger w-100';
      }
    };
    if (data.pumpTank !== undefined) setRelay('btnPumpTank', '📦 Pompe Réserve', data.pumpTank);
    if (data.pumpAqua !== undefined) setRelay('btnPumpAqua', '🐠 Pompe Aqua', data.pumpAqua);
    if (data.heater !== undefined) setRelay('btnHeater', '🔥 Chauffage', data.heater);
    if (data.light !== undefined) setRelay('btnLight', '💡 Lumière', data.light);
    if (data.forceWakeUp !== undefined) setRelay('btnForceWakeup', '⏰ Force Wakeup', data.forceWakeUp);
    if (data.mailNotif !== undefined) setRelay('btnEmailNotif', '🔔 Notifications Email', !!data.mailNotif);

    // États WiFi STA (informations détaillées uniquement)
    // Clés harmonisées: wifiSta* (cohérent avec /json et /wifi/status)
    // Ne mettre à jour le cache détaillé que si les champs sont présents (évite d'effacer le SSID
    // à chaque broadcast périodique minimal qui n'envoie que wifiStaConnected)
    if(data.wifiStaConnected !== undefined) {
      lastWifiData.wifiStaConnected = data.wifiStaConnected;
      if (data.wifiStaSSID !== undefined) lastWifiData.wifiStaSSID = data.wifiStaSSID || '--';
      if (data.wifiStaIP !== undefined) lastWifiData.wifiStaIP = data.wifiStaIP || '--';
      if (data.wifiStaRSSI !== undefined) lastWifiData.wifiStaRSSI = data.wifiStaRSSI;
      
      // Mise à jour des informations détaillées WiFi STA
      const ssidEl = $('wifiStaSSID');
      const ipEl = $('wifiStaIP');
      const rssiEl = $('wifiStaRSSI');
      const statusEl = $('wifiStaStatus');
      
      if (data.wifiStaConnected) {
        if (ssidEl) ssidEl.textContent = lastWifiData.wifiStaSSID;
        if (ipEl) ipEl.textContent = lastWifiData.wifiStaIP;
        if (rssiEl) rssiEl.textContent = lastWifiData.wifiStaRSSI;
        if (statusEl) {
          statusEl.textContent = 'CONNECTÉ';
          statusEl.className = 'badge bg-success';
        }
      } else {
        if (ssidEl) ssidEl.textContent = '--';
        if (ipEl) ipEl.textContent = '--';
        if (rssiEl) rssiEl.textContent = '--';
        if (statusEl) {
          statusEl.textContent = 'DÉCONNECTÉ';
          statusEl.className = 'badge bg-danger';
        }
      }
    } else {
      // Utiliser les dernières valeurs connues si les données ne sont pas présentes
      // (silencieux - pas de log pour éviter la pollution de la console)
      const ssidEl = $('wifiStaSSID');
      const ipEl = $('wifiStaIP');
      const rssiEl = $('wifiStaRSSI');
      const statusEl = $('wifiStaStatus');
      if (lastWifiData.wifiStaConnected) {
        if (ssidEl) ssidEl.textContent = lastWifiData.wifiStaSSID;
        if (ipEl) ipEl.textContent = lastWifiData.wifiStaIP;
        if (rssiEl) rssiEl.textContent = lastWifiData.wifiStaRSSI;
        if (statusEl) {
          statusEl.textContent = 'CONNECTÉ';
          statusEl.className = 'badge bg-success';
        }
      }
    }
    
    // États WiFi AP (informations détaillées uniquement)
    // Clés harmonisées: wifiAp* (cohérent avec /json et /wifi/status)
    // Ne mettre à jour le cache détaillé que si les champs sont présents
    if(data.wifiApActive !== undefined) {
      lastWifiData.wifiApActive = data.wifiApActive;
      if (data.wifiApSSID !== undefined) lastWifiData.wifiApSSID = data.wifiApSSID || '--';
      if (data.wifiApIP !== undefined) lastWifiData.wifiApIP = data.wifiApIP || '--';
      if (data.wifiApClients !== undefined) lastWifiData.wifiApClients = data.wifiApClients || '0';
      
      // Mise à jour des informations détaillées WiFi AP
      const ssidEl = $('wifiApSSID');
      const ipEl = $('wifiApIP');
      const clientsEl = $('wifiApClients');
      const detailStatusEl = $('wifiApStatus');
      
      if (data.wifiApActive) {
        if (ssidEl) ssidEl.textContent = lastWifiData.wifiApSSID;
        if (ipEl) ipEl.textContent = lastWifiData.wifiApIP;
        if (clientsEl) clientsEl.textContent = lastWifiData.wifiApClients;
        if (detailStatusEl) {
          detailStatusEl.textContent = 'ACTIF';
          detailStatusEl.className = 'badge bg-success';
        }
      } else {
        if (ssidEl) ssidEl.textContent = '--';
        if (ipEl) ipEl.textContent = '--';
        if (clientsEl) clientsEl.textContent = '--';
        if (detailStatusEl) {
          detailStatusEl.textContent = 'INACTIF';
          detailStatusEl.className = 'badge bg-secondary';
        }
      }
    } else {
      // Utiliser les dernières valeurs connues si les données ne sont pas présentes
      // (silencieux - pas de log pour éviter la pollution de la console)
      const ssidEl = $('wifiApSSID');
      const ipEl = $('wifiApIP');
      const clientsEl = $('wifiApClients');
      const detailStatusEl = $('wifiApStatus');
      
      if (lastWifiData.wifiApActive) {
        if (ssidEl) ssidEl.textContent = lastWifiData.wifiApSSID;
        if (ipEl) ipEl.textContent = lastWifiData.wifiApIP;
        if (clientsEl) clientsEl.textContent = lastWifiData.wifiApClients;
        if (detailStatusEl) {
          detailStatusEl.textContent = 'ACTIF';
          detailStatusEl.className = 'badge bg-success';
        }
      }
    }
    
    // Mettre à jour les graphiques
    updateCharts(data);
  });
}

// ========================================
// FONCTIONS DE GESTION DES VARIABLES BDD
// ========================================
// NOTE: Les fonctions principales loadDbVars, submitDbVars, fillFormFromDb
// sont définies dans websocket.js (versions plus complètes avec cache et optimisations)
// Ce fichier ne contient que les fonctions d'initialisation

// Cache des variables BDD (partagé avec websocket.js)
window._dbCache = window._dbCache || {};

// Charger la version du firmware
window.loadFirmwareVersion = async function loadFirmwareVersion() {
  try {
    const response = await fetch('/version', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const data = await response.json();
    
    const versionEl = document.getElementById('fwVersion');
    if (versionEl && data.version) {
      versionEl.textContent = data.version;
      document.title = `FFP3 Dashboard v${data.version}`;
    }
    
    logInfo(`Firmware version: ${data.version}`, data, 'INIT');
  } catch (e) {
    logWarn(`Erreur chargement version: ${e.message}`, null, 'INIT');
  }
}

// Charger l'historique des actions
window.loadActionHistory = async function loadActionHistory() {
  // L'historique est géré en mémoire par addToHistory()
  // Cette fonction pourrait charger un historique sauvegardé
  logInfo('Historique des actions initialisé', { count: actionHistory.length }, 'INIT');
  updateHistoryDisplay();
}