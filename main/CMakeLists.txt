idf_component_register(
  SRCS "main.c"
  INCLUDE_DIRS "."
  REQUIRES esp_http_server esp_netif esp_event nvs_flash driver mqtt
)