#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <mqtt_client.h>
#include <nvs_flash.h>
#include "cJSON.h"

class NetworkManager {
public:
    NetworkManager(const char* ssid, const char* password);
    void init();
    void start();
    static void taskFunction(void* pvParameters);
    QueueHandle_t getRadarDataQueue() { return radarDataQueue; }

private:
    const char* ssid;
    const char* password;
    esp_mqtt_client_handle_t mqtt_client;
    QueueHandle_t radarDataQueue;
    
    void initWiFi();
    void initMQTT();
    static void wifiEventHandler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data);
    static void mqttEventHandler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data);
    void processRadarData(const char* topic, const char* data);
};