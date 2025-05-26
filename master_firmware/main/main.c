#include <stdio.h>
#include <string.h>
#include <stdlib.h> // For abs()
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h" // For FreeRTOS queues
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "mqtt_client.h"
// Note: cJSON.h is not included as manual parsing will be implemented.

static const char *TAG_MAIN_APP = "master_main"; 
static const char *TAG_NETWORK = "NetworkManager";
static const char *TAG_FUSION = "FusionEngine";
static const char *TAG_FALL_DETECTOR = "FallDetector";
static const char *TAG_ALERT_MANAGER = "AlertManager";
static const char *TAG_WATCHDOG = "WatchdogTask";


// Wi-Fi Configuration
#define MASTER_ESP_WIFI_SSID      "your_master_wifi_ssid"
#define MASTER_ESP_WIFI_PASS      "your_master_wifi_password"
#define MASTER_ESP_MAXIMUM_RETRY  5

// MQTT Configuration
#define MASTER_CONFIG_BROKER_URL          "mqtt://192.168.1.100" 
#define HOME_MQTT_TOPIC_WILDCARD      "home/+/radar+"    
#define MASTER_MQTT_CLIENT_ID             "esp32_master_controller_1"
#define ALERT_TOPIC                       "home/room1/alert"


// Event Group for Wi-Fi connection status
static EventGroupHandle_t wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// MQTT Client Handle and connection flag (file static, accessible within main.c)
static esp_mqtt_client_handle_t client_handle = NULL;
static bool mqtt_connected_flag = false;

// Task Priorities (as per cahier des charges)
#define NETWORK_TASK_PRIORITY    2
#define FUSION_TASK_PRIORITY     3
#define FALL_DETECTION_TASK_PRIORITY 4 
#define ALERT_TASK_PRIORITY      5
#define WATCHDOG_TASK_PRIORITY   1 // Watchdog should have a low priority, but higher than IDLE

// Radar Data Fusion Definitions
typedef struct {
    int module_id;
    uint32_t timestamp; // This is the slave's timestamp
    float distance_m;
    char posture[16]; 
    int signal;
} RadarMessage;

// Fall Detector Definitions
#define STANDING_POSTURE "STANDING" 
#define SITTING_POSTURE  "SITTING"  
#define LYING_POSTURE    "LYING"
#define MOVING_POSTURE   "MOVING"   
#define STILL_POSTURE    "STILL"    

#define FALL_TRANSITION_MAX_MS 1000       
#define LYING_CONFIRMATION_DURATION_S 20  

typedef struct {
    float x, y;
    char final_posture[16];
    uint32_t timestamp; // Average or one of the sensor timestamps from master's perspective
} FusedData;

// Alert Manager Definitions
typedef enum { 
    ALERT_TYPE_FALL_DETECTED, 
    ALERT_TYPE_MODULE_OFFLINE,
    ALERT_TYPE_MODULE_ONLINE // Optional: For module online notifications
} AlertType;

typedef struct {
    AlertType type;
    char description[64]; 
    uint32_t alert_timestamp; 
} AlertMessage;

// Watchdog Definitions
#define NUM_SLAVE_MODULES 2
#define WATCHDOG_CHECK_INTERVAL_S 2
#define SLAVE_MODULE_TIMEOUT_S 5  // Threshold before considering a module offline (e.g., 30 seconds)

static uint32_t last_received_timestamp_ms[NUM_SLAVE_MODULES] = {0};
static bool module_offline_alerted[NUM_SLAVE_MODULES] = {false}; // Tracks if an OFFLINE alert has been sent for a module
static uint32_t system_start_time_ms = 0;


#define RADAR_DATA_QUEUE_SIZE 10
#define FUSION_OUTPUT_QUEUE_SIZE 5 
#define ALERT_QUEUE_SIZE 5 // Increased slightly for potential module online/offline alerts
#define SENSOR_SYNC_WINDOW_MS 500 

