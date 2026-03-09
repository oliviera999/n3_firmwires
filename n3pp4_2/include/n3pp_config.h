#pragma once

#include "credentials.h"

#define FIRMWARE_VERSION "4.10"

//définitions des pins pour les actionneurs
#define RELAIS 13
#define POMPE 12

//définitions des pins pour les capteurs
#define pontdiv 36

#define humidite1 33
#define humidite2 32
#define humidite3 35
#define humidite4 34

//définitions des pins pour les capteurs
#define LUMINOSITE 39

//configuration DHT
#define DHTPIN 18      // Pin numérique connectée au DHT (température et humidité air)
#define DHTTYPE DHT11  // Type de capteur DHT (DHT11)

#define uS_TO_S_FACTOR 1000000ULL /* Facteur de conversion microsecondes → secondes */
#define TIME_TO_SLEEP FreqWakeUp  /* Durée du sommeil en secondes avant réveil */

#define emailSubject "Information N3PP"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

//#define pontdiv 36  // Pin pour la lecture du diviseur de tension
#define R1 2200           // valeur de la résistances R1 en ohm du pont diviseur
#define R2 2180           // valeur de la résistances R2 en ohm du pont diviseur
#define NUM_SAMPLES 10    // Nombre d'échantillons à utiliser pour le filtrage

// Résolution de l'écran OLED
#define SCREEN_WIDTH 128   // Largeur de l'écran OLED en pixels
#define SCREEN_HEIGHT 64   // Hauteur de l'écran OLED en pixels
