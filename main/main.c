#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_netif.h"
#include "driver/gpio.h"
#include "esp_http_server.h"
#include "lwip/err.h"
#include "lwip/sys.h"
#include "esp_rom_sys.h"
#include "mqtt_client.h"

static const char *TAG = "environmental_conditions_monitor";

// Pin definitions
#define STATUS_LED_PIN GPIO_NUM_2
#define DHT11_PIN GPIO_NUM_18

// WiFi credentials -- Edit these with your actual WiFi network details.
#define WIFI_SSID_1 ""
#define WIFI_PASS_1 ""
#define WIFI_SSID_2 ""
#define WIFI_PASS_2 ""

#define AP_SSID "Fallback_Hotspot"
#define AP_PASS "llnDapo0emZw"

// MQTT broker (adjust URI as needed)
#define MQTT_BROKER_URI         "mqtt://192.168.1.10"

// HA discovery prefix
#define HA_DISCOVERY_PREFIX     "homeassistant"

// Unique IDs for sensors
#define HA_TEMP_UNIQUE_ID       "temperature"
#define HA_HUM_UNIQUE_ID        "humidity"

// Discovery topics
#define HA_TEMP_CONFIG_TOPIC    HA_DISCOVERY_PREFIX "/sensor/" HA_TEMP_UNIQUE_ID "/config"
#define HA_HUM_CONFIG_TOPIC     HA_DISCOVERY_PREFIX "/sensor/" HA_HUM_UNIQUE_ID  "/config"

// State topics (where we publish readings)
#define MQTT_TEMP_STATE_TOPIC   "temperature/state"
#define MQTT_HUM_STATE_TOPIC    "humidity/state"


// Global variables
static bool led_error_state = false;
static bool wifi_connected = false;
static float room_temp = 0.0;
static float room_humidity = 0.0;
static bool sensor_connectivity = false;

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

// Function prototypes
static void wifi_init_sta(void);
static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void dht11_task(void *pvParameters);
static void led_blink_task(void *pvParameters);
static void sensor_check_task(void *pvParameters);
static bool read_dht11(float *temperature, float *humidity);
static void configure_gpio(void);
static esp_err_t temp_handler(httpd_req_t *req);
static esp_err_t humidity_handler(httpd_req_t *req);
static esp_err_t status_handler(httpd_req_t *req);
static void start_webserver(void);
static void start_mqtt(void);
static void publish_ha_discovery(void);

static esp_mqtt_client_handle_t mqtt_client = NULL;

// HTTP server handlers
static esp_err_t temp_handler(httpd_req_t *req)
{
    char response[100];
    snprintf(response, sizeof(response), "{\"temperature\": %.2f}", room_temp);
    
    ESP_LOGI(TAG, "HTTP Request: GET /temperature");
    ESP_LOGI(TAG, "Response Data: %s", response);
    ESP_LOGI(TAG, "Data Packet Size: %d bytes", strlen(response));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static esp_err_t humidity_handler(httpd_req_t *req)
{
    char response[100];
    snprintf(response, sizeof(response), "{\"humidity\": %.2f}", room_humidity);
    
    ESP_LOGI(TAG, "HTTP Request: GET /humidity");
    ESP_LOGI(TAG, "Response Data: %s", response);
    ESP_LOGI(TAG, "Data Packet Size: %d bytes", strlen(response));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static esp_err_t status_handler(httpd_req_t *req)
{
    char response[200];
    snprintf(response, sizeof(response), 
        "{\"temperature\": %.2f, \"humidity\": %.2f, \"wifi_connected\": %s, \"sensor_ok\": %s}", 
        room_temp, room_humidity, 
        wifi_connected ? "true" : "false",
        sensor_connectivity ? "true" : "false");
    
    ESP_LOGI(TAG, "HTTP Request: GET /status");
    ESP_LOGI(TAG, "Response Data: %s", response);
    ESP_LOGI(TAG, "Data Packet Size: %d bytes", strlen(response));
    
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, response, strlen(response));
    return ESP_OK;
}

static void start_webserver(void)
{
    httpd_handle_t server = NULL;
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port = 80;

    ESP_LOGI(TAG, "Starting HTTP server on port: '%d'", config.server_port);
    if (httpd_start(&server, &config) == ESP_OK) {
        httpd_uri_t temp_uri = {
            .uri       = "/temperature",
            .method    = HTTP_GET,
            .handler   = temp_handler,
            .user_ctx  = NULL
        };
        
        httpd_uri_t humidity_uri = {
            .uri       = "/humidity",
            .method    = HTTP_GET,
            .handler   = humidity_handler,
            .user_ctx  = NULL
        };
        
        httpd_uri_t status_uri = {
            .uri       = "/status",
            .method    = HTTP_GET,
            .handler   = status_handler,
            .user_ctx  = NULL
        };

        httpd_register_uri_handler(server, &temp_uri);
        httpd_register_uri_handler(server, &humidity_uri);
        httpd_register_uri_handler(server, &status_uri);
    }
}

static void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        ESP_LOGI(TAG, "WiFi Station Started - Attempting connection...");
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        wifi_event_sta_disconnected_t* disconnected = (wifi_event_sta_disconnected_t*)event_data;
        ESP_LOGW(TAG, "WiFi Disconnected - Reason: %d", disconnected->reason);
        ESP_LOGI(TAG, "Network Status: DISCONNECTED");
        ESP_LOGI(TAG, "Data Transmission: PAUSED");
        wifi_connected = false;
        led_error_state = true;
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "WiFi Connected Successfully!");
        ESP_LOGI(TAG, "IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        ESP_LOGI(TAG, "Gateway: " IPSTR, IP2STR(&event->ip_info.gw));
        ESP_LOGI(TAG, "Netmask: " IPSTR, IP2STR(&event->ip_info.netmask));
        ESP_LOGI(TAG, "Network Status: CONNECTED");
        ESP_LOGI(TAG, "Data Transmission: ACTIVE");
        ESP_LOGI(TAG, "HTTP Server Available at: http://" IPSTR, IP2STR(&event->ip_info.ip));
        wifi_connected = true;
        led_error_state = false;
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
        start_mqtt();
        publish_ha_discovery();
    }
}

