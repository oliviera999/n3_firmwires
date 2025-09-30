#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FFP3 Dashboard</title>
<link href="/bundle.css" rel="stylesheet">
<script src="/bundle.js"></script>
<script>
// WebSocket client pour données temps réel (fallback polling)
let ws;
function startWebSocket() {
  try {
    const proto = location.protocol === 'https:' ? 'wss' : 'ws';
    const url = `${proto}://${location.host}:81/ws`;
    ws = new WebSocket(url);
    ws.onopen = () => { try { ws.send('{"type":"subscribe"}'); } catch(e){} };
    ws.onmessage = (ev) => {
      try {
        const msg = JSON.parse(ev.data);
        if (msg.type === 'sensor_update' || msg.type === 'sensor_data') {
          updateConnectionStatus(true);
          updateSensorDisplay(msg);
          updateCharts(msg);
        }
      } catch(e) {}
    };
    ws.onclose = () => { setTimeout(startWebSocket, 3000); };
    ws.onerror = () => { try { ws.close(); } catch(e){} };
  } catch (e) {}
}
</script>
<script>
// Dashboard JavaScript simplifié pour FFP3
let tempChart, waterChart;
let isOnline = false;

// Fonction utilitaire pour formater les valeurs
function formatValue(value, decimals = 1) {
  if (value === null || value === undefined || isNaN(value)) {
    return '--';
  }
  return Number(value).toFixed(decimals);
}

// Fonction utilitaire pour mettre à jour le statut de connexion
function updateConnectionStatus(online) {
  isOnline = online;
  const statusEl = document.getElementById('connectionStatus');
  const lastUpdateEl = document.getElementById('lastUpdate');

  if (online) {
    statusEl.className = 'status-indicator status-online';
    lastUpdateEl.textContent = `Dernière mise à jour: ${new Date().toLocaleTimeString()}`;
  } else {
    statusEl.className = 'status-indicator status-offline';
    lastUpdateEl.textContent = 'Connexion perdue';
  }
}

// Fonction pour afficher les erreurs
function showError(message) {
  console.error('Dashboard Error:', message);
}

// Fonctions d'indicateurs de chargement
function showLoadingIndicator() {
  const statusEl = document.getElementById('lastUpdate');
  if (statusEl) {
    statusEl.textContent = 'Chargement...';
    statusEl.style.color = '#ffc107';
  }
}

function hideLoadingIndicator() {
  const statusEl = document.getElementById('lastUpdate');
  if (statusEl) {
    statusEl.textContent = `Dernière mise à jour: ${new Date().toLocaleTimeString()}`;
    statusEl.style.color = '#6c757d';
  }
}

// Fonctions de contrôle manuel avec feedback visuel
async function action(cmd) {
  const btn = document.getElementById(`btn${cmd.charAt(0).toUpperCase() + cmd.slice(1)}`);
  if (btn) {
    btn.disabled = true;
    btn.innerHTML = '⏳ En cours...';
  }
  
  try {
    const response = await fetch(`/action?cmd=${cmd}`, { cache: 'no-store' });
    const result = await response.text();
    console.log(`Action ${cmd}: ${result}`);
    
    if (btn) {
      btn.innerHTML = '✅ Fait';
      setTimeout(() => {
        btn.disabled = false;
        if (cmd === 'feedSmall') btn.innerHTML = '🐟 Nourrir Petits';
        else if (cmd === 'feedBig') btn.innerHTML = '🐠 Nourrir Gros';
      }, 2000);
    }
  } catch (error) {
    console.error(`Erreur action ${cmd}:`, error);
    if (btn) {
      btn.innerHTML = '❌ Erreur';
      setTimeout(() => {
        btn.disabled = false;
        if (cmd === 'feedSmall') btn.innerHTML = '🐟 Nourrir Petits';
        else if (cmd === 'feedBig') btn.innerHTML = '🐠 Nourrir Gros';
      }, 2000);
    }
  }
}

async function toggleRelay(name) {
  const btn = document.getElementById(`btn${name.charAt(0).toUpperCase() + name.slice(1)}`);
  if (btn) {
    btn.disabled = true;
    btn.innerHTML = '⏳ En cours...';
  }
  
  try {
    const response = await fetch(`/action?relay=${name}`, { cache: 'no-store' });
    const result = await response.text();
    console.log(`Relay ${name}: ${result}`);
    
    // Rafraîchir rapidement l'affichage (le WebSocket enverra déjà un update)
    setTimeout(() => refresh(), 200);
    
    if (btn) {
      btn.innerHTML = '✅ Fait';
      setTimeout(() => {
        btn.disabled = false;
        // Restaurer le texte original
        if (name === 'light') btn.innerHTML = '💡 Lumière';
        else if (name === 'pumpTank') btn.innerHTML = '📦 Pompe Réserve';
        else if (name === 'pumpAqua') btn.innerHTML = '🐠 Pompe Aqua';
        else if (name === 'heater') btn.innerHTML = '🔥 Chauffage';
      }, 1500);
    }
  } catch (error) {
    console.error(`Erreur relay ${name}:`, error);
    if (btn) {
      btn.innerHTML = '❌ Erreur';
      setTimeout(() => {
        btn.disabled = false;
        if (name === 'light') btn.innerHTML = '💡 Lumière';
        else if (name === 'pumpTank') btn.innerHTML = '📦 Pompe Réserve';
        else if (name === 'pumpAqua') btn.innerHTML = '🐠 Pompe Aqua';
        else if (name === 'heater') btn.innerHTML = '🔥 Chauffage';
      }, 2000);
    }
  }
}

