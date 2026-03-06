// Service Worker FFP3 Dashboard - v11.20
// Cache offline complet pour PWA

const CACHE_VERSION = 'ffp3-v11.20';
const CACHE_STATIC = `${CACHE_VERSION}-static`;
const CACHE_DYNAMIC = `${CACHE_VERSION}-dynamic`;
const CACHE_API = `${CACHE_VERSION}-api`;

// Fichiers à mettre en cache lors de l'installation
const STATIC_ASSETS = [
  '/',
  '/index.html',
  '/shared/common.js',
  '/shared/common.css',
  '/shared/websocket.js',
  '/pages/controles.html',
  '/pages/wifi.html',
  '/assets/css/uplot.min.css',
  '/assets/js/uplot.iife.min.js',
  '/manifest.json'
];

// Endpoints API à mettre en cache (stratégie Network First)
const API_ENDPOINTS = [
  '/json',
  '/dbvars',
  '/wifi/status',
  '/version'
];

// Endpoints à ne JAMAIS mettre en cache
const NO_CACHE_ENDPOINTS = [
  '/action',
  '/dbvars/update',
  '/wifi/connect',
  '/wifi/scan',
  '/nvs',
  '/ws'
];

// Installation du Service Worker
self.addEventListener('install', (event) => {
  console.log('[SW] Installation en cours...');
  
  event.waitUntil(
    caches.open(CACHE_STATIC)
      .then((cache) => {
        console.log('[SW] Mise en cache des assets statiques');
        return cache.addAll(STATIC_ASSETS);
      })
      .then(() => {
        console.log('[SW] Installation terminée');
        // Force l'activation immédiate
        return self.skipWaiting();
      })
      .catch((error) => {
        console.error('[SW] Erreur installation:', error);
      })
  );
});

// Activation du Service Worker
self.addEventListener('activate', (event) => {
  console.log('[SW] Activation en cours...');
  
  event.waitUntil(
    // Supprimer les anciens caches
    caches.keys()
      .then((cacheNames) => {
        return Promise.all(
          cacheNames.map((cacheName) => {
            if (cacheName.startsWith('ffp3-') && 
                cacheName !== CACHE_STATIC && 
                cacheName !== CACHE_DYNAMIC && 
                cacheName !== CACHE_API) {
              console.log('[SW] Suppression ancien cache:', cacheName);
              return caches.delete(cacheName);
            }
          })
        );
      })
      .then(() => {
        console.log('[SW] Activation terminée');
        // Prendre le contrôle immédiatement
        return self.clients.claim();
      })
  );
});

// Intercepter les requêtes
self.addEventListener('fetch', (event) => {
  const { request } = event;
  const url = new URL(request.url);
  
  // Ignorer les requêtes cross-origin
  if (url.origin !== location.origin) {
    return;
  }
  
  // Ignorer WebSocket
  if (url.pathname === '/ws' || url.protocol === 'ws:' || url.protocol === 'wss:') {
    return;
  }
  
  // Ignorer les endpoints critiques (pas de cache)
  if (NO_CACHE_ENDPOINTS.some(endpoint => url.pathname.startsWith(endpoint))) {
    console.log('[SW] Bypass cache pour:', url.pathname);
    return;
  }
  
  // Stratégie selon le type de requête
  if (STATIC_ASSETS.includes(url.pathname)) {
    // Assets statiques: Cache First
    event.respondWith(cacheFirst(request, CACHE_STATIC));
  } else if (API_ENDPOINTS.some(endpoint => url.pathname.startsWith(endpoint))) {
    // API: Network First avec fallback cache
    event.respondWith(networkFirst(request, CACHE_API));
  } else if (url.pathname.startsWith('/pages/') || 
             url.pathname.startsWith('/shared/') || 
             url.pathname.startsWith('/assets/')) {
    // Autres ressources: Cache First avec update
    event.respondWith(cacheFirst(request, CACHE_DYNAMIC));
  } else {
    // Par défaut: Network First
    event.respondWith(networkFirst(request, CACHE_DYNAMIC));
  }
});

// Stratégie Cache First
async function cacheFirst(request, cacheName) {
  try {
    const cachedResponse = await caches.match(request);
    
    if (cachedResponse) {
      // Mettre à jour le cache en arrière-plan
      fetch(request)
        .then((response) => {
          if (response && response.status === 200) {
            caches.open(cacheName).then((cache) => {
              cache.put(request, response);
            });
          }
        })
        .catch(() => {}); // Ignorer les erreurs de mise à jour
      
      return cachedResponse;
    }
    
    // Pas en cache, fetcher
    const response = await fetch(request);
    
    if (response && response.status === 200) {
      const cache = await caches.open(cacheName);
      cache.put(request, response.clone());
    }
    
    return response;
    
  } catch (error) {
    console.error('[SW] Cache First error:', error);
    
    // Fallback vers cache si erreur réseau
    const cachedResponse = await caches.match(request);
    if (cachedResponse) {
      return cachedResponse;
    }
    
    // Retourner une réponse d'erreur offline
    return new Response(
      JSON.stringify({ error: 'Offline', offline: true }),
      { 
        status: 503,
        statusText: 'Service Unavailable',
        headers: { 'Content-Type': 'application/json' }
      }
    );
  }
}