static void publish_ha_discovery(void)
{
    // Temperature sensor config
    const char *temp_cfg = "{"
      "\"name\": \"Temperature\","
      "\"unit_of_measurement\": \"°C\","
      "\"state_topic\": \"" MQTT_TEMP_STATE_TOPIC "\","
      "\"value_template\": \"{{ value_json.temperature }}\","
      "\"unique_id\": \"" HA_TEMP_UNIQUE_ID "\""
    "}";

    // Humidity sensor config
    const char *hum_cfg = "{"
      "\"name\": \"Humidity\","
      "\"unit_of_measurement\": \"%\","
      "\"state_topic\": \"" MQTT_HUM_STATE_TOPIC "\","
      "\"value_template\": \"{{ value_json.humidity }}\","
      "\"unique_id\": \"" HA_HUM_UNIQUE_ID "\""
    "}";

    esp_mqtt_client_publish(mqtt_client, HA_TEMP_CONFIG_TOPIC, temp_cfg, 0, 1, 1);
    vTaskDelay(pdMS_TO_TICKS(100));  // small delay
    esp_mqtt_client_publish(mqtt_client, HA_HUM_CONFIG_TOPIC, hum_cfg, 0, 1, 1);
}

static void wifi_init_sta(void)
{
    s_wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    esp_netif_create_default_wifi_ap();

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

    // Try first WiFi network
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID_1,
            .password = WIFI_PASS_1,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_APSTA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    // Setup AP mode as fallback
    wifi_config_t ap_config = {
        .ap = {
            .ssid = AP_SSID,
            .password = AP_PASS,
            .ssid_len = strlen(AP_SSID),
            .channel = 1,
            .authmode = WIFI_AUTH_WPA_WPA2_PSK,
            .max_connection = 4
        }
    };

    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi init finished.");
}

static void start_mqtt(void)
{
    esp_mqtt_client_config_t cfg = {
        .uri = MQTT_BROKER_URI,
        // .username = "...",   // if needed
        // .password = "...",
    };
    mqtt_client = esp_mqtt_client_init(&cfg);
    esp_mqtt_client_start(mqtt_client);
        // give broker time to connect
    vTaskDelay(pdMS_TO_TICKS(500));
    publish_ha_discovery();
}

static void configure_gpio(void)
{
    gpio_config_t io_conf;
    
    // Configure status LED
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << STATUS_LED_PIN);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);
    
    // Configure DHT11 pin
    io_conf.mode = GPIO_MODE_INPUT_OUTPUT_OD;
    io_conf.pin_bit_mask = (1ULL << DHT11_PIN);
    io_conf.pull_up_en = 1;
    gpio_config(&io_conf);
}

