#ifndef CO2_H
#define CO2_H
#include "Arduino.h"
#include "config.h"


class CO2 {
public:
  void init();
  void turnOn();
  void turnOff();
  void configCO2();
  void readCO2Config();
  uint16_t readCO2();
  void updateABC();

private:
  uint8_t CO2_State[24];
  uint16_t abc_time;
};

#endif