async function mailTest() {
  const btn = document.getElementById('btnMailTest');
  if (btn) {
    btn.disabled = true;
    btn.innerHTML = '⏳ Test...';
  }
  
  try {
    const response = await fetch('/mailtest', { cache: 'no-store' });
    const result = await response.text();
    alert(`Test Mail: ${result}`);
    
    if (btn) {
      btn.innerHTML = '✅ Envoyé';
      setTimeout(() => {
        btn.disabled = false;
        btn.innerHTML = '📧 Test Mail';
      }, 2000);
    }
  } catch (error) {
    console.error('Erreur test mail:', error);
    alert(`Erreur test mail: ${error.message}`);
    if (btn) {
      btn.innerHTML = '❌ Erreur';
      setTimeout(() => {
        btn.disabled = false;
        btn.innerHTML = '📧 Test Mail';
      }, 2000);
    }
  }
}

// Fonction de mise à jour des capteurs
function updateSensorDisplay(data) {
  updateElement('tWater', formatValue(data.tempWater));
  updateElement('tAir', formatValue(data.tempAir));
  updateElement('humid', formatValue(data.humidity));
  updateElement('wlAqua', formatValue(data.wlAqua));
  updateElement('wlTank', formatValue(data.wlTank));
  updateElement('wlPota', formatValue(data.wlPota));
  updateElement('lumi', formatValue(data.luminosite));
  // Affiche en volts si disponible, sinon convertit mV -> V
  const voltageV = (typeof data.voltageV !== 'undefined') ? data.voltageV : (typeof data.voltage !== 'undefined' ? Number(data.voltage) / 1000.0 : NaN);
  updateElement('vin', formatValue(voltageV, 2));
  
  // Mise à jour des états des actionneurs
  updateElement('statusPump', data.pumpTank ? 'ON' : 'OFF');
  updateElement('statusPumpAqua', data.pumpAqua ? 'ON' : 'OFF');
  updateElement('statusHeater', data.heater ? 'ON' : 'OFF');
  updateElement('statusLight', data.light ? 'ON' : 'OFF');
}

// Fonction de mise à jour des variables BDD
function updateDatabaseDisplay(db) {
  updateElement('dbFeedMorning', db.feedMorning);
  updateElement('dbFeedNoon', db.feedNoon);
  updateElement('dbFeedEvening', db.feedEvening);
  updateElement('dbFeedBigDur', db.feedBigDur);
  updateElement('dbFeedSmallDur', db.feedSmallDur);
  updateElement('dbAqThreshold', db.aqThreshold);
  updateElement('dbTankThreshold', db.tankThreshold);
  updateElement('dbHeaterThreshold', formatValue(db.heaterThreshold));
  updateElement('dbRefillDuration', db.refillDuration);
  updateElement('dbLimFlood', db.limFlood);
  updateElement('dbEmailAddress', db.emailAddress);
  updateEmailStatus('dbEmailEnabled', db.emailEnabled);
  
  // Mise à jour de l'onglet des variables BDD
  updateElement('dbTabFeedMorning', db.feedMorning);
  updateElement('dbTabFeedNoon', db.feedNoon);
  updateElement('dbTabFeedEvening', db.feedEvening);
  updateElement('dbTabFeedBigDur', db.feedBigDur);
  updateElement('dbTabFeedSmallDur', db.feedSmallDur);
  updateElement('dbTabAqThreshold', db.aqThreshold);
  updateElement('dbTabTankThreshold', db.tankThreshold);
  updateElement('dbTabHeaterThreshold', formatValue(db.heaterThreshold));
  updateElement('dbTabRefillDuration', db.refillDuration);
  updateElement('dbTabLimFlood', db.limFlood);
}

// Fonction principale de rafraîchissement
async function refresh() {
  try {
    console.log('DEBUG: Début de refresh()');
    const response = await fetch('/json', { cache: 'no-store' });
    console.log('DEBUG: Response reçue:', response.status);
    
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }

    const data = await response.json();
    console.log('DEBUG: Données JSON:', data);
    updateConnectionStatus(true);

    // Utiliser la fonction de mise à jour optimisée
    updateSensorDisplay(data);

    // Mise à jour des statuts des équipements
    updateEquipmentStatus('statusPump', data.pumpTank, 'Pompe Réserve');
    updateEquipmentStatus('statusPumpAqua', data.pumpAqua, 'Pompe Aqua');
    updateEquipmentStatus('statusHeater', data.heater, 'Chauffage');
    updateEquipmentStatus('statusLight', data.light, 'Lumière');

    // Graphiques
    updateCharts(data);

  } catch (error) {
    console.error('DEBUG: Erreur dans refresh():', error);
    updateConnectionStatus(false);
    showError(`Erreur de rafraîchissement: ${error.message}`);
  }
}

