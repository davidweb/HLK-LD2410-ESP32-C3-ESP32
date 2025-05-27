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
#include "mdns.h"        // For mDNS
#include "esp_http_server.h" // For HTTP Server
#include "freertos/semphr.h" // For Mutex

// Note: cJSON.h is not included as manual parsing will be implemented.

static const char *TAG_MAIN_APP = "master_main";
static const char *TAG_HTTP_SERVER = "http_server";
static const char *TAG_MDNS_DISCOVERY = "mdns_discovery";
static const char *TAG_NETWORK = "NetworkManager";
static const char *TAG_FUSION = "FusionEngine";
static const char *TAG_FALL_DETECTOR = "FallDetector";
static const char *TAG_ALERT_MANAGER = "AlertManager";
static const char *TAG_WATCHDOG = "WatchdogTask";

// Placeholder for MQTT Broker CA Certificate
// Replace with your actual PEM-formatted CA certificate
static const char *mqtt_broker_ca_cert_pem_start = "-----BEGIN CERTIFICATE-----\n"
                                                "MIIDdzCCAl+gAwIBAgIEAgAAuTANBgkqhkiG9w0BAQUFADBaMQswCQYDVQQGEwJJ\n"
                                                // ... (truncated for brevity)
                                                "-----END CERTIFICATE-----\n";

// Wi-Fi Configuration
#define MASTER_ESP_WIFI_SSID      "your_master_wifi_ssid"
#define MASTER_ESP_WIFI_PASS      "your_master_wifi_password"
#define MASTER_ESP_MAXIMUM_RETRY  5

// MQTT Configuration
#define MASTER_CONFIG_BROKER_URL          "mqtts://192.168.1.100:8883" // Changed to mqtts and port 8883
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

// Web Server Data Structure and Mutex
typedef struct {
    bool mqtt_connected;
    bool module_status[NUM_SLAVE_MODULES]; // true for online, false for offline
    char last_alerts[5][128]; // Store last 5 alert descriptions
    int alert_write_index;    // Index for next alert to write (for circular buffer)
    int stored_alert_count;   // Number of alerts actually stored (0 to 5)
    uint32_t system_uptime_seconds; // System uptime
} WebServerData;

static WebServerData g_web_server_data;
static SemaphoreHandle_t g_web_data_mutex;
static httpd_handle_t http_server_handle = NULL; // Global handle for the server

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
void discover_radar_modules_task(void *pvParameters); // mDNS Discovery Task

// Network related function declarations
static void nvs_init();
static void master_wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void master_wifi_init_sta(void);
static bool parse_radar_json(const char* json_str, int data_len, RadarMessage* msg);
static void master_mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
static void master_mqtt_app_start(void);

// Fusion Engine related function declarations
static bool calculate_xy_position(float d1, float d2, float* x, float* y);

