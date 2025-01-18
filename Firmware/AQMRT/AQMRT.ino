#include <esp_pm.h>
#include <esp_sleep.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <SPI.h>
#include <SD.h>
#include <Wire.h>
#include "time.h"
#include <SensirionI2cSht4x.h>
#include <SensirionI2CSgp41.h>
#include <VOCGasIndexAlgorithm.h>
#include <NOxGasIndexAlgorithm.h>
#include "PMS.h"
#include "config.h"
#include "CO2.h"

// Define task handles
TaskHandle_t BQ25185_THandle = NULL;
TaskHandle_t vocNoxRHT_THandle = NULL;
TaskHandle_t co2_THandle = NULL;
TaskHandle_t CO2_ABC_THandle = NULL;
TaskHandle_t pms_THandle = NULL;
TaskHandle_t uSD_THandle = NULL;
TaskHandle_t display_THandle = NULL;


SensorData liveData;
SensirionI2cSht4x sht4x;
SensirionI2CSgp41 sgp41;
VOCGasIndexAlgorithm voc_algorithm;
NOxGasIndexAlgorithm nox_algorithm;
CO2 CO2Sensor;
PMS pms(Serial1);
PMS::DATA pmsData;

// Define mutex for data access
SemaphoreHandle_t dataMutex;

const char *ntpServer = "pool.ntp.org";
const long gmtOffset_sec = -18000;  //EST Offset
const int daylightOffset_sec = 3600;


// Battery Management Task
void BQ25185_Task(void *parameter) {
  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      liveData.stat1 = digitalRead(STAT1);
      liveData.stat2 = digitalRead(STAT2);

      Serial.printf("Stat 1:  %6d\nStat 2:  %6d\n\n", liveData.stat1, liveData.stat2);
      // STAT1 | STAT2 | Meaning
      //   1   |   1   | Charge complete/Charge disabled
      //   1   |   0   | Charge in progress
      //   0   |   1   | Recoverable fault
      //   0   |   0   | Non-recoverable fault
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(300000));
  }
}

// VOC and NOx task(runs every 1 second)
void VOCNOX_Task(void *parameter) {
  uint16_t error;
  char errorMessage[256];
  uint16_t compensationRh = 0;              // in ticks as defined by SGP41
  uint16_t compensationT = 0;               // in ticks as defined by SGP41
  uint16_t defaultCompenstaionRh = 0x8000;  // in ticks as defined by SGP41
  uint16_t defaultCompenstaionT = 0x6666;   // in ticks as defined by SGP41

  //precondition SGP41 for 10 seconds. This should only run once
  for (int i = 0; i < 10; i++) {
    error = sgp41.executeConditioning(defaultCompenstaionRh, defaultCompenstaionT, liveData.voc_raw);
    vTaskDelay(pdMS_TO_TICKS(1000));  // 1 second x10
  }

  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

      error = sht4x.measureHighPrecision(liveData.temperature, liveData.rh);
      if (error) {
        Serial.println("SHT4x - Error");
        // errorToString(error, errorMessage, 256);
        // Serial.println(errorMessage);
        compensationRh = defaultCompenstaionRh;
        compensationT = defaultCompenstaionT;
      } else {
        Serial.printf("Temp:    %6.2f °C\nRH:      %6.2f %%\n\n", liveData.temperature, liveData.rh);
        compensationT = static_cast<uint16_t>((liveData.temperature + 45) * 65535 / 175);
        compensationRh = static_cast<uint16_t>(liveData.rh * 65535 / 100);
      }

      error = sgp41.measureRawSignals(compensationRh, compensationT, liveData.voc_raw, liveData.nox_raw);
      if (error) {
        Serial.print("SGP41 - Error\n");
        // errorToString(error, errorMessage, 256);
        // Serial.println(errorMessage);
      } else {
        liveData.voc_index = voc_algorithm.process(liveData.voc_raw);
        liveData.nox_index = nox_algorithm.process(liveData.nox_raw);
        Serial.printf("VOC raw: %6d\nNOx raw: %6d\n", liveData.voc_raw, liveData.nox_raw);
        Serial.printf("VOC ndx: %6d\nNOx ndx: %6d\n\n", liveData.voc_index, liveData.nox_index);
      }

      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(1000));
  }
}

