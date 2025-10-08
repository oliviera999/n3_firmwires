#pragma once
#include <Arduino.h>

const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!DOCTYPE html>
<html lang="fr">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>FFP3 Dashboard v9.4</title>
<link href="/bootstrap.min.css" rel="stylesheet">
<script src="/chart.js"></script>
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
</style>
</head>
<body>
<div class="container-fluid">
  <div class="main-container">
    <div class="header">
      <h1>🐠 FFP3 Dashboard</h1>
      <p class="text-muted">Système d'Aquaponie Intelligent v9.4</p>
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
                      <span>Tension</span>
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

<script>
)rawliteral";
const char DASHBOARD_JS_INLINE[] PROGMEM = R"rawliteral(
// Dashboard JavaScript amélioré pour FFP3
let tempChart, waterChart;
let isOnline = false;
let lastUpdateTime = null;

// Initialisation des graphiques
function initCharts() {
  const ctxTemp = document.getElementById('chartTemp').getContext('2d');
  const ctxWater = document.getElementById('chartWater').getContext('2d');
  
  const dataT = { labels: [], air: [], water: [] };
  const dataW = { labels: [], aqua: [], tank: [] };
  
  tempChart = new Chart(ctxTemp, {
    type: 'line',
    data: {
      labels: dataT.labels,
      datasets: [{
        label: 'Air °C',
        data: dataT.air,
        borderColor: '#ffc107',
        backgroundColor: 'rgba(255, 193, 7, 0.1)',
        fill: true,
        tension: 0.4
      }, {
        label: 'Eau °C',
        data: dataT.water,
        borderColor: '#0dcaf0',
        backgroundColor: 'rgba(13, 202, 240, 0.1)',
        fill: true,
        tension: 0.4
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        y: {
          beginAtZero: false,
          grid: { color: 'rgba(0,0,0,0.1)' }
        },
        x: {
          grid: { color: 'rgba(0,0,0,0.1)' }
        }
      },
      plugins: {
        legend: { position: 'top' }
      }
    }
  });
  
  waterChart = new Chart(ctxWater, {
    type: 'line',
    data: {
      labels: dataW.labels,
      datasets: [{
        label: 'Aquarium cm',
        data: dataW.aqua,
        borderColor: '#198754',
        backgroundColor: 'rgba(25, 135, 84, 0.1)',
        fill: true,
        tension: 0.4
      }, {
        label: 'Réservoir cm',
        data: dataW.tank,
        borderColor: '#6f42c1',
        backgroundColor: 'rgba(111, 66, 193, 0.1)',
        fill: true,
        tension: 0.4
      }]
    },
    options: {
      responsive: true,
      maintainAspectRatio: false,
      scales: {
        y: {
          beginAtZero: true,
          grid: { color: 'rgba(0,0,0,0.1)' }
        },
        x: {
          grid: { color: 'rgba(0,0,0,0.1)' }
        }
      },
      plugins: {
        legend: { position: 'top' }
      }
    }
  });
}

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

// Fonction principale de rafraîchissement
async function refresh() {
  try {
    const response = await fetch('/json');
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    
    const data = await response.json();
    updateConnectionStatus(true);
    
    // Mise à jour des mesures
    document.getElementById('tWater').textContent = formatValue(data.tempWater);
    document.getElementById('tAir').textContent = formatValue(data.tempAir);
    document.getElementById('humid').textContent = formatValue(data.humidity);
    document.getElementById('wlAqua').textContent = formatValue(data.wlAqua, 0);
    document.getElementById('wlTank').textContent = formatValue(data.wlTank, 0);
    document.getElementById('wlPota').textContent = formatValue(data.wlPota, 0);
    document.getElementById('lumi').textContent = formatValue(data.luminosite, 0);
    document.getElementById('vin').textContent = formatValue(data.voltage, 0);
    
    // Mise à jour des statuts des équipements
    updateEquipmentStatus('statusPump', data.pumpTank, 'Pompe Réserve');
    updateEquipmentStatus('statusPumpAqua', data.pumpAqua, 'Pompe Aqua');
    updateEquipmentStatus('statusHeater', data.heater, 'Chauffage');
    updateEquipmentStatus('statusLight', data.light, 'Lumière');
    
    // Mise à jour des graphiques
    updateCharts(data);
    
    // Déclencher la récupération des variables BDD sans bloquer l'affichage
    updateDatabaseVariables().catch(err => showError(`Erreur BDD: ${err.message}`));
    
  } catch (error) {
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

// Fonction pour mettre à jour les graphiques
function updateCharts(data) {
  const now = new Date();
  const timeLabel = now.toLocaleTimeString();
  
  // Données de température
  tempChart.data.labels.push(timeLabel);
  tempChart.data.datasets[0].data.push(data.tempAir || 0);
  tempChart.data.datasets[1].data.push(data.tempWater || 0);
  
  // Données d'eau
  waterChart.data.labels.push(timeLabel);
  waterChart.data.datasets[0].data.push(data.wlAqua || 0);
  waterChart.data.datasets[1].data.push(data.wlTank || 0);
  
  // Limiter à 60 points
  const maxPoints = 60;
  if (tempChart.data.labels.length > maxPoints) {
    tempChart.data.labels.shift();
    tempChart.data.datasets[0].data.shift();
    tempChart.data.datasets[1].data.shift();
  }
  
  if (waterChart.data.labels.length > maxPoints) {
    waterChart.data.labels.shift();
    waterChart.data.datasets[0].data.shift();
    waterChart.data.datasets[1].data.shift();
  }
  
  tempChart.update('none');
  waterChart.update('none');
}

// Fonction pour mettre à jour les variables de la BDD
async function updateDatabaseVariables() {
  try {
    const response = await fetch('/dbvars');
    if (!response.ok) {
      throw new Error(`HTTP ${response.status}: ${response.statusText}`);
    }
    
    const db = await response.json();
    
    // Mise à jour des variables de nourrissage
    updateElement('dbFeedMorning', db.feedMorning);
    updateElement('dbFeedNoon', db.feedNoon);
    updateElement('dbFeedEvening', db.feedEvening);
    updateElement('dbFeedBigDur', db.feedBigDur);
    updateElement('dbFeedSmallDur', db.feedSmallDur);
    
    // Mise à jour des seuils
    updateElement('dbAqThreshold', db.aqThreshold);
    updateElement('dbTankThreshold', db.tankThreshold);
    updateElement('dbHeaterThreshold', formatValue(db.heaterThreshold));
    updateElement('dbRefillDuration', db.refillDuration);
    updateElement('dbLimFlood', db.limFlood);
    
    // Mise à jour des flags de nourrissage
    updateFeedingFlag('dbBouffeMatin', db.bouffeMatin);
    updateFeedingFlag('dbBouffeMidi', db.bouffeMidi);
    updateFeedingFlag('dbBouffeSoir', db.bouffeSoir);
    
    // Mise à jour des commandes
    updateCommandFlag('dbBouffePetits', db.bouffePetits);
    updateCommandFlag('dbBouffeGros', db.bouffeGros);
    
    // Mise à jour de l'email
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
    updateFeedingFlag('dbTabBouffeMatin', db.bouffeMatin);
    updateFeedingFlag('dbTabBouffeMidi', db.bouffeMidi);
    updateFeedingFlag('dbTabBouffeSoir', db.bouffeSoir);
    updateCommandFlag('dbTabBouffePetits', db.bouffePetits);
    updateCommandFlag('dbTabBouffeGros', db.bouffeGros);
    updateElement('dbTabEmailAddress', db.emailAddress);
    updateEmailStatus('dbTabEmailEnabled', db.emailEnabled);
    // Pré-remplissage du formulaire d'édition
    window._dbCache = db;
    if (typeof fillFormFromDb === 'function') fillFormFromDb();
    
  } catch (error) {
    showError(`Erreur BDD: ${error.message}`);
  }
}

// Fonctions utilitaires pour la mise à jour des éléments
function updateElement(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = value !== null && value !== undefined ? value : '--';
  }
}

function updateFeedingFlag(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.className = `badge ${value ? 'bg-success' : 'bg-secondary'}`;
    element.textContent = value ? 'OK' : 'En attente';
  }
}

function updateCommandFlag(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = value || '--';
    element.className = `badge ${value === '1' ? 'bg-success' : 'bg-secondary'}`;
  }
}