// HTTP Server related function declarations
static httpd_handle_t start_webserver(void);
static esp_err_t root_get_handler(httpd_req_t *req);
// No dedicated http_server_task, will start from wifi event handler
static bool http_server_started_flag = false;


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

    // Create mutex for web server data
    g_web_data_mutex = xSemaphoreCreateMutex();
    if (g_web_data_mutex == NULL) {
        ESP_LOGE(TAG_MAIN_APP, "Failed to create g_web_data_mutex. Halting.");
        while(1);
    }
    ESP_LOGI(TAG_MAIN_APP, "g_web_data_mutex created successfully.");

    // Initialize web server data (critical section)
    if(xSemaphoreTake(g_web_data_mutex, portMAX_DELAY) == pdTRUE) {
        g_web_server_data.mqtt_connected = false;
        for (int i = 0; i < NUM_SLAVE_MODULES; i++) {
            g_web_server_data.module_status[i] = false; // Initialize as offline
        }
        g_web_server_data.alert_write_index = 0;
        g_web_server_data.stored_alert_count = 0;
        for (int i = 0; i < 5; i++) {
            strcpy(g_web_server_data.last_alerts[i], ""); // Clear alert strings
        }
        g_web_server_data.system_uptime_seconds = 0;
        xSemaphoreGive(g_web_data_mutex);
    }


    // Record system start time for Watchdog initial grace period
    system_start_time_ms = esp_log_timestamp(); 

    xTaskCreate(&NetworkManager_task, "NetworkManager_task", 4096*2, NULL, NETWORK_TASK_PRIORITY, NULL);
    xTaskCreate(&FusionEngine_task, "FusionEngine_task", 4096, NULL, FUSION_TASK_PRIORITY, NULL);
    xTaskCreate(&FallDetector_task, "FallDetector_task", 4096, NULL, FALL_DETECTION_TASK_PRIORITY, NULL);
    xTaskCreate(&AlertManager_task, "AlertManager_task", 4096, NULL, ALERT_TASK_PRIORITY, NULL);
    xTaskCreate(&Watchdog_task, "Watchdog_task", 2048, NULL, WATCHDOG_TASK_PRIORITY, NULL); 
    xTaskCreate(&discover_radar_modules_task, "mdns_discover_task", 4096, NULL, 1, NULL); // Low priority for discovery
    // HTTP server will be started by NetworkManager_task upon IP acquisition

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

        // If MQTT was initialized and is not connected, try to reconnect.
        if (client_handle != NULL && !mqtt_connected_flag) {
            ESP_LOGI(TAG_NETWORK, "Wi-Fi (re)connected, attempting to reconnect MQTT client...");
            esp_err_t err = esp_mqtt_client_reconnect(client_handle);
            if (err == ESP_OK) {
                ESP_LOGI(TAG_NETWORK, "MQTT client reconnect initiated successfully.");
                // Note: MQTT_EVENT_CONNECTED will set mqtt_connected_flag = true
            } else {
                ESP_LOGE(TAG_NETWORK, "Failed to initiate MQTT client reconnect: %s. Will retry in NetworkManager_task.", esp_err_to_name(err));
                // esp_mqtt_client_start(client_handle) might be an alternative if reconnect fails persistently
            }
        }
        // Start the web server if not already started and we have a valid handle reference
        if (!http_server_started_flag && http_server_handle == NULL) {
            http_server_handle = start_webserver(); // Attempt to start the server
            if (http_server_handle != NULL) {
                http_server_started_flag = true;
                ESP_LOGI(TAG_NETWORK, "HTTP server started successfully.");
            } else {
                ESP_LOGE(TAG_NETWORK, "Failed to start HTTP server.");
                // http_server_handle will remain NULL, so it might try again on next IP_EVENT_STA_GOT_IP if Wi-Fi reconnects.
            }
        } else if (http_server_started_flag && http_server_handle != NULL) {
            ESP_LOGI(TAG_NETWORK, "HTTP server already started.");
        } else if (http_server_handle == NULL) { // This case means previous attempts failed and we should retry
             ESP_LOGW(TAG_NETWORK, "HTTP server not started, previous attempts failed. Retrying...");
             http_server_handle = start_webserver();
             if (http_server_handle != NULL) {
                http_server_started_flag = true;
                ESP_LOGI(TAG_NETWORK, "HTTP server started successfully after retry.");
            } else {
                ESP_LOGE(TAG_NETWORK, "Failed to start HTTP server on retry.");
            }
        }
    }
}


