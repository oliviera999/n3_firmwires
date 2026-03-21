#pragma once

#include "credentials.h"
#include "n3_defaults.h"

#define FIRMWARE_VERSION "4.25"

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

#define uS_TO_S_FACTOR N3_US_TO_S_FACTOR
#define TIME_TO_SLEEP FreqWakeUp

#define emailSubject "Information N3PP"

#define SMTP_HOST SMTP_HOST_ADDR
#define SMTP_PORT esp_mail_smtp_port_465
#define AUTHOR_EMAIL SMTP_EMAIL
#define AUTHOR_PASSWORD SMTP_PASSWORD

//#define pontdiv 36  // Pin pour la lecture du diviseur de tension
// R1/R2 non definis ici pour eviter conflit avec struct N3BatteryConfig (n3_battery.h) ; n3pp_sensors utilise N3_BATTERY_R1/R2
#define NUM_SAMPLES N3_BATTERY_NUM_SAMPLES

#define SCREEN_WIDTH N3_OLED_WIDTH
#define SCREEN_HEIGHT N3_OLED_HEIGHT
