idf_component_register(SRCS "src/function_controller.c"  
                    INCLUDE_DIRS "include"  
                    REQUIRES "driver log freertos command_dispatcher uart_service dc_motor_control water_level_sensor_module stepper_motor_module relay_module esp_common esp-modbus")  
