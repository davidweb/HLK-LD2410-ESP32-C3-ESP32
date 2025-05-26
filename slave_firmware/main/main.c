#include <stdio.h>
#include <string.h> // For strcpy, snprintf, strcmp
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h" // For EventGroupHandle_t
#include "esp_log.h"
#include "driver/uart.h" // For UART driver
#include "esp_system.h"  // For esp_log_timestamp
#include "nvs_flash.h"   // For nvs_flash_init
#include "esp_wifi.h"    // For Wi-Fi
#include "esp_event.h"   // For event loop
#include "esp_netif.h"   // For TCP/IP stack
#include "mqtt_client.h" // For MQTT

static const char *TAG_MAIN = "slave_main";
static const char *TAG_RADAR = "RadarTask";
static const char *TAG_WIFI = "WiFiTask";

// Wi-Fi Configuration
#define EXAMPLE_ESP_WIFI_SSID      "your_wifi_ssid"
#define EXAMPLE_ESP_WIFI_PASS      "your_wifi_password"
#define EXAMPLE_ESP_MAXIMUM_RETRY  5 // Max retries for Wi-Fi connection

// MQTT Configuration
#define CONFIG_BROKER_URL          "mqtt://192.168.1.100" // Fictitious IP for now
#define MQTT_TOPIC_RADAR_DATA      "home/room1/radar1"    // Example MQTT topic
#define MQTT_CLIENT_ID             "esp32c3_slave_radar_1" // Unique client ID

// Event Group for Wi-Fi connection status
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// MQTT Client Handle
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected_flag = false;


// UART Configuration for Radar Module (already defined from previous step)
#define RADAR_UART_NUM      (UART_NUM_1)
#define RADAR_UART_BAUDRATE 256000
#define RADAR_TXD_PIN       (GPIO_NUM_21) 
#define RADAR_RXD_PIN       (GPIO_NUM_20) 
#define RADAR_UART_BUF_SIZE (1024)

// Radar Task Timing (already defined)
#define RADAR_PRE_READ_DELAY_MS  10
#define RADAR_POST_READ_DELAY_MS 75 

// Module ID for this slave device (already defined)
#define RADAR_MODULE_ID 1

// Function Declarations (Radar Task - from previous step)
static void radar_uart_init();
static bool radar_read_data(float* distance_m, char* posture, int* signal_strength);
static void format_radar_json(char* json_buffer, size_t buffer_size, int module_id, uint32_t timestamp, float distance_m, const char* posture, int signal_strength);

// Function Declarations (Wi-Fi and MQTT)
static void nvs_init();
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void wifi_init_sta(void);
static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data);
static esp_mqtt_client_handle_t mqtt_app_start(void);
static void mqtt_publish_data(esp_mqtt_client_handle_t client, const char* topic, const char* data);

// Task function declarations
void RadarTask_task(void *pvParameters);
void WiFiTask_task(void *pvParameters);


void app_main(void)
{
    ESP_LOGI(TAG_MAIN, "Starting Slave Firmware application (ESP32-C3)");
    
    // Initialize NVS - required for Wi-Fi
    nvs_init();

    // Create tasks
    xTaskCreate(&RadarTask_task, "RadarTask_task", 4096, NULL, 5, NULL);
    xTaskCreate(&WiFiTask_task, "WiFiTask_task", 4096*2, NULL, 5, NULL); // Increased stack for WiFi/MQTT

    ESP_LOGI(TAG_MAIN, "All tasks created.");
}

static void nvs_init() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
      ESP_ERROR_CHECK(nvs_flash_erase());
      ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG_WIFI, "NVS flash initialized successfully.");
}

static int s_retry_num = 0; // Wi-Fi connection retry counter

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG_WIFI, "Wi-Fi STA Started, attempting to connect...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_CONNECTED) {
        ESP_LOGI(TAG_WIFI, "Wi-Fi STA Connected to AP.");
        s_retry_num = 0; // Reset retry counter on successful connection
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (s_retry_num < EXAMPLE_ESP_MAXIMUM_RETRY) {
            esp_wifi_connect();
            s_retry_num++;
            ESP_LOGI(TAG_WIFI, "Retry Wi-Fi connection (%d/%d)...", s_retry_num, EXAMPLE_ESP_MAXIMUM_RETRY);
        } else {
            xEventGroupSetBits(s_wifi_event_group, WIFI_FAIL_BIT);
            ESP_LOGE(TAG_WIFI, "Failed to connect to Wi-Fi after %d retries.", EXAMPLE_ESP_MAXIMUM_RETRY);
        }
        ESP_LOGE(TAG_WIFI,"Connect to the AP fail");
        mqtt_connected_flag = false; // MQTT is disconnected if Wi-Fi is
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG_WIFI, "Got IP:" IPSTR, IP2STR(&event->ip_info.ip));
        s_retry_num = 0;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = EXAMPLE_ESP_WIFI_SSID,
            .password = EXAMPLE_ESP_WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK, // Or other appropriate auth mode
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA) );
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config) );
    ESP_ERROR_CHECK(esp_wifi_start() );

    ESP_LOGI(TAG_WIFI, "wifi_init_sta finished. Waiting for connection...");

    // --- SIMULATION FOR SANDBOX ---
    // In a real environment, we would wait for WIFI_CONNECTED_BIT indefinitely or with a timeout.
    // Since Wi-Fi connection will likely fail in the sandbox, we simulate success after a delay.
    #ifdef CONFIG_SIMULATE_WIFI_CONNECTION // This would be a Kconfig option
    ESP_LOGW(TAG_WIFI, "SIMULATION: Forcing Wi-Fi connected bit after 5 seconds.");
    vTaskDelay(pdMS_TO_TICKS(5000));
    xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    #endif
    // Note: The actual connection attempt `esp_wifi_connect()` is called by the event handler upon WIFI_EVENT_STA_START.
}


