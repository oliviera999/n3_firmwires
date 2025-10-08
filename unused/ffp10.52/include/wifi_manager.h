#pragma once
#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <Arduino.h>
#include <WiFi.h>
#include <functional>
class DisplayView;

class WifiManager {
 public:
  struct Credential {
    const char* ssid;
    const char* password;
  };

  WifiManager(const Credential* list, size_t count, uint32_t timeoutMs = 8000, uint32_t retryIntervalMs = 60000);

  // Tente de se connecter ; retourne true si connecté
  bool connect(class DisplayView* disp = nullptr);

  // Renvoie le SSID auquel on est connecté ou "".
  String currentSSID() const;

  bool isConnected() const { return WiFi.status() == WL_CONNECTED; }

  // Démarre un point d'accès lorsqu'aucun réseau n'est disponible
  bool startFallbackAP();

  // À appeler régulièrement (ex. dans loop) pour tenter périodiquement une
  // reconnexion lorsqu'on est en mode AP ou déconnecté. L'intervalle est
  // fixé dans le constructeur (60 s par défaut).
  void loop(class DisplayView* disp = nullptr);
  
  // Nouvelles méthodes pour gestion intelligente du signal
  int8_t getCurrentRSSI() const;
  String getSignalQuality() const;
  bool isSignalStable();
  bool shouldReconnect();
  void checkConnectionStability();
  
  // Méthodes pour contrôle manuel WiFi
  bool disconnect();
  bool reconnect(class DisplayView* disp = nullptr);
  String getConnectionStatus() const;

 private:
  const Credential* _list;
  size_t _count;
  uint32_t _timeoutMs;

  // Intervalle minimum entre deux tentatives automatiques (ms)
  uint32_t _retryIntervalMs = 60000;
  uint32_t _lastAttemptMs = 0;

  // Gestion backoff et état
  uint32_t _baseRetryMs = 60000;
  bool _connecting = false;
  
  // Variables pour gestion intelligente du signal
  int8_t _lastRSSI = 0;
  uint32_t _lastRSSICheck = 0;
  uint32_t _weakSignalStartTime = 0;
  uint32_t _reconnectAttempts = 0;
  uint32_t _lastReconnectAttempt = 0;
};

#endif // WIFI_MANAGER_H 