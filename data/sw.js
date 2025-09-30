// Service Worker pour FFP3 Dashboard
const CACHE_NAME = 'ffp3-dashboard-v1.0';
const urlsToCache = [
  '/',
  '/index.html',
  '/chart.js',
  '/bootstrap.min.css',
  '/manifest.json'
];

// Installation du Service Worker
self.addEventListener('install', (event) => {
  console.log('[SW] Installation du Service Worker');
  event.waitUntil(
    caches.open(CACHE_NAME)
      .then((cache) => {
        console.log('[SW] Mise en cache des fichiers');
        return cache.addAll(urlsToCache);
      })
      .catch((error) => {
        console.error('[SW] Erreur lors de la mise en cache:', error);
      })
  );
});

// Activation du Service Worker
self.addEventListener('activate', (event) => {
  console.log('[SW] Activation du Service Worker');
  event.waitUntil(
    caches.keys().then((cacheNames) => {
      return Promise.all(
        cacheNames.map((cacheName) => {
          if (cacheName !== CACHE_NAME) {
            console.log('[SW] Suppression de l\'ancien cache:', cacheName);
            return caches.delete(cacheName);
          }
        })
      );
    })
  );
});

// Interception des requêtes
self.addEventListener('fetch', (event) => {
  // Stratégie Cache First pour les assets statiques
  if (urlsToCache.includes(event.request.url) || 
      event.request.url.includes('/chart.js') ||
      event.request.url.includes('/bootstrap.min.css') ||
      event.request.url.includes('/manifest.json')) {
    
    event.respondWith(
      caches.match(event.request)
        .then((response) => {
          if (response) {
            console.log('[SW] Ressource servie depuis le cache:', event.request.url);
            return response;
          }
          
          // Si pas en cache, récupérer depuis le réseau
          return fetch(event.request)
            .then((response) => {
              // Vérifier que la réponse est valide
              if (!response || response.status !== 200 || response.type !== 'basic') {
                return response;
              }
              
              // Cloner la réponse pour la mettre en cache
              const responseToCache = response.clone();
              caches.open(CACHE_NAME)
                .then((cache) => {
                  cache.put(event.request, responseToCache);
                });
              
              return response;
            });
        })
    );
  }
  
  // Stratégie Network First pour les données dynamiques
  else if (event.request.url.includes('/json') || 
           event.request.url.includes('/dbvars') ||
           event.request.url.includes('/action')) {
    
    event.respondWith(
      fetch(event.request)
        .then((response) => {
          // Si la requête réseau réussit, retourner la réponse
          if (response && response.status === 200) {
            return response;
          }
          
          // Sinon, essayer le cache
          return caches.match(event.request);
        })
        .catch(() => {
          // En cas d'erreur réseau, essayer le cache
          return caches.match(event.request);
        })
    );
  }
  
  // Pour toutes les autres requêtes, utiliser la stratégie par défaut
  else {
    event.respondWith(
      fetch(event.request)
        .catch(() => {
          // En cas d'erreur, essayer le cache
          return caches.match(event.request);
        })
    );
  }
});

// Gestion des messages du client
self.addEventListener('message', (event) => {
  if (event.data && event.data.type === 'SKIP_WAITING') {
    self.skipWaiting();
  }
});

// Notification de mise à jour disponible
self.addEventListener('message', (event) => {
  if (event.data && event.data.type === 'GET_VERSION') {
    event.ports[0].postMessage({
      version: CACHE_NAME
    });
  }
});