static void mqtt_event_handler(void* handler_args, esp_event_base_t base, int32_t event_id, void* event_data) {
    ESP_LOGD(TAG_WIFI, "MQTT Event dispatched from event loop base=%s, event_id=%ld", base, event_id);
    esp_mqtt_event_handle_t event = event_data;
    // esp_mqtt_client_handle_t client = event->client; // Client handle is in event structure
    // int msg_id; // For publish events

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_CONNECTED");
        mqtt_connected_flag = true;
        // Example: Subscribe to a topic upon connection
        // msg_id = esp_mqtt_client_subscribe(client, "/topic/qos0", 0);
        // ESP_LOGI(TAG_WIFI, "sent subscribe successful, msg_id=%d", msg_id);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_DISCONNECTED");
        mqtt_connected_flag = false;
        break;
    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_UNSUBSCRIBED:
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_DATA: // Received data on a subscribed topic
        ESP_LOGI(TAG_WIFI, "MQTT_EVENT_DATA");
        printf("TOPIC=%.*s\r\n", event->topic_len, event->topic);
        printf("DATA=%.*s\r\n", event->data_len, event->data);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG_WIFI, "MQTT_EVENT_ERROR");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            // Log specific transport errors if needed
            // ESP_LOGI(TAG_WIFI, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            // ESP_LOGI(TAG_WIFI, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG_WIFI, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG_WIFI, "Other MQTT error type: %d", event->error_handle->error_type);
        }
        break;
    default:
        ESP_LOGI(TAG_WIFI, "Other MQTT event id:%d", event->event_id);
        break;
    }
}

static esp_mqtt_client_handle_t mqtt_app_start(void) {
    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = CONFIG_BROKER_URL,
        .credentials.client_id = MQTT_CLIENT_ID,
        // .session.last_will.topic = "/topic/will", // Example last will
        // .session.last_will.msg = "I am gone",
        // .session.last_will.qos = 1,
        // .session.last_will.retain = 0,
    };

    ESP_LOGI(TAG_WIFI, "Starting MQTT client, broker: %s", CONFIG_BROKER_URL);
    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    if (client == NULL) {
        ESP_LOGE(TAG_WIFI, "Failed to initialize MQTT client");
        return NULL;
    }
    ESP_ERROR_CHECK(esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL));
    ESP_ERROR_CHECK(esp_mqtt_client_start(client));
    
    // --- SIMULATION FOR SANDBOX ---
    // In a real environment, MQTT_EVENT_CONNECTED would set mqtt_connected_flag.
    // Here, we simulate it because actual connection might fail.
    #ifdef CONFIG_SIMULATE_MQTT_CONNECTION
    ESP_LOGW(TAG_WIFI, "SIMULATION: Forcing MQTT connected flag to true after 2 seconds (if not already set by event).");
    vTaskDelay(pdMS_TO_TICKS(2000)); 
    if (!mqtt_connected_flag) { // Check if the real event already set it
         mqtt_connected_flag = true; // Simulate connection
         ESP_LOGI(TAG_WIFI, "SIMULATION: MQTT client 'connected'.");
    }
    #endif

    return client;
}

static void mqtt_publish_data(esp_mqtt_client_handle_t client, const char* topic, const char* data) {
    if (client == NULL) {
        ESP_LOGE(TAG_WIFI, "MQTT client not initialized.");
        return;
    }

    // --- SIMULATION CHECK FOR SANDBOX ---
    // In a real system, we'd rely on the MQTT_EVENT_CONNECTED event setting a flag.
    if (!mqtt_connected_flag) {
        ESP_LOGW(TAG_WIFI, "SIMULATION: MQTT client not 'connected', pretending to publish. Topic: %s, Data: %s", topic, data);
        return; // Don't attempt to publish if not connected (or simulated as not connected)
    }
    // --- END SIMULATION CHECK ---

    int msg_id = esp_mqtt_client_publish(client, topic, data, 0, 1, 0); // QoS 1, Retain 0
    if (msg_id != -1) {
        ESP_LOGI(TAG_WIFI, "Sent publish successful, msg_id=%d, topic=%s", msg_id, topic);
    } else {
        ESP_LOGE(TAG_WIFI, "Failed to publish message, topic=%s", topic);
    }
}


