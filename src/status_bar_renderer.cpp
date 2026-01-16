#include "status_bar_renderer.h"
#include "display_view.h"
#include <Adafruit_SSD1306.h>

// Implémentation minimale de la barre d'état
void StatusBarRenderer::draw(DisplayView& view, Adafruit_SSD1306& display, const StatusBarParams& params) {
    // Dessiner la barre d'état en haut de l'écran (8 pixels de hauteur)
    
    // Effacer la zone de la barre d'état
    display.fillRect(0, 0, 128, 8, BLACK);
    
    // WiFi RSSI indicator (gauche)
    display.setCursor(0, 0);
    display.setTextSize(1);
    display.setTextColor(WHITE);
    
    if (params.rssi >= -50) {
        display.print(F("WiFi +++"));
    } else if (params.rssi >= -70) {
        display.print(F("WiFi ++ "));
    } else if (params.rssi >= -85) {
        display.print(F("WiFi +  "));
    } else if (params.rssi > -100) {
        display.print(F("WiFi -  "));
    } else {
        display.print(F("WiFi X  "));
    }
    
    // Indicateur send/recv (milieu)
    display.setCursor(60, 0);
    if (params.sendState == 1) {
        display.print(F("S"));
    } else if (params.sendState == -1) {
        display.print(F("!"));
    } else {
        display.print(F(" "));
    }
    
    if (params.recvState == 1) {
        display.print(F("R"));
    } else if (params.recvState == -1) {
        display.print(F("!"));
    } else {
        display.print(F(" "));
    }
    
    // Indicateur marée (droite)
    display.setCursor(80, 0);
    if (params.tideDir > 0) {
        display.print(F("^")); // Marée montante
    } else if (params.tideDir < 0) {
        display.print(F("v")); // Marée descendante
    } else {
        display.print(F("-")); // Stable
    }
    
    // Mail blink indicator
    if (params.mailBlink) {
        display.setCursor(90, 0);
        display.print(F("@"));
    }
    
    // OTA progress overlay (si actif)
    if (params.otaOverlayActive) {
        display.setCursor(100, 0);
        display.printf("%d%%", params.otaPercent);
    }
}