static QueueHandle_t radar_data_queue;
static QueueHandle_t fusion_output_queue;
static QueueHandle_t alert_queue;


// Task function declarations
void NetworkManager_task(void *pvParameters);
void FusionEngine_task(void *pvParameters);
void FallDetector_task(void *pvParameters);
void AlertManager_task(void *pvParameters);
void Watchdog_task(void *pvParameters);

// Network related function declarations
static void nvs_init();
static void master_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void master_wifi_init_sta(void);
static bool parse_radar_json(const char* json_str, int data_len, RadarMessage* msg);
static void master_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
static void master_mqtt_app_start(void);

// Fusion Engine related function declarations
static bool calculate_xy_position(float d1, float d2, float* x, float* y);


void app_main(void)
{
    ESP_LOGI(TAG_MAIN_APP, "Starting Master Firmware application");

    nvs_init(); 

    radar_data_queue = xQueueCreate(RADAR_DATA_QUEUE_SIZE, sizeof(RadarMessage));
    if (radar_data_queue == NULL) {
        ESP_LOGE(TAG_MAIN_APP, "Failed to create radar_data_queue. Halting.");
        while(1); 
    }
    ESP_LOGI(TAG_MAIN_APP, "radar_data_queue created successfully.");

    fusion_output_queue = xQueueCreate(FUSION_OUTPUT_QUEUE_SIZE, sizeof(FusedData));
    if (fusion_output_queue == NULL) {
        ESP_LOGE(TAG_MAIN_APP, "Failed to create fusion_output_queue. Halting.");
        while(1);
    }
    ESP_LOGI(TAG_MAIN_APP, "fusion_output_queue created successfully.");

    alert_queue = xQueueCreate(ALERT_QUEUE_SIZE, sizeof(AlertMessage));
    if (alert_queue == NULL) {
        ESP_LOGE(TAG_MAIN_APP, "Failed to create alert_queue. Halting.");
        while(1);
    }
    ESP_LOGI(TAG_MAIN_APP, "alert_queue created successfully.");

    // Record system start time for Watchdog initial grace period
    system_start_time_ms = esp_log_timestamp(); 

    xTaskCreate(&NetworkManager_task, "NetworkManager_task", 4096*2, NULL, NETWORK_TASK_PRIORITY, NULL); 
    xTaskCreate(&FusionEngine_task, "FusionEngine_task", 4096, NULL, FUSION_TASK_PRIORITY, NULL);
    xTaskCreate(&FallDetector_task, "FallDetector_task", 4096, NULL, FALL_DETECTION_TASK_PRIORITY, NULL);
    xTaskCreate(&AlertManager_task, "AlertManager_task", 4096, NULL, ALERT_TASK_PRIORITY, NULL);
    xTaskCreate(&Watchdog_task, "Watchdog_task", 2048, NULL, WATCHDOG_TASK_PRIORITY, NULL); // Priority 10 as previously set

    ESP_LOGI(TAG_MAIN_APP, "All tasks created.");
}

static void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_NETWORK, "NVS flash initialized successfully.");
}

static int s_master_retry_num = 0;

static void master_wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_NETWORK, "Wi-Fi STA Started, attempting to connect...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG_NETWORK, "Wi-Fi STA Connected to AP: %s", MASTER_ESP_WIFI_SSID);
        s_master_retry_num = 0;
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        mqtt_connected_flag = false; 
        if (s_master_retry_num < MASTER_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_master_retry_num++;
            ESP_LOGI(TAG_NETWORK, "Retry Wi-Fi connection (%d/%d)...", s_master_retry_num, MASTER_ESP_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG_NETWORK, "Failed to connect to Wi-Fi AP %s after %d retries.", MASTER_ESP_WIFI_SSID, MASTER_ESP_MAXIMUM_RETRY);
        }
        ESP_LOGE(TAG_NETWORK,"Disconnected from AP %s", MASTER_ESP_WIFI_SSID);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_NETWORK, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_master_retry_num = 0;
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void master_wifi_init_sta(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &master_wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &master_wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = MASTER_ESP_WIFI_SSID,
            .password = MASTER_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_NETWORK, "master_wifi_init_sta finished. Waiting for connection...");

    #if 1 
    ESP_LOGW(TAG_NETWORK, "SIMULATION: Forcing Wi-Fi connected bit after 3 seconds for master.");
    vTaskDelay(pdMS_TO_TICKS(3000));
    xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    #endif
}

