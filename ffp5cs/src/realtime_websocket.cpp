#include "realtime_websocket.h"

#ifndef DISABLE_ASYNC_WEBSERVER
#include "automatism.h"

// Instance globale du serveur WebSocket temps réel
RealtimeWebSocket g_realtimeWebSocket;

// Implémentation de notifyClientActivity (thread-safe via std::atomic)
void RealtimeWebSocket::notifyClientActivity() {
    lastClientActivity.store(millis());
    _hasActiveClients.store(true);
    
    Serial.printf("[WebSocket] 👤 Client activity detected - %u clients connected\n", 
                  g_realtimeWebSocket.getConnectedClients());
    
    // Notifier aussi le système d'automatisme pour maintenir l'éveil
    extern Automatism g_autoCtrl;
    g_autoCtrl.notifyLocalWebActivity();
}
#else
// Stub : instance vide quand serveur web désactivé
RealtimeWebSocket g_realtimeWebSocket;
#endif