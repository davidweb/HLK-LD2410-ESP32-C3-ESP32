#pragma once

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include "cJSON.h"

struct Position {
    float x;
    float y;
};

enum class GlobalPosture {
    UNKNOWN,
    STILL,
    MOVING,
    LYING
};

class FusionEngine {
public:
    FusionEngine(QueueHandle_t dataQueue);
    void start();
    static void taskFunction(void* pvParameters);
    QueueHandle_t getPostureQueue() { return postureQueue; }

private:
    QueueHandle_t radarDataQueue;
    QueueHandle_t postureQueue;
    
    Position calculatePosition(float d1, float x1, float y1, float d2, float x2, float y2);
    GlobalPosture determineGlobalPosture(const char* posture1, const char* posture2);
};