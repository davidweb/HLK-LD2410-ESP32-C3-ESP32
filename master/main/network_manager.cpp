#include "network_manager.h"
#include <string.h>

static const char* TAG = "NetworkManager";

NetworkManager::NetworkManager(const char* ssid, const char* password)
    : ssid(ssid), password(password) {
    radarDataQueue = xQueueCreate(10, sizeof(cJSON*));
}

void NetworkManager::init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    initWiFi();
    initMQTT();
}

void NetworkManager::initWiFi() {
    esp_netif_create_default_wifi_sta();
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                             &wifiEventHandler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                             &wifiEventHandler, NULL));
    
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, ssid);
    strcpy((char*)wifi_config.sta.password, password);
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

void NetworkManager::initMQTT() {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = "mqtt://localhost";  // Local broker
    mqtt_cfg.credentials.username = "master";
    mqtt_cfg.credentials.authentication.password = "master_password";
    mqtt_cfg.broker.verification.certificate = NULL;  // Add your certificate for TLS
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                 mqttEventHandler, this);
    esp_mqtt_client_start(mqtt_client);
}

void NetworkManager::wifiEventHandler(void* arg, esp_event_base_t event_base,
                                    int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT) {
        switch (event_id) {
            case WIFI_EVENT_STA_START:
                esp_wifi_connect();
                break;
            case WIFI_EVENT_STA_DISCONNECTED:
                esp_wifi_connect();
                break;
        }
    }
}

void NetworkManager::mqttEventHandler(void *handler_args, esp_event_base_t base,
                                    int32_t event_id, void *event_data) {
    NetworkManager* manager = static_cast<NetworkManager*>(handler_args);
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            esp_mqtt_client_subscribe(event->client, "home/+/radar+", 1);
            break;
            
        case MQTT_EVENT_DATA:
            if (event->data && event->topic) {
                char* data = strndup(event->data, event->data_len);
                char* topic = strndup(event->topic, event->topic_len);
                manager->processRadarData(topic, data);
                free(data);
                free(topic);
            }
            break;
            
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
    }
}

void NetworkManager::processRadarData(const char* topic, const char* data) {
    cJSON* json = cJSON_Parse(data);
    if (json) {
        xQueueSend(radarDataQueue, &json, portMAX_DELAY);
    }
}

void NetworkManager::start() {
    xTaskCreate(taskFunction, "network_manager", 4096, this, 2, NULL);
}

void NetworkManager::taskFunction(void* pvParameters) {
    NetworkManager* manager = static_cast<NetworkManager*>(pvParameters);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}