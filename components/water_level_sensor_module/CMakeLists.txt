
idf_component_register(
    SRCS "src/water_level_sensor_module.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log command_dispatcher uart_service esp_adc
)