#include "radar_task.h"
#include "wifi_task.h"
#include <string.h>
#include <sys/time.h>

static const char* TAG = "RadarTask";

RadarTask::RadarTask(uint8_t id) : moduleId(id) {
    uartConfig = {
        .baud_rate = RADAR_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
}

void RadarTask::init() {
    configureUART();
    configureGPIO();
}

void RadarTask::configureUART() {
    ESP_ERROR_CHECK(uart_param_config(RADAR_UART_PORT, &uartConfig));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART_PORT, RADAR_TX_PIN, RADAR_RX_PIN, 
                                UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART_PORT, RADAR_BUF_SIZE * 2, 
                                      RADAR_BUF_SIZE * 2, 0, NULL, 0));
}

void RadarTask::configureGPIO() {
    gpio_config_t io_conf = {};
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << RADAR_ENABLE_PIN);
    io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
    io_conf.pull_up_en = GPIO_PULLUP_DISABLE;
    ESP_ERROR_CHECK(gpio_config(&io_conf));
}

void RadarTask::start() {
    xTaskCreate(taskFunction, "radar_task", 4096, this, 3, NULL);
}

void RadarTask::taskFunction(void* pvParameters) {
    RadarTask* task = static_cast<RadarTask*>(pvParameters);
    uint8_t* data = (uint8_t*) malloc(RADAR_BUF_SIZE);
    
    while (1) {
        // Enable radar module
        gpio_set_level(RADAR_ENABLE_PIN, 1);
        vTaskDelay(pdMS_TO_TICKS(10)); // Wait for radar to stabilize
        
        // Read radar data
        int len = uart_read_bytes(RADAR_UART_PORT, data, RADAR_BUF_SIZE, 
                                pdMS_TO_TICKS(100));
        
        if (len > 0) {
            // Process radar data and extract metrics
            float distance = 0.0f;
            uint8_t signal = 0;
            RadarPosture posture = task->determinePosture(data, len);
            
            // Create and publish JSON payload
            task->createJsonPayload(distance, posture, signal);
        }
        
        // Disable radar module
        gpio_set_level(RADAR_ENABLE_PIN, 0);
        vTaskDelay(pdMS_TO_TICKS(90)); // Complete 100ms cycle
    }
    
    free(data);
}

RadarPosture RadarTask::determinePosture(uint8_t* data, size_t len) {
    // TODO: Implement actual posture detection algorithm based on radar data
    return POSTURE_STILL;
}

void RadarTask::createJsonPayload(float distance, RadarPosture posture, uint8_t signal) {
    cJSON *root = cJSON_CreateObject();
    
    cJSON_AddNumberToObject(root, "id_module", moduleId);
    cJSON_AddNumberToObject(root, "timestamp", time(NULL));
    cJSON_AddNumberToObject(root, "distance_m", distance);
    
    const char* postureStr;
    switch (posture) {
        case POSTURE_STILL: postureStr = "STILL"; break;
        case POSTURE_MOVING: postureStr = "MOVING"; break;
        case POSTURE_LYING: postureStr = "LYING"; break;
        default: postureStr = "UNKNOWN"; break;
    }
    cJSON_AddStringToObject(root, "posture", postureStr);
    cJSON_AddNumberToObject(root, "signal", signal);
    
    char* jsonString = cJSON_Print(root);
    // TODO: Send to MQTT queue
    cJSON_Delete(root);
    free(jsonString);
}