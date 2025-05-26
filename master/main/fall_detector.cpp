#include "fall_detector.h"
#include <esp_log.h>
#include <esp_timer.h>

static const char* TAG = "FallDetector";

FallDetector::FallDetector(QueueHandle_t postureQueue)
    : postureQueue(postureQueue), fallDetected(false),
      previousPosture(GlobalPosture::UNKNOWN), previousTime(0) {
    alertQueue = xQueueCreate(5, sizeof(bool));
}

void FallDetector::start() {
    xTaskCreate(taskFunction, "fall_detector", 4096, this, 4, NULL);
}

bool FallDetector::detectFall(GlobalPosture currentPosture, int64_t currentTime) {
    if (previousPosture == GlobalPosture::STILL &&
        currentPosture == GlobalPosture::LYING) {
        
        // Check if transition happened within 1 second
        if ((currentTime - previousTime) < 1000) {
            return true;
        }
    }
    
    previousPosture = currentPosture;
    previousTime = currentTime;
    return false;
}

void FallDetector::taskFunction(void* pvParameters) {
    FallDetector* detector = static_cast<FallDetector*>(pvParameters);
    GlobalPosture currentPosture;
    int64_t lyingStartTime = 0;
    
    while (1) {
        if (xQueueReceive(detector->postureQueue, &currentPosture, portMAX_DELAY)) {
            int64_t currentTime = esp_timer_get_time() / 1000; // Convert to ms
            
            if (detector->detectFall(currentPosture, currentTime)) {
                lyingStartTime = currentTime;
            }
            
            // Confirm fall if person stays lying for more than 20 seconds
            if (currentPosture == GlobalPosture::LYING &&
                lyingStartTime > 0 &&
                (currentTime - lyingStartTime) > 20000) {
                
                detector->fallDetected = true;
                bool alert = true;
                xQueueSend(detector->alertQueue, &alert, 0);
                ESP_LOGI(TAG, "Fall detected!");
                
                // Reset detection
                lyingStartTime = 0;
            }
            
            // Reset if person is no longer lying
            if (currentPosture != GlobalPosture::LYING) {
                lyingStartTime = 0;
                detector->fallDetected = false;
            }
        }
    }
}