// Fonction pour mettre à jour le statut des équipements
function updateEquipmentStatus(elementId, isOn, label) {
  const element = document.getElementById(elementId);
  if (element) {
    element.className = `badge ${isOn ? 'bg-success' : 'bg-danger'}`;
    element.textContent = isOn ? 'ON' : 'OFF';
  }
}

// Graphiques
let _tChart, _wChart;
function initCharts() {
  const tempCanvas = document.getElementById('chartTemp');
  const waterCanvas = document.getElementById('chartWater');
  if (!tempCanvas || !waterCanvas || typeof Chart === 'undefined') {
    console.warn('DEBUG: Charts non initialisés (canvas/Chart manquant)');
    return;
  }
  const tempCtx = tempCanvas.getContext('2d');
  const waterCtx = waterCanvas.getContext('2d');
  _tChart = new Chart(tempCtx, {
    type: 'line',
    data: { labels: [], datasets: [
      { label: 'Air °C',   data: [], borderColor: '#ffc107' },
      { label: 'Eau °C',   data: [], borderColor: '#0dcaf0' }
    ]},
    options: { responsive: true }
  });
  _wChart = new Chart(waterCtx, {
    type: 'line',
    data: { labels: [], datasets: [
      { label: 'Aquarium cm', data: [], borderColor: '#198754' },
      { label: 'Réservoir cm', data: [], borderColor: '#6f42c1' }
    ]},
    options: { responsive: true }
  });
  console.log('DEBUG: Charts initialisés');
}

function updateCharts(data) {
  if (!_tChart || !_wChart) return;
  const label = new Date().toLocaleTimeString();
  // Temp
  _tChart.data.labels.push(label);
  _tChart.data.datasets[0].data.push(isNaN(data.tempAir) ? 0 : data.tempAir);
  _tChart.data.datasets[1].data.push(isNaN(data.tempWater) ? 0 : data.tempWater);
  // Water
  _wChart.data.labels.push(label);
  _wChart.data.datasets[0].data.push(isNaN(data.wlAqua) ? 0 : data.wlAqua);
  _wChart.data.datasets[1].data.push(isNaN(data.wlTank) ? 0 : data.wlTank);
  // Trim à 60 points
  const trim = (chart) => {
    const max = 60;
    while (chart.data.labels.length > max) chart.data.labels.shift();
    chart.data.datasets.forEach(ds => { while (ds.data.length > max) ds.data.shift(); });
    chart.update('none');
  };
  trim(_tChart); trim(_wChart);
}

// DB Vars helpers
function updateElement(id, value) {
  const el = document.getElementById(id);
  if (el) el.textContent = (value !== null && value !== undefined) ? value : '--';
}
function updateFeedingFlag(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  el.className = `badge ${value ? 'bg-success' : 'bg-secondary'}`;
  el.textContent = value ? 'OK' : 'En attente';
}
function updateCommandFlag(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = value || '--';
  el.className = `badge ${value === '1' ? 'bg-success' : 'bg-secondary'}`;
}
function updateEmailStatus(id, value) {
  const el = document.getElementById(id);
  if (!el) return;
  el.textContent = value ? 'Activé' : 'Désactivé';
  el.className = `badge ${value ? 'bg-success' : 'bg-secondary'}`;
}

