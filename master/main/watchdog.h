#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <esp_mqtt_client.h>
#include <map>
#include <string>

class Watchdog {
public:
    Watchdog(esp_mqtt_client_handle_t mqtt_client);
    void start();
    static void taskFunction(void* pvParameters);
    void updateModuleTimestamp(int moduleId);

private:
    esp_mqtt_client_handle_t mqtt_client;
    std::map<int, int64_t> lastSeenTimestamps;
    
    void checkModules();
    void sendOfflineAlert(int moduleId);
};