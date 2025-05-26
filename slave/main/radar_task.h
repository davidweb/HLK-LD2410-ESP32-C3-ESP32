#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <driver/uart.h>
#include <driver/gpio.h>
#include <esp_log.h>
#include "cJSON.h"

// Constants for radar configuration
#define RADAR_UART_PORT UART_NUM_1
#define RADAR_TX_PIN GPIO_NUM_7
#define RADAR_RX_PIN GPIO_NUM_6
#define RADAR_ENABLE_PIN GPIO_NUM_5

#define RADAR_BAUD_RATE 256000
#define RADAR_BUF_SIZE 1024

// Radar states
typedef enum {
    POSTURE_UNKNOWN,
    POSTURE_STILL,
    POSTURE_MOVING,
    POSTURE_LYING
} RadarPosture;

class RadarTask {
public:
    RadarTask(uint8_t moduleId);
    void init();
    void start();
    static void taskFunction(void* pvParameters);

private:
    uint8_t moduleId;
    uart_config_t uartConfig;
    
    void configureUART();
    void configureGPIO();
    void processRadarData();
    void createJsonPayload(float distance, RadarPosture posture, uint8_t signal);
    RadarPosture determinePosture(uint8_t* data, size_t len);
};