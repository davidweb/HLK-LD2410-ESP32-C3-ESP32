#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_http_client.h>
#include <mqtt_client.h>

class AlertManager {
public:
    AlertManager(QueueHandle_t alertQueue, esp_mqtt_client_handle_t mqtt_client);
    void start();
    static void taskFunction(void* pvParameters);

private:
    QueueHandle_t alertQueue;
    esp_mqtt_client_handle_t mqtt_client;
    
    void sendMQTTAlert(const char* message);
    void sendHTTPAlert(const char* message);
    void triggerLocalAlert();
};