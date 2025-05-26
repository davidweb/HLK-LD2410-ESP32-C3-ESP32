#include <esp_system.h>
#include <nvs_flash.h>
#include "network_manager.h"
#include "fusion_engine.h"
#include "fall_detector.h"
#include "alert_manager.h"
#include "watchdog.h"

extern "C" void app_main(void) {
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Create and initialize NetworkManager
    NetworkManager* networkManager = new NetworkManager(
        "your_ssid",     // Replace with actual SSID
        "your_password"  // Replace with actual password
    );
    networkManager->init();
    networkManager->start();
    
    // Create and initialize FusionEngine
    FusionEngine* fusionEngine = new FusionEngine(networkManager->getRadarDataQueue());
    fusionEngine->start();
    
    // Create and initialize FallDetector
    FallDetector* fallDetector = new FallDetector(fusionEngine->getPostureQueue());
    fallDetector->start();
    
    // Create and initialize AlertManager
    AlertManager* alertManager = new AlertManager(
        fallDetector->getAlertQueue(),
        networkManager->getMqttClient()
    );
    alertManager->start();
    
    // Create and initialize Watchdog
    Watchdog* watchdog = new Watchdog(networkManager->getMqttClient());
    watchdog->start();
}