// Stratégie Network First
async function networkFirst(request, cacheName) {
  try {
    const response = await fetch(request);
    
    if (response && response.status === 200) {
      const cache = await caches.open(cacheName);
      cache.put(request, response.clone());
    }
    
    return response;
    
  } catch (error) {
    console.log('[SW] Network error, trying cache:', request.url);
    
    const cachedResponse = await caches.match(request);
    
    if (cachedResponse) {
      // Ajouter header pour indiquer que c'est du cache
      const headers = new Headers(cachedResponse.headers);
      headers.set('X-From-Cache', 'true');
      
      return new Response(cachedResponse.body, {
        status: cachedResponse.status,
        statusText: cachedResponse.statusText,
        headers: headers
      });
    }
    
    // Aucune version en cache
    if (request.url.includes('/json') || request.url.includes('/dbvars')) {
      // Pour les API, retourner des données par défaut
      return new Response(
        JSON.stringify({ 
          offline: true,
          error: 'No network connection',
          message: 'Using offline mode'
        }),
        { 
          status: 200,
          headers: { 
            'Content-Type': 'application/json',
            'X-Offline': 'true'
          }
        }
      );
    }
    
    // Pour les pages, page offline
    return new Response(
      `<!DOCTYPE html>
      <html lang="fr">
      <head>
        <meta charset="UTF-8">
        <meta name="viewport" content="width=device-width, initial-scale=1.0">
        <title>FFP3 - Hors ligne</title>
        <style>
          body {
            font-family: system-ui;
            background: #0f172a;
            color: #e5e7eb;
            display: flex;
            align-items: center;
            justify-content: center;
            min-height: 100vh;
            margin: 0;
            padding: 20px;
          }
          .offline-container {
            text-align: center;
            max-width: 400px;
          }
          h1 { color: #3b82f6; margin-bottom: 20px; }
          button {
            background: #3b82f6;
            color: white;
            border: none;
            padding: 12px 24px;
            border-radius: 8px;
            cursor: pointer;
            font-size: 16px;
            margin-top: 20px;
          }
          button:hover { background: #2563eb; }
        </style>
      </head>
      <body>
        <div class="offline-container">
          <h1>📡 Hors ligne</h1>
          <p>Impossible de se connecter à l'ESP32.</p>
          <p>Vérifiez votre connexion réseau.</p>
          <button onclick="location.reload()">🔄 Réessayer</button>
        </div>
      </body>
      </html>`,
      { 
        status: 503,
        headers: { 'Content-Type': 'text/html' }
      }
    );
  }
}

// Écouter les messages du client
self.addEventListener('message', (event) => {
  console.log('[SW] Message reçu:', event.data);
  
  if (event.data.type === 'SKIP_WAITING') {
    self.skipWaiting();
  }
  
  if (event.data.type === 'CLEAR_CACHE') {
    event.waitUntil(
      caches.keys().then((cacheNames) => {
        return Promise.all(
          cacheNames.map((cacheName) => {
            if (cacheName.startsWith('ffp3-')) {
              console.log('[SW] Clearing cache:', cacheName);
              return caches.delete(cacheName);
            }
          })
        );
      }).then(() => {
        console.log('[SW] All caches cleared');
        // Notifier le client
        self.clients.matchAll().then((clients) => {
          clients.forEach((client) => {
            client.postMessage({ type: 'CACHE_CLEARED' });
          });
        });
      })
    );
  }
  
  if (event.data.type === 'GET_CACHE_SIZE') {
    event.waitUntil(
      caches.keys().then(async (cacheNames) => {
        let totalSize = 0;
        
        for (const cacheName of cacheNames) {
          if (cacheName.startsWith('ffp3-')) {
            const cache = await caches.open(cacheName);
            const keys = await cache.keys();
            
            for (const request of keys) {
              const response = await cache.match(request);
              if (response) {
                const blob = await response.blob();
                totalSize += blob.size;
              }
            }
          }
        }
        
        // Envoyer la taille au client
        self.clients.matchAll().then((clients) => {
          clients.forEach((client) => {
            client.postMessage({ 
              type: 'CACHE_SIZE',
              size: totalSize,
              sizeFormatted: formatBytes(totalSize)
            });
          });
        });
      })
    );
  }
});

// Utilitaire: formater les bytes
function formatBytes(bytes) {
  if (bytes === 0) return '0 Bytes';
  const k = 1024;
  const sizes = ['Bytes', 'KB', 'MB'];
  const i = Math.floor(Math.log(bytes) / Math.log(k));
  return Math.round(bytes / Math.pow(k, i) * 100) / 100 + ' ' + sizes[i];
}

console.log('[SW] Service Worker chargé - Version:', CACHE_VERSION);

