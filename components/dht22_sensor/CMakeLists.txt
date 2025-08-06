idf_component_register(SRCS "src/dht22_sensor.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES "dht" "command_dispatcher" "uart_service")  