static bool read_dht11(float *temperature, float *humidity)
{
    uint8_t data[5] = {0};
    int timeout = 0;
    
    ESP_LOGD(TAG, "DHT11: Starting sensor read cycle");
    
    // Send start signal
    gpio_set_direction(DHT11_PIN, GPIO_MODE_OUTPUT);
    gpio_set_level(DHT11_PIN, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(DHT11_PIN, 1);
    esp_rom_delay_us(30);
    
    // Switch to input mode
    gpio_set_direction(DHT11_PIN, GPIO_MODE_INPUT);
    
    // Wait for DHT11 response
    timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 1 && timeout < 100) {
        esp_rom_delay_us(1);
        timeout++;
    }
    if (timeout >= 100) {
        ESP_LOGW(TAG, "DHT11: Timeout waiting for response start");
        return false;
    }
    
    timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 0 && timeout < 100) {
        esp_rom_delay_us(1);
        timeout++;
    }
    if (timeout >= 100) {
        ESP_LOGW(TAG, "DHT11: Timeout waiting for response phase 1");
        return false;
    }
    
    timeout = 0;
    while (gpio_get_level(DHT11_PIN) == 1 && timeout < 100) {
        esp_rom_delay_us(1);
        timeout++;
    }
    if (timeout >= 100) {
        ESP_LOGW(TAG, "DHT11: Timeout waiting for response phase 2");
        return false;
    }
    
    // Read 40 bits of data
    for (int i = 0; i < 40; i++) {
        // Wait for start of bit
        timeout = 0;
        while (gpio_get_level(DHT11_PIN) == 0 && timeout < 100) {
            esp_rom_delay_us(1);
            timeout++;
        }
        if (timeout >= 100) {
            ESP_LOGW(TAG, "DHT11: Timeout reading bit %d", i);
            return false;
        }
        
        // Measure pulse width
        timeout = 0;
        while (gpio_get_level(DHT11_PIN) == 1 && timeout < 100) {
            esp_rom_delay_us(1);
            timeout++;
        }
        if (timeout >= 100) {
            ESP_LOGW(TAG, "DHT11: Timeout measuring bit %d", i);
            return false;
        }
        
        // Determine bit value
        data[i / 8] <<= 1;
        if (timeout > 40) {
            data[i / 8] |= 1;
        }
    }
    
    // Log raw data bytes
    ESP_LOGD(TAG, "DHT11: Raw data bytes: [0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X]", 
             data[0], data[1], data[2], data[3], data[4]);
    
    // Verify checksum
    uint8_t checksum = data[0] + data[1] + data[2] + data[3];
    if (checksum != data[4]) {
        ESP_LOGW(TAG, "DHT11: Checksum failed! Calculated: 0x%02X, Received: 0x%02X", 
                 checksum, data[4]);
        return false;
    }
    
    // Convert to actual values
    *humidity = (float)data[0];
    *temperature = (float)data[2];
    
    ESP_LOGD(TAG, "DHT11: Checksum OK");
    ESP_LOGD(TAG, "DHT11: Parsed Temperature: %.2f°C", *temperature);
    ESP_LOGD(TAG, "DHT11: Parsed Humidity: %.2f%%", *humidity);
    
    return true;
}

