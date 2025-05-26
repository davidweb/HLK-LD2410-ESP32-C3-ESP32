#include "wifi_task.h"

static const char* TAG = "WiFiTask";

WiFiTask::WiFiTask(const char* ssid, const char* password, const char* mqtt_uri)
    : ssid(ssid), password(password), mqtt_uri(mqtt_uri) {
}

void WiFiTask::init() {
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    initWiFi();
    initMQTT();
}

void WiFiTask::initWiFi() {
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

void WiFiTask::initMQTT() {
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.uri = mqtt_uri;
    mqtt_cfg.username = "your_username";  // Configure as needed
    mqtt_cfg.password = "your_password";  // Configure as needed
    mqtt_cfg.cert_pem = NULL;  // Add your certificate for TLS
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID,
                                 mqttEventHandler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

void WiFiTask::start() {
    xTaskCreate(taskFunction, "wifi_task", 4096, this, 2, NULL);
}

void WiFiTask::wifiEventHandler(void* arg, esp_event_base_t event_base,
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

void WiFiTask::mqttEventHandler(void *handler_args, esp_event_base_t base,
                              int32_t event_id, void *event_data) {
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT Connected");
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT Disconnected");
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT Error");
            break;
    }
}

bool WiFiTask::publishMessage(const char* topic, const char* message) {
    int msg_id = esp_mqtt_client_publish(mqtt_client, topic, message, 0, 1, 0);
    return msg_id != -1;
}

void WiFiTask::taskFunction(void* pvParameters) {
    WiFiTask* task = static_cast<WiFiTask*>(pvParameters);
    
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        // Monitor connection status and handle reconnection if needed
    }
}