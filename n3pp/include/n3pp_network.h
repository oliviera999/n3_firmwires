#pragma once

#include <Arduino.h>

void datatobdd();
void variablestoesp();
void Wificonnect();
void n3ppAccumulateNetReportElapsedFromSleep(int sleepSeconds);
void n3ppMaybeSendNetworkReportEmail();
unsigned int n3ppGetOutputsGetFailureCount();