static bool parse_radar_json(const char* json_str, int data_len, RadarMessage* msg) {
    if (json_str == NULL || msg == NULL || data_len <= 0) {
        return false;
    }

    char temp_json_str[data_len + 1]; 
    memcpy(temp_json_str, json_str, data_len);
    temp_json_str[data_len] = '\\0';

    char* ptr;
    bool success = true;

    ptr = strstr(temp_json_str, "\"id_module\":");
    if (ptr) {
        if (sscanf(ptr + strlen("\"id_module\":"), "%d", &msg->module_id) != 1) success = false;
    } else success = false;

    ptr = strstr(temp_json_str, "\"timestamp\":");
    if (ptr && success) {
        if (sscanf(ptr + strlen("\"timestamp\":"), "%u", &msg->timestamp) != 1) success = false;
    } else success = false;

    ptr = strstr(temp_json_str, "\"distance_m\":");
    if (ptr && success) {
        if (sscanf(ptr + strlen("\"distance_m\":"), "%f", &msg->distance_m) != 1) success = false;
    } else success = false;
    
    ptr = strstr(temp_json_str, "\"signal\":");
    if (ptr && success) {
        if (sscanf(ptr + strlen("\"signal\":"), "%d", &msg->signal) != 1) success = false;
    } else success = false;

    ptr = strstr(temp_json_str, "\"posture\": \"");
    if (ptr && success) {
        ptr += strlen("\"posture\": \"");
        char* end_quote = strchr(ptr, '\"');
        if (end_quote) {
            int posture_len = end_quote - ptr;
            if (posture_len < sizeof(msg->posture)) {
                strncpy(msg->posture, ptr, posture_len);
                msg->posture[posture_len] = '\\0';
            } else {
                ESP_LOGE(TAG_FUSION, "Posture string too long in JSON.");
                success = false;
            }
        } else success = false;
    } else success = false;

    if (success) {
        ESP_LOGD(TAG_FUSION, "Parsed JSON: id=%d, ts=%u, dist=%.2f, posture=%s, sig=%d",
                 msg->module_id, msg->timestamp, msg->distance_m, msg->posture, msg->signal);
    } else {
        ESP_LOGE(TAG_FUSION, "Failed to parse one or more fields in JSON: %s", temp_json_str);
    }
    return success;
}