async function updateDatabaseVariables() {
  try {
    // Afficher l'indicateur de chargement
    showLoadingIndicator();
    
    const response = await fetch('/dbvars', { cache: 'no-store' });
    if (!response.ok) throw new Error(`HTTP ${response.status}`);
    const db = await response.json();
    
    // Masquer l'indicateur de chargement
    hideLoadingIndicator();
    // Heures & durées
    updateElement('dbFeedMorning', db.feedMorning);
    updateElement('dbFeedNoon', db.feedNoon);
    updateElement('dbFeedEvening', db.feedEvening);
    updateElement('dbFeedBigDur', db.feedBigDur);
    updateElement('dbFeedSmallDur', db.feedSmallDur);
    // Seuils
    updateElement('dbAqThreshold', db.aqThreshold);
    updateElement('dbTankThreshold', db.tankThreshold);
    updateElement('dbHeaterThreshold', formatValue(db.heaterThreshold));
    updateElement('dbRefillDuration', db.refillDuration);
    updateElement('dbLimFlood', db.limFlood);
    // Flags
    updateFeedingFlag('dbBouffeMatin', db.bouffeMatin);
    updateFeedingFlag('dbBouffeMidi', db.bouffeMidi);
    updateFeedingFlag('dbBouffeSoir', db.bouffeSoir);
    // Commandes
    updateCommandFlag('dbBouffePetits', db.bouffePetits);
    updateCommandFlag('dbBouffeGros', db.bouffeGros);
    // Email
    updateElement('dbEmailAddress', db.emailAddress);
    updateEmailStatus('dbEmailEnabled', db.emailEnabled);
    updateElement('dbTabEmailAddress', db.emailAddress);
    updateEmailStatus('dbTabEmailEnabled', db.emailEnabled);
    // Onglet détaillé
    updateElement('dbTabFeedMorning', db.feedMorning);
    updateElement('dbTabFeedNoon', db.feedNoon);
    updateElement('dbTabFeedEvening', db.feedEvening);
    updateElement('dbTabFeedBigDur', db.feedBigDur);
    updateElement('dbTabFeedSmallDur', db.feedSmallDur);
    updateElement('dbTabAqThreshold', db.aqThreshold);
    updateElement('dbTabTankThreshold', db.tankThreshold);
    updateElement('dbTabHeaterThreshold', formatValue(db.heaterThreshold));
    updateElement('dbTabRefillDuration', db.refillDuration);
    updateElement('dbTabLimFlood', db.limFlood);
    updateFeedingFlag('dbTabBouffeMatin', db.bouffeMatin);
    updateFeedingFlag('dbTabBouffeMidi', db.bouffeMidi);
    updateFeedingFlag('dbTabBouffeSoir', db.bouffeSoir);
    updateCommandFlag('dbTabBouffePetits', db.bouffePetits);
    updateCommandFlag('dbTabBouffeGros', db.bouffeGros);
    updateElement('dbTabEmailAddress', db.emailAddress);
    updateEmailStatus('dbTabEmailEnabled', db.emailEnabled);
    // Cache formulaire
    window._dbCache = db;
    if (typeof fillFormFromDb === 'function') fillFormFromDb();
  } catch (e) {
    showError(`Erreur BDD: ${e.message}`);
  }
}

function fillFormFromDb() {
  const db = window._dbCache || {};
  const set = (id, v) => { const el = document.getElementById(id); if (el && v !== undefined && v !== null) el.value = v; };
  set('formFeedMorning', db.feedMorning);
  set('formFeedNoon', db.feedNoon);
  set('formFeedEvening', db.feedEvening);
  set('formFeedBigDur', db.feedBigDur);
  set('formFeedSmallDur', db.feedSmallDur);
  set('formAqThreshold', db.aqThreshold);
  set('formTankThreshold', db.tankThreshold);
  set('formHeaterThreshold', db.heaterThreshold);
  set('formRefillDuration', db.refillDuration);
  set('formLimFlood', db.limFlood);
  const email = document.getElementById('formEmailAddress'); if (email) email.value = db.emailAddress || '';
  const chk = document.getElementById('formEmailEnabled'); if (chk) chk.checked = !!db.emailEnabled;
}

async function submitDbVars(ev) {
  ev.preventDefault();
  const status = document.getElementById('dbvarsStatus');
  try {
    const params = new URLSearchParams();
    const add = (k, id) => { const el = document.getElementById(id); if (el && el.value !== '') params.append(k, String(el.value)); };
    add('feedMorning', 'formFeedMorning');
    add('feedNoon', 'formFeedNoon');
    add('feedEvening', 'formFeedEvening');
    add('tempsGros', 'formFeedBigDur');
    add('tempsPetits', 'formFeedSmallDur');
    add('aqThreshold', 'formAqThreshold');
    add('tankThreshold', 'formTankThreshold');
    add('chauffageThreshold', 'formHeaterThreshold');
    add('tempsRemplissageSec', 'formRefillDuration');
    add('limFlood', 'formLimFlood');
    const email = document.getElementById('formEmailAddress'); if (email && email.value) params.append('mail', email.value);
    const chk = document.getElementById('formEmailEnabled'); if (chk) params.append('mailNotif', chk.checked ? 'checked' : '');
    const resp = await fetch('/dbvars/update', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: params.toString() });
    if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
    const js = await resp.json();
    if (status) status.textContent = js.status === 'OK' ? 'Enregistré.' : 'Erreur.';
    updateDatabaseVariables().catch(()=>{});
  } catch (e) {
    if (status) status.textContent = `Erreur: ${e.message}`;
  }
  return false;
}

// (Supprimé: doublons de action/toggleRelay/mailTest)

