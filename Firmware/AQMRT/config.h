#ifndef CONFIG_H
#define CONFIG_H

// I2C Pins
#define SDA 35
#define SCL 36

// SPI Pins
#define SD_CS 18
#define SD_MOSI 17
#define SD_MISO 12
#define SD_SCK 13

// BMS Pins
#define STAT1 11
#define STAT2 10
#define CE 9

// LED Pins
#define LED1 4
#define LED2 5
#define LED3 6
#define LED4 7
#define LEDC_RES 8
#define LEDC_FREQ 20000

// PM2.5 Sensor Pins
#define PMS_RST 21
#define PMS_SET 33
#define PMS_TX 26
#define PMS_RX 47

// CO2 Sensor
#define CO2_EN 34
#define CO2_nRDY 48
#define CO2_ADDR 0x68




struct SensorData {
  struct tm timeinfo;
  bool stat1;
  bool stat2;
  float temperature;
  float rh;
  uint16_t voc_raw;
  uint16_t nox_raw;
  int32_t voc_index;
  int32_t nox_index;
  uint16_t co2_ppm;
  uint16_t pm1_0;
  uint16_t pm2_5;
  uint16_t pm10_0;
};



#endif
