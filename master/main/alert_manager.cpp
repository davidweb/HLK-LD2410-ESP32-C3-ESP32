#include "alert_manager.h"
#include <esp_log.h>
#include <driver/gpio.h>

static const char* TAG = "AlertManager";

// Define GPIO pins for local alerts
#define BUZZER_PIN GPIO_NUM_18
#define LED_PIN GPIO_NUM_19

AlertManager::AlertManager(QueueHandle_t alertQueue, esp_mqtt_client_handle_t mqtt_client)
    : alertQueue(alertQueue), mqtt_client(mqtt_client) {
    
    // Configure GPIO for buzzer and LED
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << BUZZER_PIN) | (1ULL << LED_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    gpio_config(&io_conf);
}

void AlertManager::start() {
    xTaskCreate(taskFunction, "alert_manager", 4096, this, 5, NULL);
}

void AlertManager::sendMQTTAlert(const char* message) {
    esp_mqtt_client_publish(mqtt_client, "home/room1/alert", message, 0, 1, 0);
}

void AlertManager::sendHTTPAlert(const char* message) {
    esp_http_client_config_t config = {
        .url = "http://your-alert-endpoint.com/api/alert",
        .method = HTTP_METHOD_POST,
    };
    
    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_post_field(client, message, strlen(message));
    esp_http_client_set_header(client, "Content-Type", "application/json");
    
    esp_err_t err = esp_http_client_perform(client);
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "HTTP POST Status = %d",
                 esp_http_client_get_status_code(client));
    }
    
    esp_http_client_cleanup(client);
}

void AlertManager::triggerLocalAlert() {
    // Activate buzzer and LED
    gpio_set_level(BUZZER_PIN, 1);
    gpio_set_level(LED_PIN, 1);
    
    // Keep alert active for 5 seconds
    vTaskDelay(pdMS_TO_TICKS(5000));
    
    // Deactivate
    gpio_set_level(BUZZER_PIN, 0);
    gpio_set_level(LED_PIN, 0);
}

void AlertManager::taskFunction(void* pvParameters) {
    AlertManager* manager = static_cast<AlertManager*>(pvParameters);
    bool alert;
    
    while (1) {
        if (xQueueReceive(manager->alertQueue, &alert, portMAX_DELAY)) {
            if (alert) {
                // Send MQTT alert
                manager->sendMQTTAlert("FALL_DETECTED");
                
                // Send HTTP alert
                manager->sendHTTPAlert("{\"type\":\"FALL_DETECTED\"}");
                
                // Trigger local alert
                manager->triggerLocalAlert();
            }
        }
    }
}