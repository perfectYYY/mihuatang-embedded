
idf_component_register(
    SRCS "src/relay_module.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log command_dispatcher uart_service
)