function updateEmailStatus(id, value) {
  const element = document.getElementById(id);
  if (element) {
    element.textContent = value ? 'Activé' : 'Désactivé';
    element.className = `badge ${value ? 'bg-success' : 'bg-secondary'}`;
  }
}

// Pré-remplir le formulaire d'édition des variables BDD
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

// Soumission du formulaire vers /dbvars/update
async function submitDbVars(ev) {
  ev.preventDefault();
  const status = document.getElementById('dbvarsStatus');
  try {
    const params = new URLSearchParams();
    const add = (k, id) => { const el = document.getElementById(id); if (el && el.value !== '') params.append(k, String(el.value)); };
    add('feedMorning', 'formFeedMorning');
    add('feedNoon', 'formFeedNoon');
    add('feedEvening', 'formFeedEvening');
    add('feedBigDur', 'formFeedBigDur');
    add('feedSmallDur', 'formFeedSmallDur');
    add('aqThreshold', 'formAqThreshold');
    add('tankThreshold', 'formTankThreshold');
    add('heaterThreshold', 'formHeaterThreshold');
    add('refillDuration', 'formRefillDuration');
    add('limFlood', 'formLimFlood');
    const email = document.getElementById('formEmailAddress'); if (email && email.value) params.append('emailAddress', email.value);
    const chk = document.getElementById('formEmailEnabled'); if (chk) params.append('emailEnabled', chk.checked ? 'checked' : '');
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

// Fonctions d'action
async function action(cmd) {
  try {
    const response = await fetch(`/action?cmd=${cmd}`);
    const result = await response.text();
    console.log(`Action ${cmd}: ${result}`);
  } catch (error) {
    showError(`Erreur action ${cmd}: ${error.message}`);
  }
}

async function toggleRelay(name) {
  try {
    const response = await fetch(`/action?relay=${name}`);
    const result = await response.text();
    console.log(`Relay ${name}: ${result}`);
  } catch (error) {
    showError(`Erreur relay ${name}: ${error.message}`);
  }
}

async function mailTest() {
  try {
    const response = await fetch('/mailtest');
    const result = await response.text();
    alert(`Test Mail: ${result}`);
  } catch (error) {
    showError(`Erreur test mail: ${error.message}`);
  }
}

// Initialisation
document.addEventListener('DOMContentLoaded', function() {
  initCharts();
  refresh();
  setInterval(refresh, 5000);
  // Rafraîchir les variables BDD moins fréquemment pour alléger le réseau
  setInterval(() => updateDatabaseVariables().catch(err => showError(`Erreur BDD: ${err.message}`)), 15000);
});
</script>
</body>
</html>
)rawliteral"; 