static void master_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    ESP_LOGD(TAG_NETWORK, "MQTT Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client; 
    int msg_id;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_CONNECTED to broker %s", MASTER_CONFIG_BROKER_URL);
        mqtt_connected_flag = true;
        msg_id = esp_mqtt_client_subscribe(client, HOME_MQTT_TOPIC_WILDCARD, 1); 
        ESP_LOGI(TAG_NETWORK, "Sent subscribe successful to topic %s, msg_id=%d", HOME_MQTT_TOPIC_WILDCARD, msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected_flag = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED: 
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_DATA received");
        ESP_LOGI(TAG_NETWORK, "TOPIC=%.*s", event->topic_len, event->topic);
        ESP_LOGD(TAG_NETWORK, "DATA (len %d)=%.*s", event->data_len, event->data_len, event->data); 
        
        RadarMessage received_radar_msg;
        if (parse_radar_json(event->data, event->data_len, &received_radar_msg)) {
            ESP_LOGI(TAG_NETWORK, "Parsed Radar Data: ID=%d, TS=%u, Dist=%.2f, Posture=%s, Sig=%d",
                     received_radar_msg.module_id, received_radar_msg.timestamp,
                     received_radar_msg.distance_m, received_radar_msg.posture, received_radar_msg.signal);
            
            if (radar_data_queue != NULL) { 
                if (xQueueSend(radar_data_queue, &received_radar_msg, pdMS_TO_TICKS(100)) == pdPASS) {
                    ESP_LOGD(TAG_NETWORK, "Radar data sent to fusion_engine_queue successfully."); 
                } else {
                    ESP_LOGE(TAG_NETWORK, "Failed to send radar data to fusion_engine_queue (queue full?).");
                }
            } else {
                 ESP_LOGE(TAG_NETWORK, "radar_data_queue is NULL. Cannot send data.");
            }
        } else {
            ESP_LOGE(TAG_NETWORK, "Failed to parse incoming radar JSON data. Raw: %.*s", event->data_len, event->data);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_NETWORK, "MQTT_EVENT_ERROR");
        break;
    default:
        ESP_LOGI(TAG_NETWORK, "Other MQTT event id:%d", event->event_id);
        break;
    }
}

static void master_mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MASTER_CONFIG_BROKER_URL,
        .credentials.client_id = MASTER_MQTT_CLIENT_ID,
    };

    ESP_LOGI(TAG_NETWORK, "Attempting to start MQTT client, broker: %s", MASTER_CONFIG_BROKER_URL);
    client_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (client_handle == NULL) {
        ESP_LOGE(TAG_NETWORK, "Failed to initialize MQTT client");
        return;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client_handle, ESP_EVENT_ANY_ID, master_mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client_handle));
    
    #if 1 
    ESP_LOGW(TAG_NETWORK, "SIMULATION: MQTT client considered 'started'. Actual connection depends on network.");
    #endif
}


void NetworkManager_task(void *pvParameters) {
    ESP_LOGI(TAG_NETWORK, "NetworkManager_task started");
    
    master_wifi_init_sta();

    ESP_LOGI(TAG_NETWORK, "Waiting for Wi-Fi connection...");
    EventBits_t bits = xEventGroupWaitBits(wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, 
            pdFALSE, 
            portMAX_DELAY);

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_NETWORK, "Wi-Fi Connected. Initializing MQTT client...");
        master_mqtt_app_start();
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_NETWORK, "Wi-Fi connection failed. MQTT will not be started.");
    } else {
        ESP_LOGE(TAG_NETWORK, "Unexpected event from Wi-Fi event group.");
    }

    for(;;) {
        if (mqtt_connected_flag) {
            ESP_LOGD(TAG_NETWORK, "NetworkManager: MQTT connection active."); 
        } else {
            ESP_LOGW(TAG_NETWORK, "NetworkManager: MQTT connection lost or not established. Wi-Fi status: %s",
                (xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) ? "Connected" : "Disconnected");
            // TODO: Add robust MQTT reconnection logic here if Wi-Fi is connected.
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); 
    }
}

static bool calculate_xy_position(float d1, float d2, float* x, float* y) {
    ESP_LOGD(TAG_FUSION, "calculate_xy_position called with d1=%.2f, d2=%.2f", d1, d2);
    *x = 1.0f;
    *y = 1.5f;
    ESP_LOGI(TAG_FUSION, "Calculated position: x=%.2f, y=%.2f (stubbed)", *x, *y);
    return true;
}

