idf_component_register(SRCS "src/ds18b20_manager.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES "ds18b20" "onewire_bus" "command_dispatcher" "uart_service")  