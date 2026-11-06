idf_component_register(SRCS "stage_view.c" "stage_board.c" "stage_frame.c" "stage_epd.c" "stage_power.c" INCLUDE_DIRS "include"
    REQUIRES stage_control esp_common PRIV_REQUIRES esp_driver_gpio esp_driver_spi esp_driver_i2c esp_timer esp_adc)
