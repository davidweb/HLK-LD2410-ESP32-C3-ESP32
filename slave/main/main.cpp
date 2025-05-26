#include <esp_system.h>
#include <nvs_flash.h>
#include "radar_task.h"
#include "wifi_task.h"

extern "C" void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create and initialize WiFi task
    WiFiTask* wifiTask = new WiFiTask(
        "your_ssid",          // Replace with actual SSID
        "your_password",      // Replace with actual password
        "mqtt://192.168.1.1"  // Replace with actual MQTT broker URI
    );
    wifiTask->init();
    wifiTask->start();
    
    // Create and initialize Radar task
    RadarTask* radarTask = new RadarTask(1); // Module ID 1
    radarTask->init();
    radarTask->start();
}