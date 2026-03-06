#pragma once

// Cartographies de broches spécifiques à la carte. Définir BOARD_S3 ou BOARD_WROOM (ou ajouter un nouveau) dans les build_flags de platformio.ini.

#if defined(BOARD_S3) // ESP32-S3
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