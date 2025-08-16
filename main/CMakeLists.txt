idf_component_register(SRCS "main.c"
                    PRIV_REQUIRES spi_flash
                    INCLUDE_DIRS "."
                    REQUIRES led_strip driver json mqtt esp_wifi nvs_flash relay_module dc_motor_control
                    stepper_motor_module steam_valve_module uart_service led_controller command_dispatcher
                    fan_controller dht22_sensor ds18b20_manager water_level_sensor_module function_controller 
                    relay_module_two compressor_control
                    )
