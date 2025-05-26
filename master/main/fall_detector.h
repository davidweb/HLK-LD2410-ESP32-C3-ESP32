#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "fusion_engine.h"

class FallDetector {
public:
    FallDetector(QueueHandle_t postureQueue);
    void start();
    static void taskFunction(void* pvParameters);
    bool isFallDetected() const { return fallDetected; }
    QueueHandle_t getAlertQueue() { return alertQueue; }

private:
    QueueHandle_t postureQueue;
    QueueHandle_t alertQueue;
    bool fallDetected;
    GlobalPosture previousPosture;
    int64_t previousTime;
    
    bool detectFall(GlobalPosture currentPosture, int64_t currentTime);
};