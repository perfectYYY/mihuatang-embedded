idf_component_register(SRCS "src/led_controller.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES uart_service driver log led_strip command_dispatcher)  