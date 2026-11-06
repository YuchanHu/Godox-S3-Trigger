idf_component_register(SRCS "stage_workspace.c" "stage_rx_pipeline.c" "stage_core.c" "stage_app.c" "stage_cache.c" "stage_cache_plan.c" "stage_timing.c" "joint_cache.c" INCLUDE_DIRS "include"
    REQUIRES esp_common godox_waveform PRIV_REQUIRES godox_trigger godox_protocol rf_transmitter
    nvs_flash esp_timer esp_system esp_partition)
set_source_files_properties(stage_rx_pipeline.c PROPERTIES COMPILE_OPTIONS "-O2")
