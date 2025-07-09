# ESP32 DHT11 MQTT Environmental Monitor

An ESP32-based environmental monitoring system that reads temperature and humidity data from a DHT11 sensor and provides multiple ways to access the data including HTTP endpoints, MQTT publishing, and Home Assistant integration.

## Features

- **DHT11 Sensor Integration**: Reads temperature and humidity data every 3 seconds
- **WiFi Connectivity**: Supports multiple WiFi networks with automatic fallback to AP mode
- **HTTP Web Server**: RESTful API endpoints for real-time data access
- **MQTT Publishing**: Publishes sensor data to MQTT broker with Home Assistant auto-discovery
- **Status LED**: Visual feedback for system status and errors
- **Comprehensive Logging**: Detailed logging for debugging and monitoring
- **Error Handling**: Robust error handling with automatic recovery mechanisms
- **Dual Mode Operation**: Station mode for normal operation, AP mode for fallback access

## Hardware Requirements

- ESP32 development board
- DHT11 temperature and humidity sensor
- Status LED (optional, uses built-in LED on GPIO 2)
- Connecting wires
- Breadboard (optional)

## Pin Configuration

| Component | ESP32 Pin | GPIO |
|-----------|-----------|------|
| DHT11 Data | GPIO 18 | 18 |
| Status LED | GPIO 2 | 2 |

## Wiring

**DHT11 Sensor:**
- VCC → 3.3V
- GND → GND
- DATA → GPIO 18

**Status LED:**
- Anode → GPIO 2
- Cathode → GND (through 220Ω resistor)

## Software Setup

This project provides two different approaches:

1. **ESP-IDF C++ Implementation** (Original approach)
2. **ESPHome YAML Configuration** (Alternative approach)

### Option 1: ESP-IDF Implementation

This project uses ESP-IDF (Espressif IoT Development Framework) and includes the following components:

- FreeRTOS
- ESP-IDF WiFi
- ESP-IDF HTTP Server
- ESP-IDF MQTT Client
- ESP-IDF GPIO Driver
- ESP-IDF NVS Flash

#### Configuration

Before flashing the firmware, update the following configuration in the source code:

```cpp
// Edit these with your WiFi credentials
#define WIFI_SSID_1 "Your_WiFi_SSID"
#define WIFI_PASS_1 "Your_WiFi_Password"
#define WIFI_SSID_2 "Backup_WiFi_SSID" // Optional backup network
#define WIFI_PASS_2 "Backup_WiFi_Password"

// Fallback AP mode credentials
#define AP_SSID "Fallback_Hotspot"
#define AP_PASS "llnDapo0emZw"

// Update with your MQTT broker IP
#define MQTT_BROKER_URI "mqtt://192.168.1.10"
```

#### Installation Steps

1. **Set up ESP-IDF environment:**
   ```bash
   git clone https://github.com/espressif/esp-idf.git
   cd esp-idf
   ./install.sh
   . ./export.sh
   ```

2. **Clone this repository:**
   ```bash
   git clone <your-repo-url>
   cd esp32-environmental-monitor
   ```

3. **Configure the project:**
   ```bash
   idf.py set-target esp32
   idf.py menuconfig
   ```

4. **Update WiFi and MQTT credentials in the source code**

5. **Build and flash:**
   ```bash
   idf.py build
   idf.py flash monitor
   ```

### Option 2: ESPHome Configuration

As an alternative to the ESP-IDF approach, you can use ESPHome for easier configuration and Home Assistant integration. Uploaded the yaml configuration in V. 1.1.

#### ESPHome Installation Steps

1. **Install ESPHome:**
   ```bash
   pip install esphome
   ```

2. **Update the YAML configuration:**
   - Replace `YOUR_WIFI_SSID` and `YOUR_WIFI_PASSWORD` with your actual WiFi credentials
   - Modify other settings as needed

3. **Compile and upload:**
   ```bash
   esphome compile esp32-dht11.yaml
   esphome upload esp32-dht11.yaml
   ```

4. **Monitor logs:**
   ```bash
   esphome logs esp32-dht11.yaml
   ```

#### ESPHome Benefits

- **Easier Configuration**: YAML-based configuration instead of C++ code
- **Home Assistant Integration**: Native integration with Home Assistant
- **OTA Updates**: Over-the-air updates built-in
- **Web Dashboard**: Built-in web interface for monitoring
- **Auto-discovery**: Automatic discovery in Home Assistant
- **Less Code**: No need to write custom C++ code

