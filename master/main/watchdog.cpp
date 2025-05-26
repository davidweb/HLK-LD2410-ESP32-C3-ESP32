#include "watchdog.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "Watchdog";

Watchdog::Watchdog(esp_mqtt_client_handle_t mqtt_client)
    : mqtt_client(mqtt_client) {
}

void Watchdog::start() {
    xTaskCreate(taskFunction, "watchdog", 4096, this, 1, NULL);
}

void Watchdog::updateModuleTimestamp(int moduleId) {
    lastSeenTimestamps[moduleId] = esp_timer_get_time() / 1000; // Convert to ms
}

void Watchdog::checkModules() {
    int64_t currentTime = esp_timer_get_time() / 1000;
    
    for (const auto& module : lastSeenTimestamps) {
        if ((currentTime - module.second) > 2000) { // 2 seconds timeout
            sendOfflineAlert(module.first);
        }
    }
}

void Watchdog::sendOfflineAlert(int moduleId) {
    char message[50];
    snprintf(message, sizeof(message), "MODULE_OFFLINE_%d", moduleId);
    esp_mqtt_client_publish(mqtt_client, "home/room1/alert", message, 0, 1, 0);
}

void Watchdog::taskFunction(void* pvParameters) {
    Watchdog* watchdog = static_cast<Watchdog*>(pvParameters);
    
    while (1) {
        watchdog->checkModules();
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}