// CO2 Read Task
void CO2_Task(void *parameter) {
  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      liveData.co2_ppm = CO2Sensor.readCO2();
      Serial.printf("CO2:     %6d ppm\n\n", liveData.co2_ppm);
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(300000));
  }
}

//  CO2 ABC Task to update ABC values every hour
void CO2_ABC_Task(void *parameter) {
  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      CO2Sensor.updateABC();
      Serial.println("ABC time incremented.");
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(3600000));
  }
}

// Particulate Matter task
void PMS_Task(void *parameter) {
  while (1) {
    // Turn on sensor and wait 30 seconds for valid data
    digitalWrite(PMS_SET, HIGH);
    vTaskDelay(pdMS_TO_TICKS(30000));

    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {
      pms.requestRead();
      if (pms.readUntil(pmsData)) {
        liveData.pm1_0 = pmsData.PM_AE_UG_1_0;
        liveData.pm2_5 = pmsData.PM_AE_UG_2_5;
        liveData.pm10_0 = pmsData.PM_AE_UG_10_0;

        Serial.printf("PM1.0:   %6d ug/m3\nPM2.5:   %6d ug/m3\nPM10:    %6d ug/m3\n\n", liveData.pm1_0, liveData.pm2_5, liveData.pm10_0);
      } else {
        Serial.println("No data.");
      }
      digitalWrite(PMS_SET, LOW);

      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(300000));
  }
}

// uSD Card Task
void uSD_Task(void *parameter) {
  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

      struct tm timeinfo;
      if (!getLocalTime(&timeinfo)) {
        Serial.println("Failed to obtain time");
        return;
      }
      char dateTime[20];
      strftime(dateTime, sizeof(dateTime), "%Y-%m-%d %H:%M:%S", &timeinfo);

      File dataFile = SD.open("/sensor_data.csv", FILE_APPEND);
      if (dataFile) {
        Serial.printf("%s,%d,%d,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d\n", dateTime, liveData.stat1, liveData.stat2, liveData.temperature, liveData.rh, liveData.voc_raw, liveData.nox_raw, liveData.voc_index, liveData.nox_index, liveData.co2_ppm, liveData.pm1_0, liveData.pm2_5, liveData.pm10_0);
        Serial.println(" ");
        dataFile.printf("%s,%d,%d,%.2f,%.2f,%d,%d,%d,%d,%d,%d,%d,%d\n", dateTime, liveData.stat1, liveData.stat2, liveData.temperature, liveData.rh, liveData.voc_raw, liveData.nox_raw, liveData.voc_index, liveData.nox_index, liveData.co2_ppm, liveData.pm1_0, liveData.pm2_5, liveData.pm10_0);
        dataFile.close();
      } else {
        Serial.println("Error opening sensor_data.csv");
      }
      xSemaphoreGive(dataMutex);
    }
    vTaskDelay(pdMS_TO_TICKS(300000));
  }
}

void Display_Task(void *parameter) {
  bool led1state = false;
  bool led2state = false;
  bool led3state = false;
  bool led4state = false;

  while (1) {
    if (xSemaphoreTake(dataMutex, pdMS_TO_TICKS(100)) == pdTRUE) {

      // LED 1 Alert: CO2
      if (liveData.co2_ppm > 1200) {
        if (led1state) {
          ledcFade(LED1, 0, 255, 1000);
          led1state = !led1state;
        } else {
          ledcFade(LED1, 255, 0, 1000);
          led1state = !led1state;
        }
      } else {
        ledcWrite(LED1, 0);
      }



      // LED 2 Alert: PM
      if (liveData.pm1_0 > 35 || liveData.pm2_5 > 35 || liveData.pm10_0 > 35) {
        if (led2state) {
          ledcFade(LED2, 0, 255, 1000);
          led2state = !led2state;
        } else {
          ledcFade(LED2, 255, 0, 1000);
          led2state = !led2state;
        }
      } else {
        ledcWrite(LED2, 0);
      }

      // LED 3 Alert: VOC
      if (liveData.voc_index > 120) {
        if (led3state) {
          ledcFade(LED3, 0, 255, 1000);
          led3state = !led3state;
        } else {
          ledcFade(LED3, 255, 0, 1000);
          led3state = !led3state;
        }
      } else {
        ledcWrite(LED3, 0);
      }

      // LED 4 Alert: Nox
      if (liveData.nox_index > 2) {
        if (led4state) {
          ledcFade(LED4, 0, 255, 1000);
          led4state = !led4state;
        } else {
          ledcFade(LED4, 255, 0, 1000);
          led4state = !led4state;
        }
      } else {
        ledcWrite(LED4, 0);
      }
      xSemaphoreGive(dataMutex);
    }

    vTaskDelay(1000);
  }
}


