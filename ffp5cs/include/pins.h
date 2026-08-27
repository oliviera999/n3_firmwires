#pragma once

// Cartographies de broches spécifiques à la carte. Définir BOARD_S3 ou BOARD_WROOM (ou ajouter un nouveau) dans les build_flags de platformio.ini.

#if defined(BOARD_S3) && defined(PINMAP_S3_CARRIER) // ESP32-S3 sur carte porteuse bi-module ffp5cs-wroom-prod rev >= 0.6
// Cartographie "carrier" : ESP32-S3-DevKitC-1 enfiché sur le site A2 de la carte
// porteuse 12/24V (option A bi-module). Ne PAS confondre avec la cartographie S3
// historique ci-dessous (système S3 câblé main, EN PRODUCTION) : les quatre
// différences corrigent des pièges du brochage historique —
//   - ONE_WIRE_BUS 37 -> 41 : GPIO 33-37 réquisitionnés par la PSRAM octale (R8) ;
//   - ULTRASON_POTA 45 -> 5 et LUMINOSITE 3 -> 1 : 45/3 sont des broches de
//     strapping (VDD_SPI / sélection JTAG), 1 = ADC1_CH0 pour la LDR ;
//   - AUX2 48 -> 42 : la LED RGB du DevKitC-1 est sur 48 (v1.0) ou 38 (v1.1).
// Sélection : -DPINMAP_S3_CARRIER (envs wroom-s3-carrier-*). Garde anti-dérive :
// hardware/ffp5cs-wroom-prod/tools/check_pinmap_vs_firmware.py (site A2).
namespace Pins {
// Capteurs ultrasoniques
constexpr int ULTRASON_AQUA = 4;
constexpr int ULTRASON_TANK = 6;
constexpr int ULTRASON_POTA = 5;

// Entrées analogiques / numériques
constexpr int EAU_POTAGER = ULTRASON_POTA; // Alias pour rétrocompatibilité
constexpr int LUMINOSITE = 1;  // ADC1_CH0

// Actionneurs / Relais
constexpr int POMPE_AQUA   = 16;
constexpr int POMPE_RESERV = 18;
constexpr int RADIATEURS   = 13;
constexpr int LUMIERE      = 15;

// Relais auxiliaires — breakout J20 de la carte porteuse (modules relais externes)
constexpr int AUX1 = 47;
constexpr int AUX2 = 42;

// Servomoteurs (distributeurs)
constexpr int SERVO_GROS   = 21;
constexpr int SERVO_PETITS = 17;

// Capteurs
constexpr int DHT_PIN      = 7;
constexpr int ONE_WIRE_BUS = 41;  // Bus DS18B20 (37 = conflit PSRAM octale)

// I2C (Wire)
constexpr int I2C_SDA = 8;
constexpr int I2C_SCL = 9;

// OLED Display (I2C)
constexpr int OLED_SDA = I2C_SDA;
constexpr int OLED_SCL = I2C_SCL;
constexpr int OLED_ADDR = 0x3C;

// RTC DS3231 (I2C) - optionnel, pour heure précise offline
constexpr uint8_t DS3231_I2C_ADDR = 0x68;

// SD carte (SPI) - optionnel ; broches NON câblées sur la carte porteuse
// (conservées pour compiler le support SD, à câbler via J20 le cas échéant)
constexpr int SD_CS_PIN   = 10;
constexpr int SD_MOSI_PIN = 11;
constexpr int SD_CLK_PIN  = 12;
constexpr int SD_MISO_PIN = 14;

} // namespace Pins

#elif defined(BOARD_S3) // ESP32-S3 (cartographie historique, câblage main — production)
namespace Pins {
// Capteurs ultrasoniques
constexpr int ULTRASON_AQUA = 4;
constexpr int ULTRASON_TANK = 6;
constexpr int ULTRASON_POTA = 45;

// Entrées analogiques / numériques
constexpr int EAU_POTAGER = ULTRASON_POTA; // Alias pour rétrocompatibilité
constexpr int LUMINOSITE = 3;

// Actionneurs / Relais
constexpr int POMPE_AQUA   = 16;
constexpr int POMPE_RESERV = 18;
constexpr int RADIATEURS   = 13; // évite conflit avec ULTRASON_TANK
constexpr int LUMIERE      = 15;

// Relais auxiliaires (v15.26) — carte porteuse 230V 6 canaux (47/48 libres, non-strapping)
constexpr int AUX1 = 47;
constexpr int AUX2 = 48;

// Servomoteurs (distributeurs). 14/12/11/10 indispos → SERVO_GROS sur 21 (libre, PWM OK).
constexpr int SERVO_GROS   = 21;
constexpr int SERVO_PETITS = 17;

// Capteurs
constexpr int DHT_PIN      = 7;
constexpr int ONE_WIRE_BUS = 37;  // Bus DS18B20

// I2C (Wire) - Pins par défaut pour ESP32-S3 devkit. Vérifier câblage (VCC/GND/SDA/SCL) et pull-ups si OLED erratique (voir i2c_bus.cpp).
constexpr int I2C_SDA = 8;  // GPIO 8 (SDA)
constexpr int I2C_SCL = 9;  // GPIO 9 (SCL)

// OLED Display (I2C)
constexpr int OLED_SDA = I2C_SDA;
constexpr int OLED_SCL = I2C_SCL;
constexpr int OLED_ADDR = 0x3C;  // Adresse I2C de l'OLED

// RTC DS3231 (I2C) - optionnel, pour heure précise offline
constexpr uint8_t DS3231_I2C_ADDR = 0x68;

// SD carte (SPI) - optionnel, broches 10/11/12/14 (libres, non utilisées par les servos)
constexpr int SD_CS_PIN   = 10;
constexpr int SD_MOSI_PIN = 11;
constexpr int SD_CLK_PIN  = 12;
constexpr int SD_MISO_PIN = 14;

} // namespace Pins

#else // ESP32-WROOM
// --- Cartographie ESP32 par défaut/hérité (exemple, ajuster selon les besoins) ---
namespace Pins {
constexpr int ULTRASON_AQUA = 4;
constexpr int ULTRASON_TANK = 19;
constexpr int ULTRASON_POTA = 33;

constexpr int EAU_POTAGER = ULTRASON_POTA;
constexpr int LUMINOSITE = 34; // ADC0
constexpr int POMPE_AQUA   = 16;
constexpr int POMPE_RESERV = 18;
constexpr int RADIATEURS   = 2;
constexpr int LUMIERE      = 15;
// Relais auxiliaires (v15.26) — carte porteuse 230V 6 canaux (23/25 libres, non-strapping)
constexpr int AUX1 = 23;
constexpr int AUX2 = 25;
constexpr int SERVO_GROS   = 12;
constexpr int SERVO_PETITS = 13;
constexpr int DHT_PIN      = 27;
constexpr int ONE_WIRE_BUS = 26;

// I2C (Wire) - Pins par défaut pour ESP32. Vérifier câblage (VCC/GND/SDA/SCL) et pull-ups si OLED erratique (voir i2c_bus.cpp).
constexpr int I2C_SDA = 21;  // GPIO 21 (SDA)
constexpr int I2C_SCL = 22;  // GPIO 22 (SCL)

// OLED Display (I2C)
constexpr int OLED_SDA = I2C_SDA;
constexpr int OLED_SCL = I2C_SCL;
constexpr int OLED_ADDR = 0x3C;  // Adresse I2C de l'OLED

// RTC DS3231 (I2C) - optionnel, pour heure précise offline
constexpr uint8_t DS3231_I2C_ADDR = 0x68;

} // namespace Pins

#endif 