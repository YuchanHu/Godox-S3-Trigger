idf_component_register(SRCS "rf_transmitter.c" "rf_receiver.c" INCLUDE_DIRS "include"
    REQUIRES esp_common PRIV_REQUIRES firmware godox_waveform esp_timer esp_phy esp_wifi)
target_link_options(${COMPONENT_LIB} INTERFACE "-T${CMAKE_CURRENT_LIST_DIR}/rf_memory.ld")

set_source_files_properties(rf_receiver.c PROPERTIES COMPILE_OPTIONS "-O2")
