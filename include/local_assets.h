#pragma once
#include <Arduino.h>

const char BOOTSTRAP_CSS[] PROGMEM = R"rawliteral(
/* Bootstrap 5.3 Core Styles */
:root{--bs-blue:#0d6efd;--bs-indigo:#6610f2;--bs-purple:#6f42c1;--bs-pink:#d63384;--bs-red:#dc3545;--bs-orange:#fd7e14;--bs-yellow:#ffc107;--bs-green:#198754;--bs-teal:#20c997;--bs-cyan:#0dcaf0;--bs-white:#fff;--bs-gray:#6c757d;--bs-gray-dark:#343a40;--bs-primary:#0d6efd;--bs-secondary:#6c757d;--bs-success:#198754;--bs-info:#0dcaf0;--bs-warning:#ffc107;--bs-danger:#dc3545;--bs-light:#f8f9fa;--bs-dark:#212529;--bs-primary-rgb:13,110,253;--bs-secondary-rgb:108,117,125;--bs-success-rgb:25,135,84;--bs-info-rgb:13,202,240;--bs-warning-rgb:255,193,7;--bs-danger-rgb:220,53,69;--bs-light-rgb:248,249,250;--bs-dark-rgb:33,37,41;--bs-white-rgb:255,255,255;--bs-black-rgb:0,0,0;--bs-body-color-rgb:33,37,41;--bs-body-bg-rgb:255,255,255;--bs-font-sans-serif:system-ui,-apple-system,"Segoe UI",Roboto,"Helvetica Neue",Arial,"Noto Sans","Liberation Sans",sans-serif,"Apple Color Emoji","Segoe UI Emoji","Segoe UI Symbol","Noto Color Emoji";--bs-font-monospace:SFMono-Regular,Menlo,Monaco,Consolas,"Liberation Mono","Courier New",monospace;--bs-gradient:linear-gradient(180deg,rgba(255,255,255,0.15),rgba(255,255,255,0));--bs-body-font-family:var(--bs-font-sans-serif);--bs-body-font-size:1rem;--bs-body-font-weight:400;--bs-body-line-height:1.5;--bs-body-color:#212529;--bs-body-bg:#fff;--bs-border-width:1px;--bs-border-style:solid;--bs-border-color:#dee2e6;--bs-border-color-translucent:rgba(0,0,0,0.175);--bs-border-radius:0.375rem;--bs-border-radius-sm:0.25rem;--bs-border-radius-lg:0.5rem;--bs-border-radius-xl:1rem;--bs-border-radius-2xl:2rem;--bs-border-radius-pill:50rem;--bs-box-shadow:0 0.125rem 0.25rem rgba(0,0,0,0.075);--bs-box-shadow-sm:0 0.125rem 0.25rem rgba(0,0,0,0.075);--bs-box-shadow-lg:0 1rem 3rem rgba(0,0,0,0.175);--bs-box-shadow-inset:inset 0 1px 2px rgba(0,0,0,0.075);--bs-focus-ring-width:0.25rem;--bs-focus-ring-opacity:0.25;--bs-focus-ring-color:rgba(13,110,253,0.25);--bs-form-valid-color:#198754;--bs-form-valid-border-color:#198754;--bs-form-invalid-color:#dc3545;--bs-form-invalid-border-color:#dc3545}

*,*::before,*::after{box-sizing:border-box}

body{margin:0;font-family:var(--bs-body-font-family);font-size:var(--bs-body-font-size);font-weight:var(--bs-body-font-weight);line-height:var(--bs-body-line-height);color:var(--bs-body-color);background-color:var(--bs-body-bg);-webkit-text-size-adjust:100%;-webkit-tap-highlight-color:rgba(0,0,0,0)}

.container,.container-fluid{width:100%;padding-right:var(--bs-gutter-x,0.75rem);padding-left:var(--bs-gutter-x,0.75rem);margin-right:auto;margin-left:auto}

@media (min-width:576px){.container{max-width:540px}}
@media (min-width:768px){.container{max-width:720px}}
@media (min-width:992px){.container{max-width:960px}}
@media (min-width:1200px){.container{max-width:1140px}}
@media (min-width:1400px){.container{max-width:1320px}}

.row{--bs-gutter-x:1.5rem;--bs-gutter-y:0;display:flex;flex-wrap:wrap;margin-top:calc(-1 * var(--bs-gutter-y));margin-right:calc(-0.5 * var(--bs-gutter-x));margin-left:calc(-0.5 * var(--bs-gutter-x))}
.row>*{flex-shrink:0;width:100%;max-width:100%;padding-right:calc(var(--bs-gutter-x) * 0.5);padding-left:calc(var(--bs-gutter-x) * 0.5);margin-top:var(--bs-gutter-y)}

.col{flex:1 0 0%}
.col-1{flex:0 0 auto;width:8.33333333%}
.col-2{flex:0 0 auto;width:16.66666667%}
.col-3{flex:0 0 auto;width:25%}
.col-4{flex:0 0 auto;width:33.33333333%}
.col-6{flex:0 0 auto;width:50%}
.col-12{flex:0 0 auto;width:100%}

@media (min-width:576px){
.col-sm{flex:1 0 0%}
.col-sm-6{flex:0 0 auto;width:50%}
}

@media (min-width:768px){
.col-md{flex:1 0 0%}
.col-md-3{flex:0 0 auto;width:25%}
.col-md-6{flex:0 0 auto;width:50%}
.col-md-9{flex:0 0 auto;width:75%}
}

@media (min-width:992px){
.col-lg{flex:1 0 0%}
.col-lg-4{flex:0 0 auto;width:33.33333333%}
.col-lg-6{flex:0 0 auto;width:50%}
.col-lg-8{flex:0 0 auto;width:66.66666667%}
}

.g-3{--bs-gutter-x:1rem;--bs-gutter-y:1rem}
.g-4{--bs-gutter-x:1.5rem;--bs-gutter-y:1.5rem}

.d-flex{display:flex!important}
.d-block{display:block!important}
.d-none{display:none!important}

.flex-wrap{flex-wrap:wrap!important}
.justify-content-center{justify-content:center!important}
.justify-content-between{justify-content:space-between!important}
.align-items-center{align-items:center!important}

.text-center{text-align:center!important}
.text-muted{color:#6c757d!important}
.text-primary{color:var(--bs-primary)!important}

.mb-0{margin-bottom:0!important}
.mb-2{margin-bottom:0.5rem!important}
.mb-3{margin-bottom:1rem!important}
.mb-4{margin-bottom:1.5rem!important}
.mt-3{margin-top:1rem!important}
.mt-4{margin-top:1.5rem!important}
.me-2{margin-right:0.5rem!important}

.p-0{padding:0!important}
.py-4{padding-top:1.5rem!important;padding-bottom:1.5rem!important}

.w-100{width:100%!important}

.card{position:relative;display:flex;flex-direction:column;min-width:0;word-wrap:break-word;background-color:var(--bs-card-bg);background-clip:border-box;border:var(--bs-card-border-width) solid var(--bs-card-border-color);border-radius:var(--bs-card-border-radius)}
.card-body{flex:1 1 auto;padding:var(--bs-card-spacer-y) var(--bs-card-spacer-x);color:var(--bs-card-color)}
.card-header{padding:var(--bs-card-cap-padding-y) var(--bs-card-cap-padding-x);margin-bottom:0;color:var(--bs-card-cap-color);background-color:var(--bs-card-cap-bg);border-bottom:var(--bs-card-border-width) solid var(--bs-card-border-color)}
.card-title{margin-bottom:var(--bs-card-title-spacer-y)}

.list-group{display:flex;flex-direction:column;padding-left:0;margin-bottom:0;border-radius:var(--bs-list-group-border-radius)}
.list-group-item{position:relative;display:block;padding:var(--bs-list-group-item-padding-y) var(--bs-list-group-item-padding-x);color:var(--bs-list-group-color);text-decoration:none;background-color:var(--bs-list-group-bg);border:var(--bs-list-group-border-width) solid var(--bs-list-group-border-color)}
.list-group-flush>.list-group-item{border-width:0 0 var(--bs-list-group-border-width)}
.list-group-flush>.list-group-item:last-child{border-bottom-width:0}

.btn{display:inline-block;padding:var(--bs-btn-padding-y) var(--bs-btn-padding-x);font-family:var(--bs-btn-font-family);font-size:var(--bs-btn-font-size);font-weight:var(--bs-btn-font-weight);line-height:var(--bs-btn-line-height);color:var(--bs-btn-color);text-align:center;text-decoration:none;vertical-align:middle;cursor:pointer;user-select:none;border:var(--bs-btn-border-width) solid var(--bs-btn-border-color);border-radius:var(--bs-btn-border-radius);background-color:var(--bs-btn-bg);transition:color 0.15s ease-in-out,background-color 0.15s ease-in-out,border-color 0.15s ease-in-out,box-shadow 0.15s ease-in-out}
.btn:hover{color:var(--bs-btn-hover-color);background-color:var(--bs-btn-hover-bg);border-color:var(--bs-btn-hover-border-color)}
.btn-primary{--bs-btn-color:#fff;--bs-btn-bg:#0d6efd;--bs-btn-border-color:#0d6efd;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#0b5ed7;--bs-btn-hover-border-color:#0a58ca}
.btn-secondary{--bs-btn-color:#fff;--bs-btn-bg:#6c757d;--bs-btn-border-color:#6c757d;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#5c636a;--bs-btn-hover-border-color:#565e64}
.btn-success{--bs-btn-color:#fff;--bs-btn-bg:#198754;--bs-btn-border-color:#198754;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#157347;--bs-btn-hover-border-color:#146c43}
.btn-info{--bs-btn-color:#000;--bs-btn-bg:#0dcaf0;--bs-btn-border-color:#0dcaf0;--bs-btn-hover-color:#000;--bs-btn-hover-bg:#3dd5f3;--bs-btn-hover-border-color:#25cff2}
.btn-warning{--bs-btn-color:#000;--bs-btn-bg:#ffc107;--bs-btn-border-color:#ffc107;--bs-btn-hover-color:#000;--bs-btn-hover-bg:#ffca2c;--bs-btn-hover-border-color:#ffc720}
.btn-danger{--bs-btn-color:#fff;--bs-btn-bg:#dc3545;--bs-btn-border-color:#dc3545;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#bb2d3b;--bs-btn-hover-border-color:#b02a37}
.btn-outline-primary{--bs-btn-color:#0d6efd;--bs-btn-border-color:#0d6efd;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#0d6efd;--bs-btn-hover-border-color:#0d6efd}
.btn-outline-secondary{--bs-btn-color:#6c757d;--bs-btn-border-color:#6c757d;--bs-btn-hover-color:#fff;--bs-btn-hover-bg:#6c757d;--bs-btn-hover-border-color:#6c757d}

.badge{display:inline-block;padding:var(--bs-badge-padding-y) var(--bs-badge-padding-x);font-size:var(--bs-badge-font-size);font-weight:var(--bs-badge-font-weight);line-height:1;color:var(--bs-badge-color);text-align:center;white-space:nowrap;vertical-align:baseline;border-radius:var(--bs-badge-border-radius)}
.bg-primary{--bs-bg-opacity:1;background-color:rgba(var(--bs-primary-rgb),var(--bs-bg-opacity))!important}
.bg-secondary{--bs-bg-opacity:1;background-color:rgba(var(--bs-secondary-rgb),var(--bs-bg-opacity))!important}
.bg-success{--bs-bg-opacity:1;background-color:rgba(var(--bs-success-rgb),var(--bs-bg-opacity))!important}
.bg-info{--bs-bg-opacity:1;background-color:rgba(var(--bs-info-rgb),var(--bs-bg-opacity))!important}
.bg-warning{--bs-bg-opacity:1;background-color:rgba(var(--bs-warning-rgb),var(--bs-bg-opacity))!important}
.bg-danger{--bs-bg-opacity:1;background-color:rgba(var(--bs-danger-rgb),var(--bs-bg-opacity))!important}
.bg-light{--bs-bg-opacity:1;background-color:rgba(var(--bs-light-rgb),var(--bs-bg-opacity))!important}

.text-dark{color:#000!important}

.nav{display:flex;flex-wrap:wrap;padding-left:0;margin-bottom:0;list-style:none}
.nav-tabs{border-bottom:var(--bs-nav-tabs-border-width) solid var(--bs-nav-tabs-border-color)}
.nav-tabs .nav-link{margin-bottom:calc(-1 * var(--bs-nav-tabs-border-width));background:none;border:var(--bs-nav-tabs-border-width) solid transparent;border-top-left-radius:var(--bs-nav-tabs-border-radius);border-top-right-radius:var(--bs-nav-tabs-border-radius)}
.nav-tabs .nav-link:hover{isolation:isolate;border-color:var(--bs-nav-tabs-link-hover-border-color)}
.nav-tabs .nav-link.active{color:var(--bs-nav-tabs-link-active-color);background-color:var(--bs-nav-tabs-link-active-bg);border-color:var(--bs-nav-tabs-link-active-border-color)}
.nav-link{display:block;padding:var(--bs-nav-link-padding-y) var(--bs-nav-link-padding-x);font-size:var(--bs-nav-link-font-size);font-weight:var(--bs-nav-link-font-weight);color:var(--bs-nav-link-color);text-decoration:none;transition:color 0.15s ease-in-out,background-color 0.15s ease-in-out,border-color 0.15s ease-in-out}
.nav-link:hover{color:var(--bs-nav-link-hover-color)}
.nav-link.active{color:var(--bs-nav-link-active-color)}

.tab-content>.tab-pane{display:none}
.tab-content>.tab-pane.active{display:block}

.border-0{border:0!important}

.gap-3{gap:1rem!important}

@media (max-width:767.98px){
.d-sm-none{display:none!important}
}
)rawliteral";

const char CHART_JS[] PROGMEM = R"rawliteral(
// Chart.js amélioré pour le dashboard FFP3
class Chart {
  constructor(ctx, config) {
    this.ctx = ctx;
    this.data = config.data;
    this.options = config.options || {};
    this.canvas = ctx.canvas;
    this.width = this.canvas.width;
    this.height = this.canvas.height;
  }
  
  update(animation = 'none') {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.width, this.height);
    
    if (this.data.labels.length < 2) return;
    
    const datasets = this.data.datasets;
    const labels = this.data.labels;
    
    // Calculer les marges
    const margin = { top: 20, right: 20, bottom: 30, left: 40 };
    const chartWidth = this.width - margin.left - margin.right;
    const chartHeight = this.height - margin.top - margin.bottom;
    
    // Dessiner les axes
    this.drawAxes(ctx, margin, chartWidth, chartHeight);
    
    // Dessiner les données
    datasets.forEach((dataset, index) => {
      this.drawDataset(ctx, dataset, labels, margin, chartWidth, chartHeight, index);
    });
    
    // Dessiner la légende
    this.drawLegend(ctx, datasets, margin);
  }
  
  drawAxes(ctx, margin, chartWidth, chartHeight) {
    ctx.strokeStyle = '#e0e0e0';
    ctx.lineWidth = 1;
    
    // Axe Y
    ctx.beginPath();
    ctx.moveTo(margin.left, margin.top);
    ctx.lineTo(margin.left, margin.top + chartHeight);
    ctx.stroke();
    
    // Axe X
    ctx.beginPath();
    ctx.moveTo(margin.left, margin.top + chartHeight);
    ctx.lineTo(margin.left + chartWidth, margin.top + chartHeight);
    ctx.stroke();
    
    // Grille horizontale + labels (0-40)
    ctx.strokeStyle = '#f0f0f0';
    ctx.lineWidth = 0.5;
    const yMax = 40;
    const steps = 8; // 0,5,10,...,40
    for (let i = 0; i <= steps; i++) {
      const y = margin.top + (chartHeight / steps) * i;
      ctx.beginPath();
      ctx.moveTo(margin.left, y);
      ctx.lineTo(margin.left + chartWidth, y);
      ctx.stroke();
      ctx.fillStyle = '#666';
      ctx.font = '10px Arial';
      const val = yMax - Math.round((yMax / steps) * i);
      ctx.fillText(String(val), margin.left - 24, y + 3);
    }
    // Label axe Y
    ctx.save();
    ctx.fillStyle = '#333';
    ctx.font = '12px Arial';
    ctx.translate(12, margin.top + chartHeight / 2);
    ctx.rotate(-Math.PI / 2);
    ctx.fillText('Valeur', 0, 0);
    ctx.restore();
  }
  
  drawDataset(ctx, dataset, labels, margin, chartWidth, chartHeight, index) {
    const values = dataset.data;
    if (values.length === 0) return;
    
    // Plage forcée 0-40
    const min = 0;
    const max = 40;
    const range = 40;
    
    ctx.strokeStyle = dataset.borderColor || '#000';
    ctx.fillStyle = dataset.backgroundColor || 'rgba(0,0,0,0.1)';
    ctx.lineWidth = 2;
    
    // Dessiner la ligne
    ctx.beginPath();
    for (let i = 0; i < values.length; i++) {
      const x = margin.left + (chartWidth / (values.length - 1)) * i;
      const v = values[i];
      const clamped = Math.max(min, Math.min(max, (isNaN(v)?0:v)));
      const y = margin.top + chartHeight - ((clamped - min) / range) * chartHeight;
      
      if (i === 0) {
        ctx.moveTo(x, y);
      } else {
        ctx.lineTo(x, y);
      }
    }
    ctx.stroke();
    
    // Remplir sous la ligne si demandé
    if (dataset.fill) {
      ctx.beginPath();
      ctx.moveTo(margin.left, margin.top + chartHeight);
      for (let i = 0; i < values.length; i++) {
        const x = margin.left + (chartWidth / (values.length - 1)) * i;
        const v = values[i];
        const clamped = Math.max(min, Math.min(max, (isNaN(v)?0:v)));
        const y = margin.top + chartHeight - ((clamped - min) / range) * chartHeight;
        ctx.lineTo(x, y);
      }
      ctx.lineTo(margin.left + chartWidth, margin.top + chartHeight);
      ctx.closePath();
      ctx.fill();
    }
    
    // Dessiner les points
    ctx.fillStyle = dataset.borderColor || '#000';
    for (let i = 0; i < values.length; i++) {
      const x = margin.left + (chartWidth / (values.length - 1)) * i;
      const v = values[i];
      const clamped = Math.max(min, Math.min(max, (isNaN(v)?0:v)));
      const y = margin.top + chartHeight - ((clamped - min) / range) * chartHeight;
      
      ctx.beginPath();
      ctx.arc(x, y, 3, 0, 2 * Math.PI);
      ctx.fill();
    }
  }
  
  drawLegend(ctx, datasets, margin) {
    const legendX = margin.left + 10;
    const legendY = 15;
    
    datasets.forEach((dataset, index) => {
      const y = legendY + index * 20;
      
      // Carré de couleur
      ctx.fillStyle = dataset.borderColor || '#000';
      ctx.fillRect(legendX, y - 5, 10, 10);
      
      // Texte
      ctx.fillStyle = '#333';
      ctx.font = '12px Arial';
      ctx.fillText(dataset.label || `Dataset ${index + 1}`, legendX + 15, y + 3);
    });
  }
}

// Fonction utilitaire pour créer un graphique
function createChart(canvasId, config) {
  const canvas = document.getElementById(canvasId);
  if (!canvas) return null;
  
  const ctx = canvas.getContext('2d');
  return new Chart(ctx, config);
}
)rawliteral"; 