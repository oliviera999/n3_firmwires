#pragma once
/**
 * Banc energie — mise en securite de la carte et bus I2C.
 *
 * Compatible DevKitC-1 nu ET carte n3-universal :
 *  - relais K1..K6 forces OFF (drivers actifs HAUT) ;
 *  - rail +3V3_SW alimente (GATE = GPIO3) avant d'ouvrir le bus I2C ;
 *  - Wire ouvert sur SDA 8 / SCL 9 AVANT n3DisplayInit (qui rappelle
 *    Wire.begin() sans broches : sans effet si le bus est deja ouvert).
 */

#include <Arduino.h>

void boardBegin();
/** Scan I2C 0x08..0x77 avec le nom probable de chaque adresse. */
void boardI2cScan();