void FusionEngine_task(void *pvParameters) {
    ESP_LOGI(TAG_FUSION, "FusionEngine_task started");

    static RadarMessage sensor1_data;
    static bool sensor1_data_valid = false;
    static RadarMessage sensor2_data;
    static bool sensor2_data_valid = false;

    RadarMessage current_msg;
    float pos_x, pos_y;
    char final_posture[16]; 

    for(;;) {
        if (xQueueReceive(radar_data_queue, &current_msg, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG_FUSION, "Received data from module_id: %d, ts: %u, dist: %.2f, posture: %s, signal: %d",
                     current_msg.module_id, current_msg.timestamp, current_msg.distance_m,
                     current_msg.posture, current_msg.signal);

            // Update last received timestamp for the specific module
            if (current_msg.module_id >= 1 && current_msg.module_id <= NUM_SLAVE_MODULES) {
                uint32_t reception_time_ms = esp_log_timestamp();
                last_received_timestamp_ms[current_msg.module_id - 1] = reception_time_ms;
                ESP_LOGD(TAG_FUSION, "Updated last_received_timestamp_ms for module %d to %u", current_msg.module_id, reception_time_ms);

                if (module_offline_alerted[current_msg.module_id - 1]) {
                    ESP_LOGI(TAG_FUSION, "Module %d is back online.", current_msg.module_id);
                    module_offline_alerted[current_msg.module_id - 1] = false;
                    // Optional: Send MODULE_ONLINE alert
                    // AlertMessage online_alert;
                    // online_alert.type = ALERT_TYPE_MODULE_ONLINE;
                    // online_alert.alert_timestamp = reception_time_ms;
                    // snprintf(online_alert.description, sizeof(online_alert.description), "Module %d back online", current_msg.module_id);
                    // if (alert_queue != NULL) xQueueSend(alert_queue, &online_alert, pdMS_TO_TICKS(10));
                }
            }


            if (current_msg.module_id == 1) { 
                sensor1_data = current_msg;
                sensor1_data_valid = true;
                ESP_LOGD(TAG_FUSION, "Stored data for Sensor 1 (ts: %u).", sensor1_data.timestamp);
            } else if (current_msg.module_id == 2) { 
                sensor2_data = current_msg;
                sensor2_data_valid = true;
                ESP_LOGD(TAG_FUSION, "Stored data for Sensor 2 (ts: %u).", sensor2_data.timestamp);
            } else {
                ESP_LOGW(TAG_FUSION, "Received data from unknown module_id: %d", current_msg.module_id);
            }

            if (sensor1_data_valid && sensor2_data_valid) {
                ESP_LOGD(TAG_FUSION, "Data from both sensors is valid. Checking timestamps...");
                ESP_LOGD(TAG_FUSION, "Sensor 1 TS: %u, Sensor 2 TS: %u, Diff: %d ms, Window: %d ms",
                         sensor1_data.timestamp, sensor2_data.timestamp,
                         abs((int32_t)sensor1_data.timestamp - (int32_t)sensor2_data.timestamp), SENSOR_SYNC_WINDOW_MS);

                if (abs((int32_t)sensor1_data.timestamp - (int32_t)sensor2_data.timestamp) <= SENSOR_SYNC_WINDOW_MS) {
                    ESP_LOGI(TAG_FUSION, "Synchronized data found for Sensor 1 and Sensor 2.");

                    calculate_xy_position(sensor1_data.distance_m, sensor2_data.distance_m, &pos_x, &pos_y);

                    if (strcmp(sensor1_data.posture, LYING_POSTURE) == 0 || strcmp(sensor2_data.posture, LYING_POSTURE) == 0) {
                        strcpy(final_posture, LYING_POSTURE);
                    } else if (strcmp(sensor1_data.posture, MOVING_POSTURE) == 0 || strcmp(sensor2_data.posture, MOVING_POSTURE) == 0) {
                        strcpy(final_posture, MOVING_POSTURE);
                    } else if (strcmp(sensor1_data.posture, SITTING_POSTURE) == 0 || strcmp(sensor2_data.posture, SITTING_POSTURE) == 0) {
                        strcpy(final_posture, SITTING_POSTURE);
                    } else if (strcmp(sensor1_data.posture, STANDING_POSTURE) == 0 || strcmp(sensor2_data.posture, STANDING_POSTURE) == 0) {
                        strcpy(final_posture, STANDING_POSTURE);
                    } else { 
                        strcpy(final_posture, STILL_POSTURE); 
                    }
                    ESP_LOGI(TAG_FUSION, "Final posture: %s", final_posture);

                    FusedData fused_output_data;
                    fused_output_data.x = pos_x;
                    fused_output_data.y = pos_y;
                    strcpy(fused_output_data.final_posture, final_posture);
                    fused_output_data.timestamp = (sensor1_data.timestamp > sensor2_data.timestamp) ? sensor1_data.timestamp : sensor2_data.timestamp;
                    
                    if (fusion_output_queue != NULL) {
                        if (xQueueSend(fusion_output_queue, &fused_output_data, pdMS_TO_TICKS(100)) == pdPASS) {
                            ESP_LOGD(TAG_FUSION, "Fused data sent to FallDetector_task queue.");
                        } else {
                            ESP_LOGE(TAG_FUSION, "Failed to send fused data to FallDetector_task queue (queue full?).");
                        }
                    } else {
                        ESP_LOGE(TAG_FUSION, "fusion_output_queue is NULL.");
                    }

                    sensor1_data_valid = false;
                    sensor2_data_valid = false;
                    ESP_LOGD(TAG_FUSION, "Sensor data invalidated, waiting for new pair.");
                } else {
                    ESP_LOGW(TAG_FUSION, "Data not synchronized: Timestamp diff %d ms > %d ms. Invalidating older data.",
                             abs((int32_t)sensor1_data.timestamp - (int32_t)sensor2_data.timestamp), SENSOR_SYNC_WINDOW_MS);
                    if (sensor1_data.timestamp < sensor2_data.timestamp) {
                        sensor1_data_valid = false; 
                        ESP_LOGI(TAG_FUSION, "Invalidated Sensor 1 data as it was older.");
                    } else {
                        sensor2_data_valid = false; 
                        ESP_LOGI(TAG_FUSION, "Invalidated Sensor 2 data as it was older.");
                    }
                }
            } else {
                ESP_LOGD(TAG_FUSION, "Waiting for data from the other sensor. S1_valid: %d, S2_valid: %d", sensor1_data_valid, sensor2_data_valid);
            }
        }
    }
}

