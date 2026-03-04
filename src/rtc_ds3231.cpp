/**
 * @file rtc_ds3231.cpp
 * @brief Implémentation du driver minimal DS3231 (I2C).
 *
 * Registres 0x00-0x06 en BCD. Heure stockée en local dans le RTC.
 */

#include "rtc_ds3231.h"
#include "i2c_bus.h"
#include "pins.h"
#include "config.h"
#include <Wire.h>
#include <cstring>

#if defined(USE_RTC_DS3231)

namespace {

constexpr uint8_t REG_SEC = 0x00;
constexpr uint8_t REG_MIN = 0x01;
constexpr uint8_t REG_HOUR = 0x02;
constexpr uint8_t REG_DOW = 0x03;
constexpr uint8_t REG_DATE = 0x04;
constexpr uint8_t REG_MONTH = 0x05;
constexpr uint8_t REG_YEAR = 0x06;

constexpr uint8_t MONTH_CENTURY_BIT = 0x80;

inline uint8_t bcdToByte(uint8_t b) {
  return (b & 0x0F) + ((b >> 4) * 10);
}

inline uint8_t byteToBcd(uint8_t v) {
  if (v > 99) return 0;
  return ((v / 10) << 4) | (v % 10);
}

}  // namespace

namespace RtcDS3231 {

bool isPresent() {
  I2CBusGuard guard;
  if (!guard) return false;
  Wire.beginTransmission(Pins::DS3231_I2C_ADDR);
  return (Wire.endTransmission() == 0);
}

bool read(time_t* outEpoch) {
  if (outEpoch == nullptr) return false;
  I2CBusGuard guard;
  if (!guard) return false;

  Wire.beginTransmission(Pins::DS3231_I2C_ADDR);
  Wire.write(REG_SEC);
  if (Wire.endTransmission(false) != 0) {
    Serial.println(F("[RTC] DS3231 diagnostic: I2C endTransmission(false) a echoue"));
    return false;
  }
  if (Wire.requestFrom(static_cast<uint8_t>(Pins::DS3231_I2C_ADDR), 7u) != 7) {
    Serial.println(F("[RTC] DS3231 diagnostic: I2C requestFrom(7) n a pas retourne 7 octets"));
    return false;
  }

  uint8_t sec = bcdToByte(Wire.read() & 0x7F);
  uint8_t min = bcdToByte(Wire.read());
  uint8_t hour = bcdToByte(Wire.read() & 0x3F);
  (void)Wire.read();  // day of week
  uint8_t date = bcdToByte(Wire.read());
  uint8_t monthReg = Wire.read();
  uint8_t month = bcdToByte(monthReg & 0x1F);
  uint8_t year2 = bcdToByte(Wire.read());
  int year = (monthReg & MONTH_CENTURY_BIT) ? (1900 + year2) : (2000 + year2);

  if (sec > 59 || min > 59 || hour > 23 || date < 1 || date > 31 || month < 1 || month > 12) {
    Serial.printf("[RTC] DS3231 diagnostic: plage BCD invalide -> %04d-%02d-%02d %02d:%02d:%02d (sec=%u min=%u hour=%u date=%u month=%u year=%d)\n",
                  year, month, date, hour, min, sec, sec, min, hour, date, month, year);
    return false;
  }

  struct tm t;
  memset(&t, 0, sizeof(t));
  t.tm_sec = sec;
  t.tm_min = min;
  t.tm_hour = hour;
  t.tm_mday = date;
  t.tm_mon = month - 1;
  t.tm_year = year - 1900;
  t.tm_isdst = -1;

  time_t epoch = mktime(&t);
  if (epoch == (time_t)-1) {
    Serial.printf("[RTC] DS3231 diagnostic: mktime a echoue pour %04d-%02d-%02d %02d:%02d:%02d\n",
                  year, month, date, hour, min, sec);
    return false;
  }

  if (epoch < SleepConfig::EPOCH_MIN_VALID || epoch > SleepConfig::EPOCH_MAX_VALID) {
    Serial.printf("[RTC] DS3231 diagnostic: epoch %lu hors plage [%lu..%lu] (date lue: %04d-%02d-%02d %02d:%02d:%02d)\n",
                  (unsigned long)epoch, (unsigned long)SleepConfig::EPOCH_MIN_VALID,
                  (unsigned long)SleepConfig::EPOCH_MAX_VALID, year, month, date, hour, min, sec);
    return false;
  }

  *outEpoch = epoch;
  return true;
}

bool write(time_t epoch) {
  struct tm t;
  if (!localtime_r(&epoch, &t)) return false;

  I2CBusGuard guard;
  if (!guard) return false;

  uint8_t year = t.tm_year + 1900;
  uint8_t year2 = (year >= 2000) ? (year - 2000) : (year - 1900);
  uint8_t monthReg = byteToBcd(t.tm_mon + 1);
  if (year >= 2000) monthReg &= 0x7F;
  else monthReg |= MONTH_CENTURY_BIT;

  Wire.beginTransmission(Pins::DS3231_I2C_ADDR);
  Wire.write(REG_SEC);
  Wire.write(byteToBcd(static_cast<uint8_t>(t.tm_sec)));
  Wire.write(byteToBcd(static_cast<uint8_t>(t.tm_min)));
  Wire.write(byteToBcd(static_cast<uint8_t>(t.tm_hour)));
  uint8_t dow = (t.tm_wday == 0) ? 7 : static_cast<uint8_t>(t.tm_wday);  // DS3231 1-7 (Sun=1)
  Wire.write(byteToBcd(dow));
  Wire.write(byteToBcd(static_cast<uint8_t>(t.tm_mday)));
  Wire.write(monthReg);
  Wire.write(byteToBcd(year2));
  return (Wire.endTransmission() == 0);
}

}  // namespace RtcDS3231

#endif  // USE_RTC_DS3231
