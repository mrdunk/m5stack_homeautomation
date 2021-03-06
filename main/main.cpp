#include <M5Stack.h>

#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "freertos/event_groups.h"

#include "menu.h"
#include "occupancy.h"
#include "mqtt.h"
#include "temparature.h"
#include "outlet.h"
#include "googleSheet.h"
#include "config.h"
#include "getUrl.h"

#if CONFIG_FREERTOS_UNICORE
#define ARDUINO_RUNNING_CORE 0
#else
#define ARDUINO_RUNNING_CORE 1
#endif

#define BOILER_TOPIC GLOBAL_ID "/heating/state" 


mrdunk::Temperature* temperature;
mrdunk::OutletKankun* outletBoiler;
mrdunk::HttpRequest* httpRequest;

// The setup routine runs once when M5Stack starts up
void setup() {
  esp_log_level_set("phy_init", ESP_LOG_INFO);
  // Initialize the M5Stack object
  M5.begin(true, false);

  // LCD display
  M5.Lcd.clearDisplay();
  M5.Lcd.setTextColor(WHITE);
  M5.Lcd.setTextSize(4);
  M5.Lcd.setCursor(10, 10);
  M5.Lcd.print("Loading...\n");
  setupMenu();

  // Networking
  wifi_init();

  mqtt_app_start();

  temperature = new mrdunk::Temperature("inside/downstairs/livingroom");
  outletBoiler = new mrdunk::OutletKankun(BOILER_TOPIC, false);
}

// Every 20ms.
void loopTask(void *pvParameters) {
  while(1) {
    micros();     // update overflow
    M5.update();  // Reads buttons, updates speaker.
    occupancy_update();
    updateMenu();
    delay(20 / portTICK_PERIOD_MS);
  }
}

// Every few seconds. 15 * 1000ms
void loopTask15sec(void *pvParameters) {
  char currentTemp[20] = "";
  while(1) {
    float temp = temperature->average();
    if(isnan(temp)) {
      snprintf(currentTemp, 20, "Waiting for update.");
    } else {
      snprintf(currentTemp, 16, "%.1fC\n", temp);
    }
    setButton0(currentTemp, nullptr);
    setButton1(outletBoiler->getState());

    delay(15 * 1000 / portTICK_PERIOD_MS);
  }
}

void loopTask30min(void *pvParameters) {
  while(1) {
    if(outletBoiler->getState()) {
      googleSheet_connect(WEB_URL, "?topic=" BOILER_TOPIC "&state=1");
    } else {
      googleSheet_connect(WEB_URL, "?topic=" BOILER_TOPIC "&state=0");
    }
    delay(30 * 60 * 1000 / portTICK_PERIOD_MS);
  }
}

extern "C" void app_main() {
  esp_chip_info_t chip_info;
  esp_chip_info(&chip_info);
  printf("This is ESP32 chip with %d CPU cores, WiFi%s%s, ",
      chip_info.cores,
      (chip_info.features & CHIP_FEATURE_BT) ? "/BT" : "",
      (chip_info.features & CHIP_FEATURE_BLE) ? "/BLE" : "");

  printf("silicon revision %d, ", chip_info.revision);

  printf("%dMB %s flash\n\n", spi_flash_get_chip_size() / (1024 * 1024),
      (chip_info.features & CHIP_FEATURE_EMB_FLASH) ? "embedded" : "external");

  //initArduino();
  setup();

  xTaskCreatePinnedToCore(
      loopTask, "loopTask", 8192, NULL, 1, NULL, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(
      loopTask15sec, "loopTask15sec", 8192, NULL, 10, NULL, ARDUINO_RUNNING_CORE);
  xTaskCreatePinnedToCore(
      loopTask30min, "loopTask30min", 8192, NULL, 10, NULL, ARDUINO_RUNNING_CORE);

  while(1) {
    vTaskDelay(1000 / portTICK_PERIOD_MS);
  }
}