// Initialisation
document.addEventListener('DOMContentLoaded', function() {
  console.log('DEBUG: DOMContentLoaded déclenché');
  try {
    // Récupérer et afficher la version firmware
    try {
      fetch('/version', { cache: 'no-store' })
        .then(r => r.ok ? r.json() : Promise.reject(new Error('HTTP ' + r.status)))
        .then(j => {
          const v = j && j.version ? String(j.version) : '--';
          const el = document.getElementById('fwVersion');
          if (el) el.textContent = v;
          try { document.title = `FFP3 Dashboard v${v}`; } catch(e){}
        })
        .catch(e => console.warn('Version fetch failed:', e.message));
    } catch(e) { console.warn('Version init error:', e); }

    initCharts();
    
    // Chargement immédiat et parallèle pour un affichage instantané
    const immediateLoad = async () => {
      console.log('DEBUG: Chargement immédiat des données...');
      
      // Chargement parallèle immédiat
      const [sensorData, dbVars] = await Promise.allSettled([
        fetch('/json', { cache: 'no-store' }).then(r => r.json()),
        fetch('/dbvars', { cache: 'no-store' }).then(r => r.json())
      ]);
      
      // Affichage immédiat des données disponibles
      if (sensorData.status === 'fulfilled') {
        console.log('DEBUG: Données capteurs chargées immédiatement');
        updateSensorDisplay(sensorData.value);
        updateConnectionStatus(true);
      } else {
        console.log('DEBUG: Erreur chargement capteurs:', sensorData.reason);
        updateConnectionStatus(false);
      }
      
      if (dbVars.status === 'fulfilled') {
        console.log('DEBUG: Variables BDD chargées immédiatement');
        updateDatabaseDisplay(dbVars.value);
      } else {
        console.log('DEBUG: Erreur chargement BDD:', dbVars.reason);
      }
    };
    
    // Exécution immédiate
    immediateLoad();
    
    // Démarrer WebSocket et réduire le polling
    startWebSocket();
    console.log('DEBUG: refresh() initial appelé');
    setInterval(refresh, 5000);
    
    // Activer le comportement d'onglets sans dépendre de Bootstrap JS
    const navLinks = document.querySelectorAll('[data-bs-toggle="tab"]');
    const tabPanes = document.querySelectorAll('.tab-pane');
    navLinks.forEach(link => {
      link.addEventListener('click', (ev) => {
        ev.preventDefault();
        const targetSel = link.getAttribute('data-bs-target');
        if (!targetSel) return;
        // nav-link active
        navLinks.forEach(l => l.classList.remove('active'));
        link.classList.add('active');
        // tab-pane show active
        tabPanes.forEach(p => { p.classList.remove('show'); p.classList.remove('active'); });
        const target = document.querySelector(targetSel);
        if (target) { target.classList.add('show'); target.classList.add('active'); }
      });
    });
    setInterval(() => updateDatabaseVariables().catch(err => showError(`Erreur BDD: ${err.message}`)), 10000); // Réduit à 10s
    console.log('DEBUG: Initialisation terminée');
  } catch (error) {
    console.error('DEBUG: Erreur dans DOMContentLoaded:', error);
  }
});
</script>
<style>
:root {
  --primary-color: #0d6efd;
  --success-color: #198754;
  --warning-color: #ffc107;
  --danger-color: #dc3545;
  --info-color: #0dcaf0;
  --dark-color: #212529;
  --light-color: #f8f9fa;
}

body {
  font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif;
  background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
  min-height: 100vh;
}

.main-container {
  background: rgba(255, 255, 255, 0.95);
  backdrop-filter: blur(10px);
  border-radius: 20px;
  box-shadow: 0 20px 40px rgba(0,0,0,0.1);
  margin: 20px auto;
  padding: 30px;
}

.header {
  text-align: center;
  margin-bottom: 30px;
  padding-bottom: 20px;
  border-bottom: 2px solid #e9ecef;
}

.header h1 {
  color: var(--primary-color);
  font-weight: 700;
  margin-bottom: 10px;
}

.status-indicator {
  display: inline-block;
  width: 12px;
  height: 12px;
  border-radius: 50%;
  margin-right: 8px;
}

.status-online { background-color: var(--success-color); }
.status-offline { background-color: var(--danger-color); }

.card {
  border: none;
  border-radius: 15px;
  box-shadow: 0 5px 15px rgba(0,0,0,0.08);
  transition: transform 0.3s ease, box-shadow 0.3s ease;
  margin-bottom: 20px;
}

.card:hover {
  transform: translateY(-5px);
  box-shadow: 0 10px 25px rgba(0,0,0,0.15);
}

