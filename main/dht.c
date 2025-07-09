/*
    * DHT Sensor Reading for ESP-IDF
    *
    * This code provides functionality to read temperature and humidity data from DHT11 or DHT22 sensors
    * using the ESP-IDF framework. It includes error handling, logging, and supports both integer and float
    * data formats.
    * 
*/

#include "dht.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "rom/ets_sys.h"
#include "driver/gpio.h"

static const char* TAG = "DHT";

#define DHT_TIMEOUT_US 85
#define DHT_TIMER_INTERVAL 2
#define DHT_DATA_BITS 40

// Function to wait for a specific pin state with a timeout
// Returns 1 if the expected state is reached within the timeout, 0 otherwise

static int dht_await_pin_state(gpio_num_t pin, uint32_t timeout, bool expected_pin_state, uint32_t *duration)
{
    for (uint32_t i = 0; i < timeout; i += DHT_TIMER_INTERVAL) {
        if (gpio_get_level(pin) == expected_pin_state) {
            if (duration) {
                *duration = i;
            }
            return 1;
        }
        ets_delay_us(DHT_TIMER_INTERVAL);
    }
    return 0;
}

// Function to fetch data from the DHT sensor.
// It pulls the pin low to initiate the read sequence, then reads the data bits.

static esp_err_t dht_fetch_data(gpio_num_t pin, uint8_t data[5])
{
    uint32_t low_duration;
    uint32_t high_duration;

    // Phase 'A' pulling signal low to start the read sequence.
    gpio_set_direction(pin, GPIO_MODE_OUTPUT_OD);
    gpio_set_level(pin, 0);
    ets_delay_us(20 * 1000);
    gpio_set_level(pin, 1);
    ets_delay_us(40);
    gpio_set_direction(pin, GPIO_MODE_INPUT);

    // Step through Phase 'B', 40us low signal from sensor.
    if (!dht_await_pin_state(pin, DHT_TIMEOUT_US, 0, NULL)) {
        ESP_LOGE(TAG, "Timeout waiting for start signal low");
        return ESP_ERR_TIMEOUT;
    }

    // Step through Phase 'C', 80us high signal from sensor.
    if (!dht_await_pin_state(pin, DHT_TIMEOUT_US, 1, NULL)) {
        ESP_LOGE(TAG, "Timeout waiting for start signal high");
        return ESP_ERR_TIMEOUT;
    }

    // Step through Phase 'D', 80us low signal from sensor.
    if (!dht_await_pin_state(pin, DHT_TIMEOUT_US, 0, NULL)) {
        ESP_LOGE(TAG, "Timeout waiting for data start");
        return ESP_ERR_TIMEOUT;
    }

    // Read in each of the 40 bits of data.
    for (int i = 0; i < DHT_DATA_BITS; i++) {
        if (!dht_await_pin_state(pin, DHT_TIMEOUT_US, 1, &low_duration)) {
            ESP_LOGE(TAG, "Timeout waiting for data bit %d high", i);
            return ESP_ERR_TIMEOUT;
        }
        if (!dht_await_pin_state(pin, DHT_TIMEOUT_US, 0, &high_duration)) {
            ESP_LOGE(TAG, "Timeout waiting for data bit %d low", i);
            return ESP_ERR_TIMEOUT;
        }

        uint8_t b = i / 8;
        uint8_t m = i % 8;
        if (!m) {
            data[b] = 0;
        }

        data[b] |= (high_duration > low_duration) << (7 - m);
    }

    return ESP_OK;
}

// Function to read data from the DHT sensor.
// It verifies the checksum and parses the humidity and temperature values.

esp_err_t dht_read_data(dht_sensor_type_t sensor_type, gpio_num_t pin, int16_t *humidity, int16_t *temperature)
{
    uint8_t data[5] = {0};
    esp_err_t result = dht_fetch_data(pin, data);
    
    if (result != ESP_OK) {
        return result;
    }

    // Verify checksum.
    if (data[4] != ((data[0] + data[1] + data[2] + data[3]) & 0xFF)) {
        ESP_LOGE(TAG, "Checksum failed, data may be corrupted");
        return ESP_ERR_INVALID_CRC;
    }

    // Parse data based on sensor type.
    if (sensor_type == DHT_TYPE_DHT11) {
        *humidity = data[0] * 10;
        *temperature = data[2] * 10;
    } else {    // For DHT22, AM2301.
        *humidity = ((data[0] << 8) | data[1]);
        *temperature = ((data[2] << 8) | data[3]);
        if (*temperature & 0x8000) {
            *temperature = -(*temperature & 0x7FFF);
        }
    }

    ESP_LOGD(TAG, "Raw data: %02x %02x %02x %02x %02x", data[0], data[1], data[2], data[3], data[4]);
    ESP_LOGD(TAG, "Humidity: %d.%d%%, Temperature: %d.%d°C", 
             *humidity / 10, *humidity % 10, *temperature / 10, *temperature % 10);

    return ESP_OK;
}

// Function to read temperature and humidity as float values.

esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin, float *humidity, float *temperature)
{
    int16_t h, t;
    esp_err_t result = dht_read_data(sensor_type, pin, &h, &t);
    
    if (result == ESP_OK) {
        *humidity = h / 10.0f;
        *temperature = t / 10.0f;
    }
    
    return result;
}