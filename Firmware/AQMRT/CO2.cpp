#include "CO2.h"
#include "config.h"
#include <Arduino.h>
#include <Wire.h>


void CO2::init(){
  turnOn();
  delay(35);
  configCO2();
  readCO2Config();
  turnOff();
}

void CO2::turnOn() {
  digitalWrite(CO2_EN, HIGH);  // Turn on CO2 Sensor
}

void CO2::turnOff() {
  digitalWrite(CO2_EN, LOW);  // Turn on CO2 Sensor
}

void CO2::configCO2() {
  int error;
  uint16_t SINGLE = 0x0001;

  // Set ABC true
  Serial.println("Enabling Automatic Background Calibration (ABC)");
  uint8_t meterControl = 0x00;
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0xA5);  //METER_CONTROL
  Wire.write(meterControl);
  error = Wire.endTransmission(true);
  delay(50);

  // Set Mode to Single
  Serial.println("Setting Measurement Mode to Single");
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0x95);  // MEASUREMENT_MODE
  Wire.write(SINGLE);
  error = Wire.endTransmission(true);
  delay(50);

  // Restart Sensor
  Serial.println("Restarting sensor to apply changes");
  turnOff();
  delay(50);
  turnOn();
  delay(50);
}

void CO2::readCO2Config() {
  int error;

  // Read Config
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0x95);  // MEASUREMENT_MODE
  Wire.endTransmission(false);
  error = Wire.requestFrom(CO2_ADDR, 7);
  if (error != 7) {
    Serial.print("Failed to write to CO2_ADDR. Error code : ");
    Serial.println(error);
    return;
  }

  // Read Payload
  uint8_t measMode = Wire.read();  // Measurement Mode
  uint8_t byteHi = Wire.read();    // Measurement Period MSB
  uint8_t byteLo = Wire.read();    // Measurement Period LSB
  uint16_t measPeriod = ((int16_t)(int8_t)byteHi << 8) | (uint16_t)byteLo;
  byteHi = Wire.read();  // Number of Samples MSB
  byteLo = Wire.read();  // Number of Samples LSB
  uint16_t numSamples = ((int16_t)(int8_t)byteHi << 8) | (uint16_t)byteLo;
  byteHi = Wire.read();  // ABC period MSB
  byteLo = Wire.read();  // ABC period LSB
  uint16_t abcPeriod = ((int16_t)(int8_t)byteHi << 8) | (uint16_t)byteLo;

  // Request Meter Control Byte
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0xA5);  //METER_CONTROL
  Wire.endTransmission(false);
  error = Wire.requestFrom(CO2_ADDR, 1);
  if (error != 1) {
    Serial.print("Failed to write to CO2_ADDR. Error code : ");
    Serial.println(error);
    return;
  }
  uint8_t meterControl = Wire.read();

  Serial.print("Measurement Mode: ");
  Serial.println(measMode);
  Serial.print("Measurement Period, sec: ");
  Serial.println(measPeriod);
  Serial.print("Number of Samples: ");
  Serial.println(numSamples);
  Serial.print("MeterControl: ");
  Serial.println(meterControl, HEX);


  // Read CO2_State Data
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0xC4);  // ABC_TIME
  Wire.endTransmission(false);
  error = Wire.requestFrom(CO2_ADDR, 24);
  if (error != 24) {
    Serial.print("Failed to read measurements command. Error code: ");
    Serial.println(error);
    turnOff();
    return;
  }
  for (int n = 0; n < 24; n++) {
    CO2_State[n] = Wire.read();
  }

  // Turn off Sensor
  turnOff();
  Serial.println("Saved Sensor State Successfully\n");
}

uint16_t CO2::readCO2() {
  int error;
  int numRegCmd = 25;
  int numRegRead = 7;
  int numRegState = 24;
  uint8_t cmdArray[25];
  cmdArray[0] = 0x01;

  // Fill remaining 24 bytes with state data
  for (int n = 1; n < numRegCmd; n++) {
    cmdArray[n] = CO2_State[n - 1];
  }

  // Turn on Sensor
  turnOn();
  delay(50);

  // Start Single Measurement and write state data
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0xC3);  // START_MEASUREMENT
  for (int reg_n = 0; reg_n < numRegCmd; reg_n++) {
    Wire.write(cmdArray[reg_n]);
  }
  error = Wire.endTransmission(true);
  if (error != 0) {
    Serial.print("Failed to send measurement command. Error code: ");
    Serial.println(error);
    turnOff();
    // return;
  }
  delay(2000);

  // Request Payload
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0x01);  // ERROR STATUS
  Wire.endTransmission(false);
  error = Wire.requestFrom(CO2_ADDR, numRegRead);
  if (error != numRegRead) {
    Serial.print("Failed to read values. Error code: ");
    Serial.println(error);
    turnOff();
    // return;
  }

  // Read Payload
  uint8_t eStatus = Wire.read();  // Error Status
  uint8_t byteHi = Wire.read();   // Reserved
  uint8_t byteLo = Wire.read();   // Reserved
  byteHi = Wire.read();           // Reserved
  byteLo = Wire.read();           // Reserved
  byteHi = Wire.read();           // Filtered CO2 Value MSB
  byteLo = Wire.read();           // Filtered CO2 Value LSB
  uint16_t co2Val = ((int16_t)(int8_t)byteHi << 8) | (uint16_t)byteLo;
  // data.co2_ppm = co2Val;
  // Read sensor state data from 0xC4-0xDB and save it for next measurement
  Wire.beginTransmission(CO2_ADDR);
  Wire.write(0xC4);  // ABC_TIME
  Wire.endTransmission(false);
  error = Wire.requestFrom(CO2_ADDR, numRegState);
  if (error != numRegState) {
    Serial.print("Failed to read measurements command. Error code: ");
    Serial.println(error);
    turnOff();
    // return;
  }
  for (int n = 0; n < numRegState; n++) {
    CO2_State[n] = Wire.read();
  }

  // Turn off Sensor
  turnOff();

  return co2Val;
}


void CO2::updateABC() {
  abc_time = ((int16_t)(int8_t)CO2_State[0] << 8) | (uint16_t)CO2_State[1];
  abc_time = abc_time + 1;
  CO2_State[0] = abc_time >> 8;
  CO2_State[1] = abc_time & 0x00FF;
}