.card-header {
  background: linear-gradient(135deg, var(--primary-color), #0a58ca);
  color: white;
  border-radius: 15px 15px 0 0 !important;
  font-weight: 600;
  padding: 15px 20px;
}

.card-body {
  padding: 20px;
}

.badge {
  font-size: 0.85em;
  padding: 8px 12px;
  border-radius: 20px;
  font-weight: 500;
}

.btn {
  border-radius: 25px;
  font-weight: 500;
  padding: 10px 20px;
  transition: all 0.3s ease;
  border: 2px solid transparent;
}

.btn:hover {
  transform: translateY(-2px);
  box-shadow: 0 5px 15px rgba(0,0,0,0.2);
}

.chart-container {
  background: white;
  border-radius: 15px;
  padding: 20px;
  box-shadow: 0 5px 15px rgba(0,0,0,0.08);
  margin-bottom: 20px;
}

.loading {
  text-align: center;
  padding: 20px;
  color: #6c757d;
}

.error {
  color: var(--danger-color);
  background: #f8d7da;
  padding: 10px;
  border-radius: 5px;
  margin: 10px 0;
}

.nav-tabs {
  border-bottom: 2px solid #e9ecef;
  margin-bottom: 30px;
  list-style: none;
  padding-left: 0;
  display: flex;
  flex-wrap: wrap;
}

.nav-tabs .nav-link {
  border: none;
  border-radius: 25px;
  margin-right: 10px;
  padding: 12px 24px;
  font-weight: 500;
  transition: all 0.3s ease;
}

.nav-tabs .nav-link.active {
  background: var(--primary-color);
  color: white;
}

.nav-tabs .nav-link:hover:not(.active) {
  background: #e9ecef;
  color: var(--primary-color);
}

.list-group-item {
  border: none;
  border-bottom: 1px solid #f1f3f4;
  padding: 15px 20px;
  transition: background-color 0.3s ease;
}

.list-group-item:hover {
  background-color: #f8f9fa;
}

.tab-content {
  min-height: 400px;
}

@media (max-width: 768px) {
  .main-container {
    margin: 10px;
    padding: 20px;
  }
  
  .btn {
    margin-bottom: 10px;
  }
}

/* Responsive & overflow fixes (fallback if Bootstrap grid is absent) */
html, body { max-width: 100%; overflow-x: hidden; }
.container-fluid, .main-container { max-width: 100%; overflow-x: hidden; }
.row { display: flex; flex-wrap: wrap; margin-left: -12px; margin-right: -12px; }
[class^="col-"], [class*=" col-"] { padding-left: 12px; padding-right: 12px; box-sizing: border-box; }
/* Default to full width on very small screens */
[class*="col-"] { flex: 0 0 100%; max-width: 100%; }
@media (min-width: 576px) { .col-sm-6 { flex: 0 0 50%; max-width: 50%; } }
@media (min-width: 768px) { .col-md-3 { flex: 0 0 25%; max-width: 25%; } }
@media (min-width: 992px) { .col-lg-4 { flex: 0 0 33.333%; max-width: 33.333%; } .col-lg-8 { flex: 0 0 66.666%; max-width: 66.666%; } }
/* Media elements should never overflow */
img, canvas, svg, video { max-width: 100%; height: auto; display: block; }
/* Avoid flex children overflow in list items */
.list-group-item .d-flex { min-width: 0; }
/* Allow wrapping inside buttons/badges on narrow screens */
.btn { white-space: normal; }
.badge { white-space: normal; }
</style>
</head>
<body>
<div class="container-fluid">
  <div class="main-container">
    <div class="header">
      <h1>🐠 FFP3 Dashboard</h1>
      <p class="text-muted">Système d'Aquaponie Intelligent v<span id="fwVersion">--</span></p>
      <div class="d-flex justify-content-center align-items-center">
        <span class="status-indicator status-online" id="connectionStatus"></span>
        <span id="lastUpdate" class="text-muted">Dernière mise à jour: --</span>
      </div>
    </div>
    
    <ul class="nav nav-tabs" id="mainTabs" role="tablist">
      <li class="nav-item" role="presentation">
        <button class="nav-link active" id="dashboard-tab" data-bs-toggle="tab" data-bs-target="#dashboard" type="button" role="tab">
          📊 Dashboard
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <button class="nav-link" id="variables-tab" data-bs-toggle="tab" data-bs-target="#variables" type="button" role="tab">
          🗄️ Variables BDD
        </button>
      </li>
      <li class="nav-item" role="presentation">
        <a class="nav-link" href="/diag" target="_blank">
          🔍 Diagnostic
        </a>
      </li>
      <li class="nav-item" role="presentation">
        <a class="nav-link" href="/update" target="_blank">
          🔄 OTA
        </a>
      </li>
      <li class="nav-item" role="presentation">
        <a class="nav-link" href="/json" target="_blank">
          📄 JSON
        </a>
      </li>
</ul>

    <div class="tab-content" id="mainTabContent">
      <!-- Dashboard Tab -->
      <div class="tab-pane fade show active" id="dashboard" role="tabpanel">
        <div class="row g-4">
          <!-- Mesures Card -->
          <div class="col-lg-4">
      <div class="card">
              <div class="card-header">
                <h5 class="mb-0">🌡️ Mesures en Temps Réel</h5>
              </div>
        <div class="card-body">
                <div class="list-group list-group-flush">
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">💧</span>
                      <span>Température eau</span>
                    </div>
                    <span class="badge bg-primary" id="tWater">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">🌡️</span>
                      <span>Température air</span>
                    </div>
                    <span class="badge bg-info" id="tAir">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">💨</span>
                      <span>Humidité</span>
                    </div>
                    <span class="badge bg-primary" id="humid">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">🐠</span>
                      <span>Aquarium</span>
                    </div>
                    <span class="badge bg-secondary" id="wlAqua">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">📦</span>
                      <span>Réserve</span>
                    </div>
                    <span class="badge bg-secondary" id="wlTank">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">🌱</span>
                      <span>Potager</span>
                    </div>
                    <span class="badge bg-secondary" id="wlPota">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">☀️</span>
                      <span>Luminosité</span>
                    </div>
                    <span class="badge bg-warning text-dark" id="lumi">--</span>
                  </div>
                  <div class="list-group-item d-flex justify-content-between align-items-center">
                    <div class="d-flex align-items-center">
                      <span class="me-2">⚡</span>
                      <span>Tension (V)</span>
                    </div>
                    <span class="badge bg-success" id="vin">--</span>
        </div>
      </div>
    </div>
    </div>
  </div>

          <!-- Graphiques Card -->
          <div class="col-lg-8">
            <div class="chart-container">
              <h5 class="mb-3">📈 Graphiques Temps Réel</h5>
              <canvas id="chartTemp" height="200"></canvas>
            </div>
            <div class="chart-container">
              <h5 class="mb-3">💧 Niveaux d'Eau</h5>
              <canvas id="chartWater" height="200"></canvas>
            </div>
          </div>
        </div>

        <!-- Actionneurs Section -->
        <div class="row g-4 mt-4">
          <div class="col-12">
            <div class="card">
              <div class="card-header">
                <h5 class="mb-0">🎛️ Contrôles Manuels</h5>
              </div>
              <div class="card-body">
  <div class="row g-3">
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-primary w-100" onclick="action('feedSmall')" id="btnFeedSmall">
                      🐟 Nourrir Petits
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-primary w-100" onclick="action('feedBig')" id="btnFeedBig">
                      🐠 Nourrir Gros
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-warning w-100" onclick="toggleRelay('light')" id="btnLight">
                      💡 Lumière
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-info w-100" onclick="toggleRelay('pumpTank')" id="btnPumpTank">
                      📦 Pompe Réserve
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-info w-100" onclick="toggleRelay('pumpAqua')" id="btnPumpAqua">
                      🐠 Pompe Aqua
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-danger w-100" onclick="toggleRelay('heater')" id="btnHeater">
                      🔥 Chauffage
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-success w-100" onclick="mailTest()" id="btnMailTest">
                      📧 Test Mail
                    </button>
    </div>
                  <div class="col-md-3 col-sm-6">
                    <button class="btn btn-outline-secondary w-100" onclick="refresh()" id="btnRefresh">
                      🔄 Actualiser
                    </button>
    </div>
  </div>

                <!-- Status Indicators -->
                <div class="row g-3 mt-4">
                  <div class="col-12">
                    <h6 class="text-muted mb-3">État des Équipements</h6>
                    <div class="d-flex flex-wrap gap-3">
                      <div class="d-flex align-items-center">
                        <span class="me-2">📦</span>
                        <span class="me-2">Pompe Réserve:</span>
                        <span id="statusPump" class="badge bg-secondary">Inconnu</span>
                      </div>
                      <div class="d-flex align-items-center">
                        <span class="me-2">🐠</span>
                        <span class="me-2">Pompe Aqua:</span>
                        <span id="statusPumpAqua" class="badge bg-secondary">Inconnu</span>
                      </div>
                      <div class="d-flex align-items-center">
                        <span class="me-2">🔥</span>
                        <span class="me-2">Chauffage:</span>
                        <span id="statusHeater" class="badge bg-secondary">Inconnu</span>
                      </div>
                      <div class="d-flex align-items-center">
                        <span class="me-2">💡</span>
                        <span class="me-2">Lumière:</span>
                        <span id="statusLight" class="badge bg-secondary">Inconnu</span>
                      </div>
        </div>
      </div>
    </div>
        </div>
      </div>
    </div>
  </div>
</div>

      <!-- Variables BDD Tab -->
      <div class="tab-pane fade" id="variables" role="tabpanel">
        <div class="row g-4">
          <div class="col-12">
      <div class="card">
              <div class="card-header">
                <h5 class="mb-0">🗄️ Variables Base de Données Distante - Vue Détaillée</h5>
              </div>
              <div class="card-body">
                <div class="row g-4">
                  <div class="col-lg-6">
                    <div class="card border-0 bg-light">
        <div class="card-body">
                        <h6 class="card-title text-primary">🍽️ Configuration Nourrissage</h6>
                        <div class="list-group list-group-flush">
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🌅</span>
                              <span>Heure matin</span>
                            </div>
                            <span class="badge bg-info" id="dbTabFeedMorning">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">☀️</span>
                              <span>Heure midi</span>
                            </div>
                            <span class="badge bg-info" id="dbTabFeedNoon">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🌙</span>
                              <span>Heure soir</span>
                            </div>
                            <span class="badge bg-info" id="dbTabFeedEvening">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🐠</span>
                              <span>Durée gros</span>
                            </div>
                            <span class="badge bg-warning" id="dbTabFeedBigDur">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🐟</span>
                              <span>Durée petits</span>
                            </div>
                            <span class="badge bg-warning" id="dbTabFeedSmallDur">--</span>
                          </div>
                        </div>
        </div>
      </div>
    </div>
                  
                  <div class="col-lg-6">
                    <div class="card border-0 bg-light">
        <div class="card-body">
                        <h6 class="card-title text-primary">⚙️ Seuils & Configuration</h6>
                        <div class="list-group list-group-flush">
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🐠</span>
                              <span>Seuil Aquarium</span>
                            </div>
                            <span class="badge bg-secondary" id="dbTabAqThreshold">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">📦</span>
                              <span>Seuil Réservoir</span>
                            </div>
                            <span class="badge bg-secondary" id="dbTabTankThreshold">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">🔥</span>
                              <span>Seuil Chauffage</span>
                            </div>
                            <span class="badge bg-danger" id="dbTabHeaterThreshold">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">⏱️</span>
                              <span>Durée Remplissage</span>
                            </div>
                            <span class="badge bg-primary" id="dbTabRefillDuration">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">⚠️</span>
                              <span>Limite Inondation</span>
                            </div>
                            <span class="badge bg-warning" id="dbTabLimFlood">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">📧</span>
                              <span>Email</span>
                            </div>
                            <span class="badge bg-secondary" id="dbTabEmailAddress">--</span>
                          </div>
                          <div class="list-group-item d-flex justify-content-between align-items-center">
                            <div class="d-flex align-items-center">
                              <span class="me-2">✉️</span>
                              <span>Notifications Email</span>
                            </div>
                            <span class="badge bg-secondary" id="dbTabEmailEnabled">--</span>
                          </div>
                        </div>
        </div>
      </div>
    </div>
  </div>

                <div class="row g-4 mt-3">
                  <div class="col-lg-6">
                    <div class="card border-0 bg-light">
        <div class="card-body">
                        <h6 class="card-title text-primary">🏷️ État Nourrissage</h6>
                        <div class="d-flex justify-content-around">
                          <div class="text-center">
                            <div class="mb-2">🌅</div>
              <span id="dbTabBouffeMatin" class="badge bg-secondary">Matin</span>
            </div>
                          <div class="text-center">
                            <div class="mb-2">☀️</div>
              <span id="dbTabBouffeMidi" class="badge bg-secondary">Midi</span>
            </div>
                          <div class="text-center">
                            <div class="mb-2">🌙</div>
              <span id="dbTabBouffeSoir" class="badge bg-secondary">Soir</span>
            </div>
          </div>
        </div>
      </div>
    </div>
                  
                  <div class="col-lg-6">
                    <div class="card border-0 bg-light">
        <div class="card-body">
                        <h6 class="card-title text-primary">📝 Modifier Variables (Local → Serveur)</h6>
                        <form id="dbvarsForm" onsubmit="return submitDbVars(event)">
                          <div class="row g-2">
                            <div class="col-6">
                              <label class="form-label">Heure matin</label>
                              <input class="form-control" type="number" id="formFeedMorning" min="0" max="23">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Heure midi</label>
                              <input class="form-control" type="number" id="formFeedNoon" min="0" max="23">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Heure soir</label>
                              <input class="form-control" type="number" id="formFeedEvening" min="0" max="23">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Durée gros (s)</label>
                              <input class="form-control" type="number" id="formFeedBigDur" min="0" max="120">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Durée petits (s)</label>
                              <input class="form-control" type="number" id="formFeedSmallDur" min="0" max="120">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Seuil Aquarium (cm)</label>
                              <input class="form-control" type="number" id="formAqThreshold" min="0" max="1000">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Seuil Réservoir (cm)</label>
                              <input class="form-control" type="number" id="formTankThreshold" min="0" max="1000">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Seuil Chauffage (°C)</label>
                              <input class="form-control" type="number" step="0.1" id="formHeaterThreshold">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Durée Remplissage (s)</label>
                              <input class="form-control" type="number" id="formRefillDuration" min="0" max="3600">
                            </div>
                            <div class="col-6">
                              <label class="form-label">Limite Inondation (cm)</label>
                              <input class="form-control" type="number" id="formLimFlood" min="0" max="1000">
                            </div>
                            <div class="col-12">
                              <label class="form-label">Email</label>
                              <input class="form-control" type="email" id="formEmailAddress">
                            </div>
                            <div class="col-12 form-check mt-2">
                              <input class="form-check-input" type="checkbox" id="formEmailEnabled">
                              <label class="form-check-label" for="formEmailEnabled">Notifications email activées</label>
                            </div>
                          </div>
                          <div class="mt-3 d-flex gap-2">
                            <button class="btn btn-primary" type="submit">Enregistrer</button>
                            <button class="btn btn-secondary" type="button" onclick="fillFormFromDb()">Recharger</button>
                          </div>
                          <div id="dbvarsStatus" class="mt-2 text-muted"></div>
                        </form>
                      </div>
                    </div>
                  </div>
                </div>
              </div>
            </div>
        </div>
      </div>
    </div>
  </div>
</div>
</div>

)rawliteral";