// HTTP Server Request Handler for Root Path
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    // Estimate buffer size (can be quite large for HTML)
    // Increased to 1500 to accommodate more data and styling
    buf_len = 1500; 
    buf = malloc(buf_len);
    if (!buf) {
        ESP_LOGE(TAG_HTTP_SERVER, "Failed to allocate memory for HTTP response");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char temp_buffer[256]; // For individual lines/parts

    if (xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        // Start HTML
        snprintf(buf, buf_len, "<!DOCTYPE html><html><head><title>ESP32 Master Status</title>"
                              "<meta http-equiv=\"refresh\" content=\"10\">" // Auto-refresh every 10 seconds
                              "<style>"
                              "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; color: #333; }"
                              "h1 { color: #0056b3; }"
                              "h2 { color: #0056b3; border-bottom: 1px solid #ccc; padding-bottom: 5px; }"
                              ".status-ok { color: green; font-weight: bold; }"
                              ".status-offline { color: red; font-weight: bold; }"
                              "ul { list-style-type: none; padding-left: 0; }"
                              "li { background-color: #fff; border: 1px solid #ddd; margin-bottom: 5px; padding: 10px; border-radius: 4px; }"
                              ".container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
                              "</style>"
                              "</head><body><div class=\"container\"><h1>ESP32 Master Status</h1>");

        // MQTT Status
        snprintf(temp_buffer, sizeof(temp_buffer), "<p>MQTT Status: <span class=\"%s\">%s</span></p>",
                 g_web_server_data.mqtt_connected ? "status-ok" : "status-offline",
                 g_web_server_data.mqtt_connected ? "Connected" : "Disconnected");
        strlcat(buf, temp_buffer, buf_len);

        // System Uptime
        uint32_t uptime_total_seconds = g_web_server_data.system_uptime_seconds;
        uint32_t days = uptime_total_seconds / (24 * 3600);
        uint32_t hours = (uptime_total_seconds % (24 * 3600)) / 3600;
        uint32_t minutes = (uptime_total_seconds % 3600) / 60;
        uint32_t seconds = uptime_total_seconds % 60;
        snprintf(temp_buffer, sizeof(temp_buffer), "<p>System Uptime: %u days, %02u:%02u:%02u</p>", days, hours, minutes, seconds);
        strlcat(buf, temp_buffer, buf_len);


        // Module Status
        strlcat(buf, "<h2>Module Status</h2>", buf_len);
        for (int i = 0; i < NUM_SLAVE_MODULES; i++) {
            snprintf(temp_buffer, sizeof(temp_buffer), "<p>Module %d: <span class=\"%s\">%s</span></p>",
                     i + 1,
                     g_web_server_data.module_status[i] ? "status-ok" : "status-offline",
                     g_web_server_data.module_status[i] ? "Online" : "Offline");
            strlcat(buf, temp_buffer, buf_len);
        }

        // Last Alerts
        strlcat(buf, "<h2>Last Alerts</h2><ul>", buf_len);
        if (g_web_server_data.stored_alert_count == 0) {
            strlcat(buf, "<li>No alerts yet.</li>", buf_len);
        } else {
            // Display alerts in chronological order (oldest first from circular buffer)
            for (int i = 0; i < g_web_server_data.stored_alert_count; i++) {
                // Calculate the correct index to read from the circular buffer
                int alert_idx = (g_web_server_data.alert_write_index - g_web_server_data.stored_alert_count + i + 5) % 5;
                snprintf(temp_buffer, sizeof(temp_buffer), "<li>%s</li>", g_web_server_data.last_alerts[alert_idx]);
                strlcat(buf, temp_buffer, buf_len);
            }
        }
        strlcat(buf, "</ul>", buf_len);

        xSemaphoreGive(g_web_data_mutex);
    } else {
        ESP_LOGE(TAG_HTTP_SERVER, "Failed to take g_web_data_mutex for HTTP handler");
        snprintf(buf, buf_len, "<h1>Error fetching status</h1><p>Could not access system data. Please try again.</p>");
        // No need to give mutex if not taken
    }

    // End HTML
    strlcat(buf, "</div></body></html>", buf_len);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, strlen(buf));

    free(buf); // Free the buffer
    return ESP_OK;
}

// HTTP Server URI Registration
static const httpd_uri_t root_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL 
};

// Function to start the web server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7; 
    config.lru_purge_enable = true; 

    ESP_LOGI(TAG_HTTP_SERVER, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG_HTTP_SERVER, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        return server;
    }

    ESP_LOGE(TAG_HTTP_SERVER, "Error starting server!");
    return NULL;
}

// Function to stop the webserver (optional, if needed for cleanup)
/*
static void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
*/

#define MDNS_QUERY_SERVICE_TYPE "_hlk_radar"
#define MDNS_QUERY_PROTO "_tcp"
#define MDNS_QUERY_INTERVAL_MS 30000 // Query every 30 seconds

