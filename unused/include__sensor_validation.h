#pragma once

// =============================================================================
// MODULE DE VALIDATION DES CAPTEURS
// =============================================================================
// Centralise toute la logique de validation et correction des valeurs capteurs
// =============================================================================

#include <Arduino.h>
#include <cmath>
#include "project_config.h"

namespace SensorValidation {
    
    // =============================================================================
    // VALIDATION TEMPÉRATURE EAU (DS18B20)
    // =============================================================================
    class WaterTempValidator {
    public:
        static bool isValid(float temp) {
            // Rejette NaN, infinité, et valeurs hors plage
            if (std::isnan(temp) || std::isinf(temp)) {
                LOG_DEBUG("Temp eau invalide: NaN/Inf");
                return false;
            }
            
            if (temp <= SensorConfig::WaterTemp::MIN_VALID || 
                temp >= SensorConfig::WaterTemp::MAX_VALID) {
                LOG_DEBUG("Temp eau hors plage: %.2f°C [%.1f-%.1f]", 
                         temp, SensorConfig::WaterTemp::MIN_VALID, 
                         SensorConfig::WaterTemp::MAX_VALID);
                return false;
            }
            
            return true;
        }
        
        static float sanitize(float temp) {
            if (!isValid(temp)) {
                return NAN; // Retourne NaN pour indiquer une erreur
            }
            return temp;
        }
        
        static bool isPlausibleChange(float newTemp, float lastTemp) {
            if (std::isnan(lastTemp)) return true; // Première lecture
            
            float delta = std::abs(newTemp - lastTemp);
            if (delta > SensorConfig::WaterTemp::MAX_DELTA) {
                LOG_DEBUG("Changement temp eau trop rapide: %.2f°C -> %.2f°C (delta=%.2f)", 
                         lastTemp, newTemp, delta);
                return false;
            }
            return true;
        }
    };
    
    // =============================================================================
    // VALIDATION TEMPÉRATURE/HUMIDITÉ AIR (DHT)
    // =============================================================================
    class AirSensorValidator {
    private:
        static bool isDHT11() {
            // Détection automatique basée sur la carte et la configuration
            #if defined(BOARD_WROOM) && !defined(DHT22_SENSOR)
                return true; // ESP32-WROOM utilise DHT11 par défaut
            #else
                return false; // ESP32-S3 ou DHT22 explicite
            #endif
        }
        
    public:
        static bool isValidTemp(float temp) {
            if (std::isnan(temp) || std::isinf(temp)) {
                LOG_DEBUG("Temp air invalide: NaN/Inf");
                return false;
            }
            
            float min = SensorConfig::AirSensor::TEMP_MIN;
            float max = SensorConfig::AirSensor::TEMP_MAX;
            
            if (temp < min || temp > max) {
                LOG_DEBUG("Temp air hors plage: %.2f°C [%.1f-%.1f] (Capteur: %s)", 
                         temp, min, max, isDHT11() ? "DHT11" : "DHT22");
                return false;
            }
            
            return true;
        }
        
        static bool isValidHumidity(float humidity) {
            if (std::isnan(humidity) || std::isinf(humidity)) {
                LOG_DEBUG("Humidité invalide: NaN/Inf");
                return false;
            }
            
            float min = SensorConfig::AirSensor::HUMIDITY_MIN;
            float max = SensorConfig::AirSensor::HUMIDITY_MAX;
            
            if (humidity < min || humidity > max) {
                LOG_DEBUG("Humidité hors plage: %.1f%% [%.1f-%.1f] (Capteur: %s)", 
                         humidity, min, max, isDHT11() ? "DHT11" : "DHT22");
                return false;
            }
            
            return true;
        }
        
        static float sanitizeTemp(float temp) {
            if (!isValidTemp(temp)) {
                LOG_WARN("Temp air invalide, retour NaN");
                return NAN;
            }
            return temp;
        }
        
        static float sanitizeHumidity(float humidity) {
            if (!isValidHumidity(humidity)) {
                LOG_WARN("Humidité invalide, retour NaN");
                return NAN;
            }
            return humidity;
        }
        
        static const char* getSensorType() {
            return isDHT11() ? "DHT11" : "DHT22";
        }
    };
    
    // =============================================================================
    // VALIDATION CAPTEURS ULTRASONIQUES
    // =============================================================================
    class UltrasonicValidator {
    public:
        static bool isValid(uint16_t distance) {
            if (distance < SensorConfig::Ultrasonic::MIN_DISTANCE_CM || 
                distance > SensorConfig::Ultrasonic::MAX_DISTANCE_CM) {
                LOG_DEBUG("Distance invalide: %u cm [%u-%u]", 
                         distance, SensorConfig::Ultrasonic::MIN_DISTANCE_CM,
                         SensorConfig::Ultrasonic::MAX_DISTANCE_CM);
                return false;
            }
            return true;
        }
        
        static uint16_t sanitize(uint16_t distance) {
            if (!isValid(distance)) {
                return 0; // 0 indique une erreur de lecture
            }
            return distance;
        }
        
        static bool isPlausibleChange(uint16_t newDist, uint16_t lastDist) {
            if (lastDist == 0) return true; // Première lecture
            
            uint16_t delta = abs((int)newDist - (int)lastDist);
            if (delta > SensorConfig::Ultrasonic::MAX_DELTA_CM) {
                LOG_DEBUG("Changement distance trop rapide: %u cm -> %u cm (delta=%u)", 
                         lastDist, newDist, delta);
                return false;
            }
            return true;
        }
    };
    