void FallDetector_task(void *pvParameters) {
    ESP_LOGI(TAG_FALL_DETECTOR, "FallDetector_task started");

    static FusedData previous_data;
    static bool previous_data_valid = false;
    static uint32_t potential_fall_start_time_ms = 0;
    static bool in_potential_fall_state = false;

    FusedData current_data;

    for(;;) {
        if (xQueueReceive(fusion_output_queue, &current_data, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG_FALL_DETECTOR, "Received fused data: TS=%u, Pos=(%.2f, %.2f), Posture=%s",
                     current_data.timestamp, current_data.x, current_data.y, current_data.final_posture);

            if (previous_data_valid) {
                bool was_upright_or_moving = (strcmp(previous_data.final_posture, STANDING_POSTURE) == 0) ||
                                             (strcmp(previous_data.final_posture, SITTING_POSTURE) == 0)  ||
                                             (strcmp(previous_data.final_posture, MOVING_POSTURE) == 0)   ||
                                             (strcmp(previous_data.final_posture, STILL_POSTURE) == 0); 
                                   
                if (was_upright_or_moving && strcmp(current_data.final_posture, LYING_POSTURE) == 0) {
                    uint32_t transition_time_ms = current_data.timestamp - previous_data.timestamp;
                    ESP_LOGI(TAG_FALL_DETECTOR, "Transition to LYING detected. Prev: %s, Curr: %s, Time_diff: %u ms",
                             previous_data.final_posture, current_data.final_posture, transition_time_ms);

                    if (transition_time_ms < FALL_TRANSITION_MAX_MS) {
                        ESP_LOGW(TAG_FALL_DETECTOR, "Potential fall detected! Transition time: %u ms. Entering potential fall state.", transition_time_ms);
                        in_potential_fall_state = true;
                        potential_fall_start_time_ms = current_data.timestamp; 
                    } else {
                         ESP_LOGI(TAG_FALL_DETECTOR, "Transition to LYING too slow (%u ms), not considered a fall trigger.", transition_time_ms);
                    }
                }
            }

            if (in_potential_fall_state) {
                if (strcmp(current_data.final_posture, LYING_POSTURE) == 0) {
                    uint32_t lying_duration_ms = current_data.timestamp - potential_fall_start_time_ms;
                    ESP_LOGI(TAG_FALL_DETECTOR, "In potential fall state, current posture: LYING. Lying duration: %u ms.", lying_duration_ms);
                    if (lying_duration_ms >= (LYING_CONFIRMATION_DURATION_S * 1000)) {
                        ESP_LOGE(TAG_FALL_DETECTOR, "CHUTE CONFIRMÉE! Lying duration: %u ms.", lying_duration_ms);
                        
                        AlertMessage alert_msg;
                        alert_msg.type = ALERT_TYPE_FALL_DETECTED;
                        alert_msg.alert_timestamp = current_data.timestamp; 
                        snprintf(alert_msg.description, sizeof(alert_msg.description), 
                                 "Chute détectée à %lu (Pos: %.2f,%.2f)", 
                                 (unsigned long)current_data.timestamp, current_data.x, current_data.y);

                        if (alert_queue != NULL) {
                            if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) == pdPASS) {
                                ESP_LOGI(TAG_FALL_DETECTOR, "Fall alert sent to AlertManager_task queue.");
                            } else {
                                ESP_LOGE(TAG_FALL_DETECTOR, "Failed to send fall alert to AlertManager_task queue.");
                            }
                        } else {
                            ESP_LOGE(TAG_FALL_DETECTOR, "alert_queue is NULL!");
                        }
                        
                        in_potential_fall_state = false;
                        potential_fall_start_time_ms = 0;
                        ESP_LOGI(TAG_FALL_DETECTOR, "Fall state reset after confirmation.");
                    }
                } else { 
                    ESP_LOGI(TAG_FALL_DETECTOR, "Potential fall cancelled. Person no longer LYING. Current posture: %s", current_data.final_posture);
                    in_potential_fall_state = false;
                    potential_fall_start_time_ms = 0;
                }
            }
            
            previous_data = current_data;
            previous_data_valid = true;
        }
    }
}

