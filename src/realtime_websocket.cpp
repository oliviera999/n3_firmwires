#include "realtime_websocket.h"
#include "automatism.h"

// Instance globale du serveur WebSocket temps réel
RealtimeWebSocket g_realtimeWebSocket;

// Implémentation de notifyClientActivity
void RealtimeWebSocket::notifyClientActivity() {
    lastClientActivity = millis();
    hasActiveClients = true;
    
    Serial.printf("[WebSocket] 👤 Client activity detected - %u clients connected\n", 
                  g_realtimeWebSocket.getConnectedClients());
    
    // Notifier aussi le système d'automatisme pour maintenir l'éveil
    extern Automatism g_autoCtrl;
    g_autoCtrl.notifyLocalWebActivity();
}