void setup() {
  Serial.begin(115200);
  Wire.begin(SDA, SCL);

  ledcAttach(LED1, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED2, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED3, LEDC_FREQ, LEDC_RES);
  ledcAttach(LED4, LEDC_FREQ, LEDC_RES);

  pinMode(CE, OUTPUT);
  pinMode(STAT1, INPUT_PULLUP);
  pinMode(STAT2, INPUT_PULLUP);
  pinMode(PMS_SET, OUTPUT);
  pinMode(CO2_EN, OUTPUT);

  digitalWrite(CE, LOW);  // Enable Battery Charging

  // Initialize SD card
  SPI.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  if (!SD.begin(SD_CS)) {
    Serial.println("SD card initialization failed!");
  }
  Serial.println("SD card initialized successfully.");

  // Setup sensor comms
  sht4x.begin(Wire, SHT40_I2C_ADDR_44);
  sgp41.begin(Wire);

  // Setup PMS Sensor
  Serial1.begin(9600, SERIAL_8N1, PMS_RX, PMS_TX);
  pms.passiveMode();           // Switch to passive mode
  digitalWrite(PMS_SET, LOW);  // Put sensor to sleep

  // Init and Config CO2 Sensor
  CO2Sensor.init();

  // Setup initial wifi connection using local AP
  WiFiManager wm;
  bool res;
  res = wm.autoConnect("AirQualityMonitor", "password");  // password protected ap
  if (!res) {
    Serial.println("Failed to connect");
  } else {
    Serial.println("connected...yeey :)");
    // Init and get the time
    configTime(gmtOffset_sec, daylightOffset_sec, ntpServer);

    struct tm timeinfo;
    if (!getLocalTime(&timeinfo)) {
      Serial.println("Failed to obtain time");
      return;
    }
    Serial.println(&timeinfo, "%A, %B %d %Y %H:%M:%S");
    wm.disconnect();      // Disconnect from the network
    WiFi.mode(WIFI_OFF);  // Turn off the WiFi radio
    btStop();             // Turn off bluetooth
  }

  // Configure power management
  // esp_pm_config_t pm_config = {
  //   .max_freq_mhz = 80,
  //   .min_freq_mhz = 10,
  //   .light_sleep_enable = true
  // };
  // esp_pm_configure(&pm_config);
  esp_pm_config_t pmConfig;
  pmConfig.min_freq_mhz = 80;
  pmConfig.max_freq_mhz = 160;
  pmConfig.light_sleep_enable = true;

  uint8_t err = esp_pm_configure(&pmConfig);
  Serial.println(err);


  // Create mutex for data access
  dataMutex = xSemaphoreCreateMutex();
  //                     (Function Name, Debug Name, Stack Size, Parameter, Priority,   Task Handle Name, Core)
  xTaskCreatePinnedToCore(BQ25185_Task, "BQ25185", 4096, NULL, 1, &BQ25185_THandle, 0);
  xTaskCreatePinnedToCore(VOCNOX_Task, "TempRH", 4096, NULL, 1, &vocNoxRHT_THandle, 0);
  xTaskCreatePinnedToCore(CO2_Task, "CO2", 4096, NULL, 1, &co2_THandle, 0);
  xTaskCreatePinnedToCore(CO2_ABC_Task, "CO2ABC", 4096, NULL, 1, &CO2_ABC_THandle, 0);
  xTaskCreatePinnedToCore(PMS_Task, "PM", 4096, NULL, 1, &pms_THandle, 0);
  xTaskCreatePinnedToCore(uSD_Task, "SDWrite", 4096, NULL, 2, &uSD_THandle, 0);
  xTaskCreatePinnedToCore(Display_Task, "Display", 4096, NULL, 1, &display_THandle, 0);
}

void loop() {
  vTaskDelete(NULL);  // Delete the loop task
}