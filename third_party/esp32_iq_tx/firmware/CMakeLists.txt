if(IDF_TARGET STREQUAL "esp32s3")
    set(backend "src/targets/esp32s3.c")
    set(platform_requires esp_wifi esp_phy esp_event esp_timer)
else()
    message(FATAL_ERROR "This vendored iq_tx build supports esp32s3 only")
endif()
idf_component_register(SRCS "src/iq_tx.c" "${backend}"
    INCLUDE_DIRS "include" PRIV_INCLUDE_DIRS "src"
    REQUIRES esp_common
    PRIV_REQUIRES ${platform_requires} heap freertos)