    // =============================================================================
    // VALIDATION CAPTEURS ANALOGIQUES
    // =============================================================================
    class AnalogValidator {
    public:
        static bool isValidADC(uint16_t value) {
            if (value > SensorConfig::Analog::ADC_MAX_VALUE) {
                LOG_DEBUG("Valeur ADC invalide: %u (max=%u)", 
                         value, SensorConfig::Analog::ADC_MAX_VALUE);
                return false;
            }
            return true;
        }
        
        static bool isValidVoltage(uint16_t millivolts) {
            if (millivolts > SensorConfig::Analog::VOLTAGE_MAX_MV) {
                LOG_DEBUG("Tension invalide: %u mV (max=%u)", 
                         millivolts, SensorConfig::Analog::VOLTAGE_MAX_MV);
                return false;
            }
            return true;
        }
        
        static uint16_t sanitizeADC(uint16_t value) {
            if (!isValidADC(value)) {
                return 0;
            }
            return value;
        }
        
        static uint16_t sanitizeVoltage(uint16_t millivolts) {
            if (!isValidVoltage(millivolts)) {
                return 0;
            }
            return millivolts;
        }
    };
    
    // =============================================================================
    // STRUCTURE DE VALIDATION COMPLÈTE
    // =============================================================================
    struct ValidationResult {
        bool isValid;
        String errorMessage;
        float correctedValue;
        
        ValidationResult(bool valid = true, const String& msg = "", float value = 0) 
            : isValid(valid), errorMessage(msg), correctedValue(value) {}
    };
    
    // =============================================================================
    // VALIDATEUR PRINCIPAL
    // =============================================================================
    class MainValidator {
    private:
        // Historique pour détection d'anomalies
        float _lastWaterTemp = NAN;
        float _lastAirTemp = NAN;
        float _lastHumidity = NAN;
        uint16_t _lastAquaLevel = 0;
        uint16_t _lastTankLevel = 0;
        uint32_t _lastValidationTime = 0;
        
    public:
        // Validation complète d'un jeu de mesures
        bool validateAll(float& waterTemp, float& airTemp, float& humidity,
                        uint16_t& aquaLevel, uint16_t& tankLevel, uint16_t& potaLevel,
                        uint16_t& luminosity, uint16_t& voltage) {
            
            bool allValid = true;
            uint32_t now = millis();
            
            // Log du type de capteur utilisé (une fois par minute)
            if (now - _lastValidationTime > 60000) {
                LOG_INFO("Validation capteurs - Type DHT: %s", 
                        AirSensorValidator::getSensorType());
                _lastValidationTime = now;
            }
            
            // Validation température eau
            if (!WaterTempValidator::isValid(waterTemp)) {
                waterTemp = NAN;
                allValid = false;
            } else if (!WaterTempValidator::isPlausibleChange(waterTemp, _lastWaterTemp)) {
                LOG_WARN("Changement temp eau suspect, conservation ancienne valeur");
                if (!std::isnan(_lastWaterTemp)) {
                    waterTemp = _lastWaterTemp;
                }
            } else {
                _lastWaterTemp = waterTemp;
            }
            
            // Validation température air
            if (!AirSensorValidator::isValidTemp(airTemp)) {
                airTemp = NAN;
                allValid = false;
            } else {
                _lastAirTemp = airTemp;
            }
            
            // Validation humidité
            if (!AirSensorValidator::isValidHumidity(humidity)) {
                humidity = NAN;
                allValid = false;
            } else {
                _lastHumidity = humidity;
            }
            
            // Validation niveaux ultrasoniques
            aquaLevel = UltrasonicValidator::sanitize(aquaLevel);
            tankLevel = UltrasonicValidator::sanitize(tankLevel);
            potaLevel = UltrasonicValidator::sanitize(potaLevel);
            
            if (aquaLevel == 0) allValid = false;
            else _lastAquaLevel = aquaLevel;
            
            if (tankLevel == 0) allValid = false;
            else _lastTankLevel = tankLevel;
            
            // Validation capteurs analogiques
            luminosity = AnalogValidator::sanitizeADC(luminosity);
            voltage = AnalogValidator::sanitizeVoltage(voltage);
            
            if (!allValid) {
                LOG_WARN("Validation échouée pour certains capteurs");
            }
            
            return allValid;
        }
        
        // Reset de l'historique (après un redémarrage par exemple)
        void resetHistory() {
            _lastWaterTemp = NAN;
            _lastAirTemp = NAN;
            _lastHumidity = NAN;
            _lastAquaLevel = 0;
            _lastTankLevel = 0;
            _lastValidationTime = 0;
            LOG_INFO("Historique de validation réinitialisé");
        }
        
        // Obtenir un rapport de validation
        String getValidationReport() {
            String report = "=== Rapport de Validation ===\n";
            report += "Type capteur DHT: ";
            report += AirSensorValidator::getSensorType();
            report += "\n";
            
            report += "Dernières valeurs valides:\n";
            report += " - Temp eau: ";
            report += std::isnan(_lastWaterTemp) ? "N/A" : String(_lastWaterTemp, 1) + "°C";
            report += "\n - Temp air: ";
            report += std::isnan(_lastAirTemp) ? "N/A" : String(_lastAirTemp, 1) + "°C";
            report += "\n - Humidité: ";
            report += std::isnan(_lastHumidity) ? "N/A" : String(_lastHumidity, 1) + "%";
            report += "\n - Niveau aqua: ";
            report += _lastAquaLevel == 0 ? "N/A" : String(_lastAquaLevel) + " cm";
            report += "\n - Niveau tank: ";
            report += _lastTankLevel == 0 ? "N/A" : String(_lastTankLevel) + " cm";
            report += "\n";
            
            return report;
        }
    };
}

// Instance globale du validateur principal
extern SensorValidation::MainValidator g_sensorValidator;
