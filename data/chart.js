// Chart.js simplifié pour ESP32 FFP3 Dashboard
(function() {
  'use strict';
  
  // Version simplifiée de Chart.js pour les graphiques de base
  window.Chart = function(ctx, config) {
    this.ctx = ctx;
    this.config = config;
    this.data = config.data;
    this.options = config.options || {};
    this.canvas = ctx.canvas;
    this.width = this.canvas.width;
    this.height = this.canvas.height;
    
    this.init();
  };
  
  Chart.prototype.init = function() {
    this.draw();
  };
  
  Chart.prototype.draw = function() {
    const ctx = this.ctx;
    ctx.clearRect(0, 0, this.width, this.height);
    
    if (this.config.type === 'line') {
      this.drawLineChart();
    }
  };
  
  Chart.prototype.drawLineChart = function() {
    const ctx = this.ctx;
    const datasets = this.data.datasets;
    const labels = this.data.labels;
    
    if (!labels.length || !datasets.length) return;
    
    const padding = 40;
    const chartWidth = this.width - 2 * padding;
    const chartHeight = this.height - 2 * padding;
    
    // Dessiner les axes
    ctx.strokeStyle = '#e0e0e0';
    ctx.lineWidth = 1;
    ctx.beginPath();
    ctx.moveTo(padding, padding);
    ctx.lineTo(padding, this.height - padding);
    ctx.lineTo(this.width - padding, this.height - padding);
    ctx.stroke();
    
    // Calculer les valeurs min/max
    let minValue = Infinity;
    let maxValue = -Infinity;
    
    datasets.forEach(dataset => {
      dataset.data.forEach(value => {
        if (value !== null && value !== undefined && !isNaN(value)) {
          minValue = Math.min(minValue, value);
          maxValue = Math.max(maxValue, value);
        }
      });
    });
    
    if (minValue === Infinity) return;
    
    const valueRange = maxValue - minValue;
    const stepX = chartWidth / Math.max(1, labels.length - 1);
    
    // Dessiner les datasets
    datasets.forEach((dataset, datasetIndex) => {
      ctx.strokeStyle = dataset.borderColor || '#000';
      ctx.lineWidth = 2;
      ctx.beginPath();
      
      let firstPoint = true;
      dataset.data.forEach((value, index) => {
        if (value === null || value === undefined || isNaN(value)) return;
        
        const x = padding + index * stepX;
        const y = this.height - padding - ((value - minValue) / valueRange) * chartHeight;
        
        if (firstPoint) {
          ctx.moveTo(x, y);
          firstPoint = false;
        } else {
          ctx.lineTo(x, y);
        }
      });
      
      ctx.stroke();
      
      // Dessiner les points
      ctx.fillStyle = dataset.borderColor || '#000';
      dataset.data.forEach((value, index) => {
        if (value === null || value === undefined || isNaN(value)) return;
        
        const x = padding + index * stepX;
        const y = this.height - padding - ((value - minValue) / valueRange) * chartHeight;
        
        ctx.beginPath();
        ctx.arc(x, y, 3, 0, 2 * Math.PI);
        ctx.fill();
      });
    });
    
    // Dessiner les labels sur l'axe X (seulement les derniers)
    ctx.fillStyle = '#666';
    ctx.font = '10px Arial';
    ctx.textAlign = 'center';
    const maxLabels = 10;
    const step = Math.max(1, Math.floor(labels.length / maxLabels));
    for (let i = 0; i < labels.length; i += step) {
      const x = padding + i * stepX;
      ctx.fillText(labels[i], x, this.height - padding + 15);
    }
  };
  
  Chart.prototype.update = function(mode) {
    this.draw();
  };
  
})();