void discover_radar_modules_task(void *pvParameters) {
    ESP_LOGI(TAG_MDNS_DISCOVERY, "mDNS discovery task started.");

    esp_err_t err = mdns_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MDNS_DISCOVERY, "mDNS Init failed: %s", esp_err_to_name(err));
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG_MDNS_DISCOVERY, "mDNS Initalized.");

    err = mdns_hostname_set("esp32-master-controller");
    if (err != ESP_OK) {
        ESP_LOGE(TAG_MDNS_DISCOVERY, "mdns_hostname_set (master) failed: %s", esp_err_to_name(err));
    } else {
        ESP_LOGI(TAG_MDNS_DISCOVERY, "mDNS hostname set to: esp32-master-controller");
    }
    // Instance name for master is not strictly necessary for discovery only
    // mdns_instance_name_set("ESP32 Master Fall Detection System");


    mdns_result_t *results = NULL;

    for (;;) {
        ESP_LOGI(TAG_MDNS_DISCOVERY, "Querying for mDNS services...");
        
        err = mdns_query_ptr(MDNS_QUERY_SERVICE_TYPE, MDNS_QUERY_PROTO, 3000, 20, &results); // 3 sec timeout, max 20 results
        if (err != ESP_OK) {
            ESP_LOGE(TAG_MDNS_DISCOVERY, "mDNS query failed: %s", esp_err_to_name(err));
            vTaskDelay(pdMS_TO_TICKS(MDNS_QUERY_INTERVAL_MS));
            continue;
        }

        if (!results) {
            ESP_LOGI(TAG_MDNS_DISCOVERY, "No mDNS services found.");
        } else {
            ESP_LOGI(TAG_MDNS_DISCOVERY, "Found %d mDNS services:", mdns_query_results_count(results)); // ESP-IDF v5+ way
            mdns_result_t *r = results;
            int i = 0;
            while(r){
                i++;
                ESP_LOGI(TAG_MDNS_DISCOVERY, "--- Service #%d ---", i);
                ESP_LOGI(TAG_MDNS_DISCOVERY, "  Instance: %s", r->instance_name ? r->instance_name : "N/A");
                ESP_LOGI(TAG_MDNS_DISCOVERY, "  Hostname: %s", r->hostname ? r->hostname : "N/A");
                ESP_LOGI(TAG_MDNS_DISCOVERY, "  Port: %u", r->port);
                
                // IP Addresses
                for (int j = 0; j < r->addr_count; j++) {
                    if (r->addr[j].addr.type == ESP_IPADDR_TYPE_V4) {
                        ESP_LOGI(TAG_MDNS_DISCOVERY, "  IPv4[%d]: " IPSTR, j, IP2STR(&(r->addr[j].addr.u_addr.ip4)));
                    }
                    #if CONFIG_LWIP_IPV6
                    else if (r->addr[j].addr.type == ESP_IPADDR_TYPE_V6) {
                        ESP_LOGI(TAG_MDNS_DISCOVERY, "  IPv6[%d]: " IPV6STR, j, IPV62STR(r->addr[j].addr.u_addr.ip6));
                    }
                    #endif
                }

                // TXT Records
                ESP_LOGI(TAG_MDNS_DISCOVERY, "  TXT Records (%d):", r->txt_count);
                for (int k = 0; k < r->txt_count; k++) {
                    ESP_LOGI(TAG_MDNS_DISCOVERY, "    %s = %s", r->txt[k].key, r->txt[k].value ? r->txt[k].value : "N/A");
                     // TODO: Here you could parse module_id, e.g. if (strcmp(r->txt[k].key, "module_id") == 0) ...
                }
                r = r->next;
            }
            mdns_query_results_free(results);
            results = NULL; // Important to reset for next query
        }
        
        vTaskDelay(pdMS_TO_TICKS(MDNS_QUERY_INTERVAL_MS));
    }
    // mdns_free(); // Should be called on deinit
    vTaskDelete(NULL); // Should not be reached in normal operation
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
        if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_web_server_data.mqtt_connected = true;
            xSemaphoreGive(g_web_data_mutex);
        } else {
            ESP_LOGE(TAG_NETWORK, "Failed to take g_web_data_mutex for MQTT connect status.");
        }
        msg_id = esp_mqtt_client_subscribe(client, HOME_MQTT_TOPIC_WILDCARD, 1); 
        ESP_LOGI(TAG_NETWORK, "Sent subscribe successful to topic %s, msg_id=%d", HOME_MQTT_TOPIC_WILDCARD, msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_NETWORK, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected_flag = false;
        if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_web_server_data.mqtt_connected = false;
            xSemaphoreGive(g_web_data_mutex);
        } else {
            ESP_LOGE(TAG_NETWORK, "Failed to take g_web_data_mutex for MQTT disconnect status.");
        }
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
                if (xQueueSend(radar_data_queue, &received_radar_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                    ESP_LOGE(TAG_NETWORK, "Failed to send radar data to radar_data_queue (queue full or error).");
                } else {
                    ESP_LOGD(TAG_NETWORK, "Radar data sent to radar_data_queue successfully.");
                }
            } else {
                 ESP_LOGE(TAG_NETWORK, "radar_data_queue is NULL. Cannot send data."); // This check is good.
            }
        } else {
            ESP_LOGE(TAG_NETWORK, "Failed to parse incoming radar JSON data. Raw: %.*s", event->data_len, event->data);
        }
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_NETWORK, "MQTT_EVENT_ERROR");
        if (event->error_handle) { // Check if error_handle is not NULL
            ESP_LOGE(TAG_NETWORK, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGE(TAG_NETWORK, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
            ESP_LOGE(TAG_NETWORK, "MQTT error type: 0x%x", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGI(TAG_NETWORK, "TCP transport error: Last error code reported from esp-tls: 0x%x, Last tls stack error number: 0x%x", 
                         event->error_handle->esp_tls_last_esp_err, event->error_handle->esp_tls_stack_err);
            } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
                ESP_LOGI(TAG_NETWORK, "Connection refused error: 0x%x (Check broker, credentials, client ID, CA cert)", event->error_handle->connect_return_code);
            } else {
                ESP_LOGW(TAG_NETWORK, "Other MQTT error type: %d", event->error_handle->error_type);
            }
        } else {
            ESP_LOGE(TAG_NETWORK, "MQTT_EVENT_ERROR, but no error handle available.");
        }
        break;
    default:
        ESP_LOGI(TAG_NETWORK, "Other MQTT event id:%d", event->event_id);
        break;
    }
}

