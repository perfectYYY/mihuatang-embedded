idf_component_register(SRCS "src/function_controller.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES "driver log freertos command_dispatcher uart_service")  