void AlertManager_task(void *pvParameters) {
    ESP_LOGI(TAG_ALERT_MANAGER, "AlertManager_task started");
    AlertMessage received_alert;
    char mqtt_payload[128];

    for(;;) {
        if (xQueueReceive(alert_queue, &received_alert, portMAX_DELAY) == pdPASS) {
            ESP_LOGI(TAG_ALERT_MANAGER, "Received alert. Type: %d, Description: %s, Timestamp: %u",
                     received_alert.type, received_alert.description, received_alert.alert_timestamp);

            if (received_alert.type == ALERT_TYPE_FALL_DETECTED) {
                snprintf(mqtt_payload, sizeof(mqtt_payload), 
                         "{\"alert_type\": \"FALL_DETECTED\", \"description\": \"%s\", \"timestamp\": %u}", 
                         received_alert.description, received_alert.alert_timestamp);
            } else if (received_alert.type == ALERT_TYPE_MODULE_OFFLINE) {
                snprintf(mqtt_payload, sizeof(mqtt_payload), 
                         "{\"alert_type\": \"MODULE_OFFLINE\", \"description\": \"%s\", \"timestamp\": %u}", 
                         received_alert.description, received_alert.alert_timestamp);
            } else {
                snprintf(mqtt_payload, sizeof(mqtt_payload), 
                         "{\"alert_type\": \"UNKNOWN\", \"description\": \"%s\", \"timestamp\": %u}", 
                         received_alert.description, received_alert.alert_timestamp);
            }

            ESP_LOGI(TAG_ALERT_MANAGER, "Prepared MQTT Payload: %s", mqtt_payload);

            if (mqtt_connected_flag && client_handle != NULL) {
                int msg_id = esp_mqtt_client_publish(client_handle, ALERT_TOPIC, mqtt_payload, 0, 1, 0); 
                if (msg_id != -1) {
                    ESP_LOGI(TAG_ALERT_MANAGER, "Alert published to MQTT topic %s, msg_id=%d", ALERT_TOPIC, msg_id);
                } else {
                    ESP_LOGE(TAG_ALERT_MANAGER, "Failed to publish alert to MQTT topic %s", ALERT_TOPIC);
                }
            } else {
                ESP_LOGW(TAG_ALERT_MANAGER, "MQTT not connected. Alert not published via MQTT.");
            }

            ESP_LOGI(TAG_ALERT_MANAGER, "Placeholder for Buzzer/LED activation.");
            ESP_LOGI(TAG_ALERT_MANAGER, "Placeholder for HTTP POST/E-mail notification.");
        }
    }
}