static void master_mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MASTER_CONFIG_BROKER_URL,
        .broker.verification.certificate = mqtt_broker_ca_cert_pem_start,
        .credentials.client_id = MASTER_MQTT_CLIENT_ID,
    };

    ESP_LOGI(TAG_NETWORK, "Attempting to start MQTT client, broker URI: %s", mqtt_cfg.broker.address.uri);
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

            // If Wi-Fi is connected, MQTT client is initialized, but MQTT is not connected, try to start/reconnect.
            if ((xEventGroupGetBits(wifi_event_group) & WIFI_CONNECTED_BIT) && client_handle != NULL && !mqtt_connected_flag) {
                ESP_LOGI(TAG_NETWORK, "NetworkManager_task: Wi-Fi is connected but MQTT is not. Attempting to start/reconnect MQTT client...");
                esp_err_t err = esp_mqtt_client_start(client_handle); // Using start as it handles various states including initial start and reconnect after stop.
                if (err == ESP_OK) {
                    ESP_LOGI(TAG_NETWORK, "NetworkManager_task: MQTT client start/reconnect initiated successfully.");
                    // MQTT_EVENT_CONNECTED will set mqtt_connected_flag = true
                } else {
                    ESP_LOGE(TAG_NETWORK, "NetworkManager_task: Failed to start/reconnect MQTT client: %s.", esp_err_to_name(err));
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(30000)); // Check connection status periodically
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
        if (xQueueReceive(radar_data_queue, &current_msg, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG_FUSION, "Error receiving from radar_data_queue. This should not happen with portMAX_DELAY unless queue is deleted.");
            // If this happens, it might indicate a severe issue.
            // Add a small delay here to prevent a tight loop if the queue is somehow problematic.
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; // Skip the rest of the loop iteration
        }
        
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
                    if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        if (current_msg.module_id -1 < NUM_SLAVE_MODULES) { // Bounds check
                           g_web_server_data.module_status[current_msg.module_id - 1] = true;
                        }
                        xSemaphoreGive(g_web_data_mutex);
                    } else {
                        ESP_LOGE(TAG_FUSION, "Failed to take g_web_data_mutex for module online status.");
                    }
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
                        if (xQueueSend(fusion_output_queue, &fused_output_data, pdMS_TO_TICKS(100)) != pdPASS) {
                            ESP_LOGE(TAG_FUSION, "Failed to send fused data to fusion_output_queue (queue full or error).");
                        } else {
                            ESP_LOGD(TAG_FUSION, "Fused data sent to fusion_output_queue.");
                        }
                    } else {
                        ESP_LOGE(TAG_FUSION, "fusion_output_queue is NULL."); // This check is good.
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
        if (xQueueReceive(fusion_output_queue, &current_data, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG_FALL_DETECTOR, "Error receiving from fusion_output_queue. This should not happen with portMAX_DELAY unless queue is deleted.");
            // Add a small delay here to prevent a tight loop if the queue is somehow problematic.
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; // Skip the rest of the loop iteration
        }
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
                            if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                                ESP_LOGE(TAG_FALL_DETECTOR, "Failed to send fall alert to alert_queue (queue full or error).");
                            } else {
                                ESP_LOGI(TAG_FALL_DETECTOR, "Fall alert sent to alert_queue.");
                            }
                        } else {
                            ESP_LOGE(TAG_FALL_DETECTOR, "alert_queue is NULL!"); // This check is good.
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
        if (xQueueReceive(alert_queue, &received_alert, portMAX_DELAY) != pdPASS) {
            ESP_LOGE(TAG_ALERT_MANAGER, "Error receiving from alert_queue. This should not happen with portMAX_DELAY unless queue is deleted.");
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue; 
        }
        ESP_LOGI(TAG_ALERT_MANAGER, "Received alert. Type: %d, Description: %s, Timestamp: %u",
                 received_alert.type, received_alert.description, received_alert.alert_timestamp);

        // Update web server data with the new alert
        if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Use alert_write_index for circular buffer
            strncpy(g_web_server_data.last_alerts[g_web_server_data.alert_write_index], 
                    received_alert.description, 
                    sizeof(g_web_server_data.last_alerts[0])-1);
            g_web_server_data.last_alerts[g_web_server_data.alert_write_index][sizeof(g_web_server_data.last_alerts[0])-1] = '\\0'; // Ensure null termination
            
            g_web_server_data.alert_write_index = (g_web_server_data.alert_write_index + 1) % 5;
            if (g_web_server_data.stored_alert_count < 5) {
                g_web_server_data.stored_alert_count++;
            }
            xSemaphoreGive(g_web_data_mutex);
        } else {
            ESP_LOGE(TAG_ALERT_MANAGER, "Failed to take g_web_data_mutex for updating alerts.");
        }

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