static void dht11_task(void *pvParameters)
{
    float temp, hum;
    static uint32_t read_count   = 0;
    static uint32_t success_count= 0;
    static uint32_t fail_count   = 0;

    while (1) {
        read_count++;
        ESP_LOGI(TAG, "=== DHT11 Reading Cycle #%u ===", read_count);

        if (read_dht11(&temp, &hum)) {
            success_count++;
            if (!isnan(temp) && !isnan(hum)) {
                bool temp_changed = (temp != room_temp);
                bool hum_changed  = (hum  != room_humidity);

                room_temp     = temp;
                room_humidity = hum;

                ESP_LOGI(TAG, "Sensor Reading SUCCESS:");
                ESP_LOGI(TAG, "  Temperature: %.2f°C %s",
                         temp, temp_changed ? "(CHANGED)" : "(UNCHANGED)");
                ESP_LOGI(TAG, "  Humidity: %.2f%% %s",
                         hum, hum_changed ? "(CHANGED)" : "(UNCHANGED)");
                ESP_LOGI(TAG, "  Success Rate: %u/%u (%.1f%%)",
                         success_count, read_count,
                         (float)success_count/read_count * 100.0f);

                // LED blink on successful reading when Wi-Fi is connected
                if (wifi_connected && !led_error_state) {
                    ESP_LOGI(TAG, "Status LED: Quick blink (data ready)");
                    gpio_set_level(STATUS_LED_PIN, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_set_level(STATUS_LED_PIN, 0);

                    // Publish to MQTT state topics
                    if (mqtt_client) {
                        char payload[64];
                        int len;

                        // temperature
                        len = snprintf(payload, sizeof(payload),
                                       "{\"temperature\": %.2f}", room_temp);
                        esp_mqtt_client_publish(mqtt_client,
                                               MQTT_TEMP_STATE_TOPIC,
                                               payload, len, 1, 0);

                        // humidity
                        len = snprintf(payload, sizeof(payload),
                                       "{\"humidity\": %.2f}", room_humidity);
                        esp_mqtt_client_publish(mqtt_client,
                                               MQTT_HUM_STATE_TOPIC,
                                               payload, len, 1, 0);
                    }
                }
            } else {
                ESP_LOGW(TAG, "Sensor data contains NaN values");
                room_temp     = 0.0f;
                room_humidity = 0.0f;
                fail_count++;
            }
        } else {
            fail_count++;
            ESP_LOGW(TAG, "Sensor Reading FAILED:");
            ESP_LOGW(TAG, "  Failed attempts: %u/%u (%.1f%%)",
                     fail_count, read_count,
                     (float)fail_count/read_count * 100.0f);
            room_temp     = 0.0f;
            room_humidity = 0.0f;
        }

        // Log current data state
        ESP_LOGI(TAG, "Current Data State:");
        ESP_LOGI(TAG, "  Temperature: %.2f°C", room_temp);
        ESP_LOGI(TAG, "  Humidity: %.2f%%", room_humidity);
        ESP_LOGI(TAG, "  Wi-Fi Status: %s", wifi_connected ? "CONNECTED" : "DISCONNECTED");
        ESP_LOGI(TAG, "  Data Available for HTTP: %s",
                 (room_temp != 0.0f || room_humidity != 0.0f) ? "YES" : "NO");

        vTaskDelay(pdMS_TO_TICKS(3000)); // Update every 3 seconds
    }
}

static void sensor_check_task(void *pvParameters)
{
    static bool last_sensor_state = false;
    
    while (1) {
        // Check sensor connectivity
        sensor_connectivity = !(room_temp == 0.0 && room_humidity == 0.0);
        
        // Log state changes
        if (sensor_connectivity != last_sensor_state) {
            ESP_LOGI(TAG, "=== SENSOR CONNECTIVITY CHANGED ===");
            ESP_LOGI(TAG, "Sensor Status: %s", sensor_connectivity ? "ONLINE" : "OFFLINE");
            if (!sensor_connectivity) {
                ESP_LOGW(TAG, "Sensor appears to be offline - no valid data received");
                led_error_state = true;
            }
            last_sensor_state = sensor_connectivity;
        }
        
        // Periodic status report
        static int status_counter = 0;
        if (++status_counter >= 10) { // Every 10 seconds
            status_counter = 0;
            ESP_LOGI(TAG, "=== SYSTEM STATUS REPORT ===");
            ESP_LOGI(TAG, "WiFi: %s", wifi_connected ? "CONNECTED" : "DISCONNECTED");
            ESP_LOGI(TAG, "Sensor: %s", sensor_connectivity ? "ONLINE" : "OFFLINE");
            ESP_LOGI(TAG, "LED Error State: %s", led_error_state ? "ERROR" : "NORMAL");
            ESP_LOGI(TAG, "Data Values: T=%.2f°C, H=%.2f%%", room_temp, room_humidity);
            ESP_LOGI(TAG, "Free Heap: %d bytes", esp_get_free_heap_size());
        }
        
        vTaskDelay(pdMS_TO_TICKS(1000)); // Check every second
    }
}

static void led_blink_task(void *pvParameters)
{
    TickType_t last_blink = 0;
    bool led_state = false;
    static bool last_error_state = false;
    
    while (1) {
        TickType_t now = xTaskGetTickCount();
        
        // Log LED state changes
        if (led_error_state != last_error_state) {
            ESP_LOGI(TAG, "LED Status Changed: %s", led_error_state ? "ERROR BLINK MODE" : "NORMAL MODE");
            last_error_state = led_error_state;
        }
        
        if (led_error_state) {
            // Fast blink when there's an error
            if ((now - last_blink) >= pdMS_TO_TICKS(75)) {
                led_state = !led_state;
                gpio_set_level(STATUS_LED_PIN, led_state);
                last_blink = now;
            }
        } else {
            // Turn off LED if it's on and enough time has passed
            if (led_state && (now - last_blink) >= pdMS_TO_TICKS(200)) {
                gpio_set_level(STATUS_LED_PIN, 0);
                led_state = false;
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_LOGI(TAG, "ESP_WIFI_MODE_STA");
    
    // Configure GPIO
    configure_gpio();
    
    // Initialize WiFi
    wifi_init_sta();
    
    // Start web server
    start_webserver();
    
    // Create tasks
    xTaskCreate(dht11_task, "dht11_task", 4096, NULL, 5, NULL);
    xTaskCreate(led_blink_task, "led_blink_task", 2048, NULL, 3, NULL);
    xTaskCreate(sensor_check_task, "sensor_check_task", 2048, NULL, 4, NULL);
    
    ESP_LOGI(TAG, "Office Temperature Monitor Started");
}