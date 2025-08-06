
idf_component_register(
    SRCS "src/stepper_motor_module.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log freertos command_dispatcher uart_service
)