// HTTP Server Request Handler for Root Path
static esp_err_t root_get_handler(httpd_req_t *req)
{
    char*  buf;
    size_t buf_len;

    // Estimate buffer size (can be quite large for HTML)
    // This is a rough estimate, adjust as needed, or use dynamic allocation carefully
    buf_len = 1024; 
    buf = malloc(buf_len);
    if (!buf) {
        ESP_LOGE(TAG_HTTP_SERVER, "Failed to allocate memory for HTTP response");
        httpd_resp_send_500(req);
        return ESP_FAIL;
    }

    char temp_buffer[256]; // For individual lines/parts

    if (xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(500)) == pdTRUE) {
        // Start HTML
        snprintf(buf, buf_len, "<!DOCTYPE html><html><head><title>ESP32 Master Status</title>"
                              "<meta http-equiv=\"refresh\" content=\"10\">" // Auto-refresh every 10 seconds
                              "<style>"
                              "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f4f4f4; color: #333; }"
                              "h1 { color: #0056b3; }"
                              "h2 { color: #0056b3; border-bottom: 1px solid #ccc; padding-bottom: 5px; }"
                              ".status-ok { color: green; }"
                              ".status-offline { color: red; }"
                              "ul { list-style-type: none; padding-left: 0; }"
                              "li { background-color: #fff; border: 1px solid #ddd; margin-bottom: 5px; padding: 10px; border-radius: 4px; }"
                              ".container { background-color: #fff; padding: 20px; border-radius: 8px; box-shadow: 0 0 10px rgba(0,0,0,0.1); }"
                              "</style>"
                              "</head><body><div class=\"container\"><h1>ESP32 Master Status</h1>");

        // MQTT Status
        snprintf(temp_buffer, sizeof(temp_buffer), "<p>MQTT Status: <span class=\"%s\">%s</span></p>",
                 g_web_server_data.mqtt_connected ? "status-ok" : "status-offline",
                 g_web_server_data.mqtt_connected ? "Connected" : "Disconnected");
        strlcat(buf, temp_buffer, buf_len);

        // System Uptime
        uint32_t uptime = g_web_server_data.system_uptime_seconds;
        uint32_t days = uptime / (24 * 3600);
        uptime = uptime % (24 * 3600);
        uint32_t hours = uptime / 3600;
        uptime = uptime % 3600;
        uint32_t minutes = uptime / 60;
        uint32_t seconds = uptime % 60;
        snprintf(temp_buffer, sizeof(temp_buffer), "<p>System Uptime: %u days, %02u:%02u:%02u</p>", days, hours, minutes, seconds);
        strlcat(buf, temp_buffer, buf_len);


        // Module Status
        strlcat(buf, "<h2>Module Status</h2>", buf_len);
        for (int i = 0; i < NUM_SLAVE_MODULES; i++) {
            snprintf(temp_buffer, sizeof(temp_buffer), "<p>Module %d: <span class=\"%s\">%s</span></p>",
                     i + 1,
                     g_web_server_data.module_status[i] ? "status-ok" : "status-offline",
                     g_web_server_data.module_status[i] ? "Online" : "Offline");
            strlcat(buf, temp_buffer, buf_len);
        }

        // Last Alerts
        strlcat(buf, "<h2>Last Alerts</h2><ul>", buf_len);
        if (g_web_server_data.stored_alert_count == 0) {
            strlcat(buf, "<li>No alerts yet.</li>", buf_len);
        } else {
            // Display alerts in chronological order (oldest first from circular buffer)
            for (int i = 0; i < g_web_server_data.stored_alert_count; i++) {
                int alert_idx = (g_web_server_data.alert_write_index - g_web_server_data.stored_alert_count + i + 5) % 5;
                snprintf(temp_buffer, sizeof(temp_buffer), "<li>%s</li>", g_web_server_data.last_alerts[alert_idx]);
                strlcat(buf, temp_buffer, buf_len);
            }
        }
        strlcat(buf, "</ul>", buf_len);

        xSemaphoreGive(g_web_data_mutex);
    } else {
        ESP_LOGE(TAG_HTTP_SERVER, "Failed to take g_web_data_mutex for HTTP handler");
        snprintf(buf, buf_len, "<h1>Error fetching status</h1><p>Could not access system data. Please try again.</p>");
        // No need to give mutex if not taken
    }

    // End HTML
    strlcat(buf, "</div></body></html>", buf_len);

    httpd_resp_set_type(req, "text/html");
    httpd_resp_send(req, buf, strlen(buf));

    free(buf); // Free the buffer
    return ESP_OK;
}