## API Endpoints (ESP-IDF Version Only)

Once the device is connected to WiFi, you can access the following HTTP endpoints:

### GET /temperature
Returns current temperature reading in JSON format.

**Response:**
```json
{
  "temperature": 23.50
}
```

### GET /humidity
Returns current humidity reading in JSON format.

**Response:**
```json
{
  "humidity": 45.20
}
```

### GET /status
Returns comprehensive system status including sensor readings and connectivity status.

**Response:**
```json
{
  "temperature": 23.50,
  "humidity": 45.20,
  "wifi_connected": true,
  "sensor_ok": true
}
```

## MQTT Topics

The device publishes to the following MQTT topics:

- **Temperature State:** `temperature/state`
- **Humidity State:** `humidity/state`
- **Home Assistant Discovery:**
  - `homeassistant/sensor/temperature/config`
  - `homeassistant/sensor/humidity/config`

## Home Assistant Integration

The device automatically publishes Home Assistant discovery messages, making it easy to integrate with your Home Assistant installation. The sensors will appear as:

- **Temperature Sensor**: Shows current temperature in °C
- **Humidity Sensor**: Shows current humidity in %

For the ESPHome version, the integration is even simpler as ESPHome provides native Home Assistant integration.

## Status LED Behavior

The status LED provides visual feedback about the system state:

- **Off**: Normal operation, WiFi connected, sensor working
- **Quick Blink (100ms)**: Data successfully read and transmitted
- **Fast Blink (75ms)**: Error state (WiFi disconnected or sensor offline)

## Logging

The system provides comprehensive logging including:

- DHT11 sensor read cycles with success/failure rates
- WiFi connection status and IP configuration
- HTTP request logging with response data (ESP-IDF version)
- MQTT publish confirmations
- System status reports every 10 seconds
- Memory usage monitoring

## Error Handling

The system includes robust error handling:

- **WiFi Disconnection**: Automatic reconnection attempts
- **Sensor Failures**: Graceful handling with retry logic
- **MQTT Connection**: Automatic reconnection on network recovery
- **Checksum Validation**: DHT11 data integrity verification
- **Timeout Protection**: Prevents system hangs during sensor reads

## Troubleshooting

### WiFi Connection Failed
- Check WiFi credentials in configuration
- Verify WiFi network is available
- Check serial monitor for connection errors

### Sensor Reading Failures
- Verify DHT11 wiring connections
- Check power supply (3.3V)
- Ensure pull-up resistor on data line (usually built-in)

### HTTP Server Not Accessible (ESP-IDF version)
- Check WiFi connection status
- Verify IP address from serial monitor
- Ensure device and client are on same network

### MQTT Not Working
- Verify MQTT broker IP and accessibility
- Check MQTT broker logs
- Ensure proper network connectivity

### ESPHome Specific Issues
- Check ESPHome logs for compilation errors
- Verify YAML syntax is correct
- Ensure Home Assistant API is accessible

## Debugging

**ESP-IDF Version:**
Enable detailed logging by setting log level to DEBUG in menuconfig:
`Component config → Log output → Default log verbosity → Debug`

**ESPHome Version:**
Use the ESPHome logs command:
```bash
esphome logs esp32-dht11-mqtt.yaml
```

## Performance Specifications

- **Sensor Update Rate**: 3 seconds
- **WiFi Reconnection**: Automatic
- **HTTP Response Time**: < 100ms (ESP-IDF version)
- **MQTT Publish Rate**: Every sensor update
- **Memory Usage**: ~50KB heap usage
- **Power Consumption**: ~100mA @ 3.3V

## Contributing

- Fork the repository
- Create a feature branch
- Make your changes
- Add tests if applicable
- Submit a pull request

## Changelog

### v1.0.0
- Initial release
- DHT11 sensor integration
- WiFi connectivity with fallback AP
- HTTP REST API (ESP-IDF version)
- MQTT publishing
- Home Assistant auto-discovery
- Status LED feedback
- Logging
- ESPHome configuration alternative

---

**Note:** This project is designed for educational and personal use. For production deployments, consider additional security measures and error handling based on your specific requirements.

Choose the approach that best fits your needs:
- **ESP-IDF**: More control, custom HTTP API, lower-level programming
- **ESPHome**: Easier setup, better Home Assistant integration, YAML configuration