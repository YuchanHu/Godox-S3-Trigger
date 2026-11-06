idf_component_register(SRCS "godox_trigger.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES rf_transmitter godox_waveform godox_protocol esp_timer)