void Watchdog_task(void *pvParameters) {
    ESP_LOGI(TAG_WATCHDOG, "Watchdog_task started");
    // system_start_time_ms is initialized in app_main before this task starts.

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_S * 1000));
        uint32_t current_time_ms = esp_log_timestamp();

        for (int i = 0; i < NUM_SLAVE_MODULES; i++) {
            bool initial_grace_period_passed = (current_time_ms - system_start_time_ms > (SLAVE_MODULE_TIMEOUT_S * 1000));

            if (last_received_timestamp_ms[i] == 0) { // Module has never sent data
                if (initial_grace_period_passed && !module_offline_alerted[i]) {
                    ESP_LOGW(TAG_WATCHDOG, "Module %d has never sent data after initial timeout.", i + 1);
                    
                    AlertMessage alert_msg;
                    alert_msg.type = ALERT_TYPE_MODULE_OFFLINE;
                    alert_msg.alert_timestamp = current_time_ms;
                    snprintf(alert_msg.description, sizeof(alert_msg.description), "Module %d never reported.", i + 1);
                    
                    if (alert_queue != NULL) {
                        if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                            ESP_LOGE(TAG_WATCHDOG, "Failed to send MODULE_OFFLINE alert (never reported) for module %d.", i + 1);
                        } else {
                             ESP_LOGI(TAG_WATCHDOG, "MODULE_OFFLINE alert (never reported) for module %d sent.", i + 1);
                        }
                    }
                    module_offline_alerted[i] = true;
                }
            } else if ((current_time_ms - last_received_timestamp_ms[i]) > (SLAVE_MODULE_TIMEOUT_S * 1000)) {
                // Module has sent data before, but not recently
                if (!module_offline_alerted[i]) {
                    ESP_LOGW(TAG_WATCHDOG, "Module %d timed out. Last seen %u ms ago.", i + 1, current_time_ms - last_received_timestamp_ms[i]);
                    
                    AlertMessage alert_msg;
                    alert_msg.type = ALERT_TYPE_MODULE_OFFLINE;
                    alert_msg.alert_timestamp = current_time_ms;
                    snprintf(alert_msg.description, sizeof(alert_msg.description), "Module %d offline. Last seen %u ms ago.", i + 1, current_time_ms - last_received_timestamp_ms[i]);
                    
                    if (alert_queue != NULL) {
                         if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                            ESP_LOGE(TAG_WATCHDOG, "Failed to send MODULE_OFFLINE alert for module %d.", i + 1);
                        } else {
                            ESP_LOGI(TAG_WATCHDOG, "MODULE_OFFLINE alert for module %d sent.", i + 1);
                        }
                    }
                    module_offline_alerted[i] = true;
                }
            } 
            // The case where module_offline_alerted[i] is true and the module comes back online
            // is handled in FusionEngine_task.
        }
    }
}
