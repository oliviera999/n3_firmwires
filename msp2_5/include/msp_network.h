/* MeteoStationPrototype (msp1) — Réseau
 * httpGETRequest, datatobdd, variablestoesp, Wificonnect
 */
#pragma once

#include <Arduino.h>

String httpGETRequest(const char* serverNameOutput);
void datatobdd();
void variablestoesp();
void Wificonnect();
