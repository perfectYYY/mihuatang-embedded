
idf_component_register(
    SRCS "src/dc_motor_control.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log command_dispatcher uart_service
)