// HTTP Server URI Registration
static const httpd_uri_t root_uri = {
    .uri      = "/",
    .method   = HTTP_GET,
    .handler  = root_get_handler,
    .user_ctx = NULL 
};

// Function to start the web server
static httpd_handle_t start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_open_sockets = 7; // Increased a bit
    config.lru_purge_enable = true; // Enable LRU purge for inactive sockets

    ESP_LOGI(TAG_HTTP_SERVER, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        ESP_LOGI(TAG_HTTP_SERVER, "Registering URI handlers");
        httpd_register_uri_handler(server, &root_uri);
        return server;
    }

    ESP_LOGE(TAG_HTTP_SERVER, "Error starting server!");
    return NULL;
}

// Function to stop the webserver (optional, if needed for cleanup)
/*
static void stop_webserver(httpd_handle_t server)
{
    if (server) {
        httpd_stop(server);
    }
}
*/

void Watchdog_task(void *pvParameters) {
    ESP_LOGI(TAG_WATCHDOG, "Watchdog_task started");
    // system_start_time_ms is initialized in app_main before this task starts.

    for(;;) {
        vTaskDelay(pdMS_TO_TICKS(WATCHDOG_CHECK_INTERVAL_S * 1000));
        uint32_t current_time_ms = esp_log_timestamp();
        uint32_t uptime_seconds = (current_time_ms - system_start_time_ms) / 1000;

        if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            g_web_server_data.system_uptime_seconds = uptime_seconds;
            xSemaphoreGive(g_web_data_mutex);
        } else {
            ESP_LOGE(TAG_WATCHDOG, "Failed to take g_web_data_mutex for uptime update.");
        }

        for (int i = 0; i < NUM_SLAVE_MODULES; i++) {
            bool initial_grace_period_passed = (current_time_ms - system_start_time_ms > (SLAVE_MODULE_TIMEOUT_S * 1000));

            if (last_received_timestamp_ms[i] == 0) { // Module has never sent data
                if (initial_grace_period_passed && !module_offline_alerted[i]) {
                    ESP_LOGW(TAG_WATCHDOG, "Module %d has never sent data after initial timeout.", i + 1);
                    if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        g_web_server_data.module_status[i] = false;
                        xSemaphoreGive(g_web_data_mutex);
                    } else {
                        ESP_LOGE(TAG_WATCHDOG, "Failed to take g_web_data_mutex for module status (never reported).");
                    }
                    
                    AlertMessage alert_msg;
                    alert_msg.type = ALERT_TYPE_MODULE_OFFLINE;
                    alert_msg.alert_timestamp = current_time_ms;
                    snprintf(alert_msg.description, sizeof(alert_msg.description), "Module %d never reported.", i + 1);
                    
                    if (alert_queue != NULL) {
                        if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                            ESP_LOGE(TAG_WATCHDOG, "Failed to send MODULE_OFFLINE alert (never reported) for module %d to alert_queue.", i + 1);
                        } else {
                             ESP_LOGI(TAG_WATCHDOG, "MODULE_OFFLINE alert (never reported) for module %d sent to alert_queue.", i + 1);
                        }
                    } else {
                        ESP_LOGE(TAG_WATCHDOG, "alert_queue is NULL. Cannot send MODULE_OFFLINE alert (never reported) for module %d.", i + 1);
                    }
                    module_offline_alerted[i] = true; // Mark as alerted
                }
            } else if ((current_time_ms - last_received_timestamp_ms[i]) > (SLAVE_MODULE_TIMEOUT_S * 1000)) {
                // Module has sent data before, but not recently
                if (!module_offline_alerted[i]) { // Check if an offline alert for this timeout has already been sent
                    ESP_LOGW(TAG_WATCHDOG, "Module %d timed out. Last seen %u ms ago.", i + 1, current_time_ms - last_received_timestamp_ms[i]);
                     if(xSemaphoreTake(g_web_data_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
                        g_web_server_data.module_status[i] = false;
                        xSemaphoreGive(g_web_data_mutex);
                    } else {
                        ESP_LOGE(TAG_WATCHDOG, "Failed to take g_web_data_mutex for module status (timeout).");
                    }
                    
                    AlertMessage alert_msg;
                    alert_msg.type = ALERT_TYPE_MODULE_OFFLINE;
                    alert_msg.alert_timestamp = current_time_ms;
                    snprintf(alert_msg.description, sizeof(alert_msg.description), "Module %d offline. Last seen %u ms ago.", i + 1, current_time_ms - last_received_timestamp_ms[i]);
                    
                    if (alert_queue != NULL) {
                         if (xQueueSend(alert_queue, &alert_msg, pdMS_TO_TICKS(100)) != pdPASS) {
                            ESP_LOGE(TAG_WATCHDOG, "Failed to send MODULE_OFFLINE alert (timeout) for module %d to alert_queue.", i + 1);
                        } else {
                            ESP_LOGI(TAG_WATCHDOG, "MODULE_OFFLINE alert (timeout) for module %d sent to alert_queue.", i + 1);
                        }
                    } else {
                        ESP_LOGE(TAG_WATCHDOG, "alert_queue is NULL. Cannot send MODULE_OFFLINE alert (timeout) for module %d.", i + 1);
                    }
                    module_offline_alerted[i] = true; // Mark as alerted
                }
            } 
            // The case where module_offline_alerted[i] is true and the module comes back online
            // is handled in FusionEngine_task.
        }
    }
}
