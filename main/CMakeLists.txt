idf_component_register(SRCS "main.c" INCLUDE_DIRS "."
    PRIV_REQUIRES console godox_trigger godox_protocol rf_transmitter stage_control stage_ui
    esp_app_format esp_hw_support spi_flash esp_timer nvs_flash esp_event)