// Radar Task (from previous step, unchanged)
static void radar_uart_init() {
    uart_config_t uart_config = {
        .baud_rate = RADAR_UART_BAUDRATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    ESP_LOGI(TAG_RADAR, "Initializing UART for Radar on UART_NUM_%d", RADAR_UART_NUM);
    ESP_ERROR_CHECK(uart_driver_install(RADAR_UART_NUM, RADAR_UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(RADAR_UART_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(RADAR_UART_NUM, RADAR_TXD_PIN, RADAR_RXD_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    ESP_LOGI(TAG_RADAR, "UART Initialized. TX:%d, RX:%d", RADAR_TXD_PIN, RADAR_RXD_PIN);
}

static bool radar_read_data(float* distance_m, char* posture, int* signal_strength) {
    *distance_m = 2.25f;
    strcpy(posture, "LYING"); 
    *signal_strength = 72;
    return true;
}

static void format_radar_json(char* json_buffer, size_t buffer_size, int module_id, uint32_t timestamp, float distance_m, const char* posture, int signal_strength) {
    snprintf(json_buffer, buffer_size,
             "{\n"
             "  \"id_module\": %d,\n"
             "  \"timestamp\": %u,\n"
             "  \"distance_m\": %.2f,\n"
             "  \"posture\": \"%s\",\n"
             "  \"signal\": %d\n"
             "}",
             module_id, timestamp, distance_m, posture, signal_strength);
}

void RadarTask_task(void *pvParameters) {
    ESP_LOGI(TAG_RADAR, "RadarTask_task started");
    radar_uart_init();
    float distance_m;
    char posture[16]; 
    int signal_strength;
    char json_buffer[256]; 

    for(;;) {
        ESP_LOGD(TAG_RADAR, "Simulating Radar Module ON"); 
        vTaskDelay(pdMS_TO_TICKS(RADAR_PRE_READ_DELAY_MS)); 

        if (radar_read_data(&distance_m, posture, &signal_strength)) {
            uint32_t timestamp = esp_log_timestamp(); 
            format_radar_json(json_buffer, sizeof(json_buffer), RADAR_MODULE_ID, timestamp, distance_m, posture, signal_strength);
            ESP_LOGI(TAG_RADAR, "Generated JSON: \n%s", json_buffer);

            // --- TODO: Actual data passing to WiFiTask_task via queue/event ---
            // For now, WiFiTask_task will generate its own dummy data.
        } else {
            ESP_LOGW(TAG_RADAR, "Failed to read data from radar module.");
        }
        ESP_LOGD(TAG_RADAR, "Simulating Radar Module OFF/Standby");
        vTaskDelay(pdMS_TO_TICKS(RADAR_POST_READ_DELAY_MS)); 
    }
}

// WiFiTask_task Implementation
void WiFiTask_task(void *pvParameters) {
    ESP_LOGI(TAG_WIFI, "WiFiTask_task started");

    // nvs_init() is called from app_main now.
    wifi_init_sta();

    ESP_LOGI(TAG_WIFI, "Waiting for Wi-Fi connection...");
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
            WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
            pdFALSE, // Don't clear bits on exit
            pdFALSE, // Don't wait for all bits
            portMAX_DELAY); // Wait indefinitely (or use a timeout)

    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG_WIFI, "Wi-Fi Connected. Initializing MQTT client...");
        mqtt_client = mqtt_app_start();
        if (mqtt_client == NULL) {
            ESP_LOGE(TAG_WIFI, "Failed to start MQTT client. Task will not publish.");
            // Optionally, could try to restart MQTT or handle error differently
        }
    } else if (bits & WIFI_FAIL_BIT) {
        ESP_LOGE(TAG_WIFI, "Wi-Fi connection failed. Task will not initialize MQTT or publish.");
        // Task can loop here, or delete itself, or signal error.
        // For now, it will simply not proceed to MQTT part.
    } else {
        ESP_LOGE(TAG_WIFI, "Unexpected event from Wi-Fi event group.");
    }

    // Only proceed to publish loop if MQTT client was initialized (which implies Wi-Fi connected)
    if (mqtt_client) {
        char dummy_json_buffer[256]; // Buffer for dummy JSON data
        for(;;) {
            // Simulate generating or receiving radar data to send
            // In a real system, this would come from RadarTask_task via a FreeRTOS queue
            uint32_t current_timestamp = esp_log_timestamp();
            format_radar_json(dummy_json_buffer, sizeof(dummy_json_buffer), 
                              RADAR_MODULE_ID, current_timestamp, 
                              1.5f, "SITTING", 80); // Example dummy data

            ESP_LOGI(TAG_WIFI, "WiFiTask: Publishing dummy data: %s", dummy_json_buffer);
            mqtt_publish_data(mqtt_client, MQTT_TOPIC_RADAR_DATA, dummy_json_buffer);
            
            vTaskDelay(pdMS_TO_TICKS(10000)); // Publish every 10 seconds
        }
    } else {
        ESP_LOGE(TAG_WIFI, "MQTT client not available. WiFiTask will suspend itself.");
        vTaskSuspend(NULL); // Suspend itself as it cannot do its job
    }
}
