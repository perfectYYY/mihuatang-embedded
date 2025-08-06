idf_component_register(SRCS "src/fan_controller.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES "driver log command_dispatcher uart_service")  