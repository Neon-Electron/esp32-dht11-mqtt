#ifndef DHT_H
#define DHT_H

#include "esp_err.h"
#include "driver/gpio.h"

#ifdef __cplusplus
extern "C" {
#endif

// DHT sensor types
typedef enum {
    DHT_TYPE_DHT11,
    DHT_TYPE_DHT22,
    DHT_TYPE_AM2301
} dht_sensor_type_t;

/**
 * @brief Read data from DHT sensor
 * 
 * @param sensor_type Type of DHT sensor
 * @param pin GPIO pin connected to DHT sensor
 * @param humidity Pointer to store humidity value (in 0.1%)
 * @param temperature Pointer to store temperature value (in 0.1°C)
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t dht_read_data(dht_sensor_type_t sensor_type, gpio_num_t pin, int16_t *humidity, int16_t *temperature);

/**
 * @brief Read temperature and humidity as float values
 * 
 * @param sensor_type Type of DHT sensor
 * @param pin GPIO pin connected to DHT sensor
 * @param humidity Pointer to store humidity value (in %)
 * @param temperature Pointer to store temperature value (in °C)
 * @return ESP_OK on success, ESP_ERR_* on failure
 */
esp_err_t dht_read_float_data(dht_sensor_type_t sensor_type, gpio_num_t pin, float *humidity, float *temperature);

#ifdef __cplusplus
}
#endif

#endif // DHT_H