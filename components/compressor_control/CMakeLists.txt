set(KCONFIG ${CMAKE_CURRENT_LIST_DIR}/Kconfig)

idf_component_register(
    SRCS "src/compressor_control.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_common log command_dispatcher uart_service
)