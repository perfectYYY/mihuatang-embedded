
idf_component_register(
    SRCS "src/shake_motor_module.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log command_dispatcher uart_service
)