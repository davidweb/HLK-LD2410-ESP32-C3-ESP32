#include "fusion_engine.h"
#include <esp_log.h>
#include <math.h>

static const char* TAG = "FusionEngine";

struct RadarData {
    int id_module;
    int64_t timestamp;
    float distance_m;
    const char* posture;
    int signal;
};

FusionEngine::FusionEngine(QueueHandle_t dataQueue) 
    : radarDataQueue(dataQueue) {
    postureQueue = xQueueCreate(10, sizeof(GlobalPosture));
}

void FusionEngine::start() {
    xTaskCreate(taskFunction, "fusion_engine", 6144, this, 3, NULL);
}

Position FusionEngine::calculatePosition(float d1, float x1, float y1, 
                                      float d2, float x2, float y2) {
    // Simple triangulation implementation
    // This is a basic approximation - could be improved with more sophisticated algorithms
    float a = 2 * (x2 - x1);
    float b = 2 * (y2 - y1);
    float c = d1*d1 - d2*d2 - x1*x1 + x2*x2 - y1*y1 + y2*y2;
    
    Position pos;
    pos.x = (c*b)/(a*b);
    pos.y = (c*a)/(a*b);
    
    return pos;
}

GlobalPosture FusionEngine::determineGlobalPosture(const char* posture1, 
                                                 const char* posture2) {
    if (strcmp(posture1, "LYING") == 0 || strcmp(posture2, "LYING") == 0) {
        return GlobalPosture::LYING;
    }
    if (strcmp(posture1, "MOVING") == 0 || strcmp(posture2, "MOVING") == 0) {
        return GlobalPosture::MOVING;
    }
    if (strcmp(posture1, "STILL") == 0 && strcmp(posture2, "STILL") == 0) {
        return GlobalPosture::STILL;
    }
    return GlobalPosture::UNKNOWN;
}

void FusionEngine::taskFunction(void* pvParameters) {
    FusionEngine* engine = static_cast<FusionEngine*>(pvParameters);
    RadarData radar1Data = {}, radar2Data = {};
    cJSON* json;
    
    while (1) {
        if (xQueueReceive(engine->radarDataQueue, &json, portMAX_DELAY)) {
            int id_module = cJSON_GetObjectItem(json, "id_module")->valueint;
            int64_t timestamp = cJSON_GetObjectItem(json, "timestamp")->valueint;
            float distance = cJSON_GetObjectItem(json, "distance_m")->valuedouble;
            const char* posture = cJSON_GetObjectItem(json, "posture")->valuestring;
            int signal = cJSON_GetObjectItem(json, "signal")->valueint;
            
            if (id_module == 1) {
                radar1Data = {id_module, timestamp, distance, posture, signal};
            } else if (id_module == 2) {
                radar2Data = {id_module, timestamp, distance, posture, signal};
            }
            
            // Process if we have data from both radars within 20ms
            if (abs(radar1Data.timestamp - radar2Data.timestamp) < 20) {
                Position pos = engine->calculatePosition(
                    radar1Data.distance_m, 0, 0,  // Radar 1 at origin
                    radar2Data.distance_m, 2, 0   // Radar 2 at (2,0)
                );
                
                GlobalPosture globalPosture = engine->determineGlobalPosture(
                    radar1Data.posture, radar2Data.posture
                );
                
                xQueueSend(engine->postureQueue, &globalPosture, 0);
                
                // Reset data
                radar1Data = {};
                radar2Data = {};
            }
            
            cJSON_Delete(json);
        }
    }
}