### MIHUATANG-README

---



##### 这是米花糖科技衣物护理机的开发调试手册

>本项目代码使用ESP-IDF 构建，开发版本为ESP-IDF v6.0.0，请注意版本不得低于V5.4.2

##### 下面介绍各组件，方便开发调试

* uart_service

>目前使用uart 串口与一块控制触摸屏的esp-s3 通信，`TX PIN 17`这是 TX，`RX PIN 18`这是 RX，波特率为 115200

* command_dispatcher

> 这是用于uart 指令转发的中间件，其余组件在这里注册命令

* DHT22_sensor

>这是温湿传组件，需要用到`  zorxx/dht: ^1.0.1`组件，硬件引脚为`GPIO 40`,读取请求为`get_temp_humi`，期待格式为
>
>```c
>        float temperature = 0;  
>        float humidity = 0;  
>
>        esp_err_t ret = dht_read_float_data(  
>            DHT_TYPE_AM2301,         
>            SENSOR_GPIO_PIN,  
>            &humidity,  
>            &temperature  
>        );  
>
>```

* ds18b20

>这是温传组件，需要用到`  espressif/ds18b20: ^0.1.2`与` espressif/onewire_bus: ^1.0.4`组件，硬件引脚为`GPIO 39`,读取请求为`get_temp`

* Fan_controller

>这是风扇组件，PWM 控制Pin 口为`GPIO 41` 
>
>频率是25KHZ

* Led_controller

>无用，仅作串口调试用

* Relay_module

>这是继电器组件，`GPIO 38`

* dc_motor_control

>这是蒸汽部分水泵电机的控制组件，`IN1_GPIO 15`,`IN2_GPIO 16`,`PWM 4`,1KHZ

* Steam_valve_control

>这是蒸汽部分电磁阀的组件

Stepper_motor_control

>这是压缩机部分电磁阀的控制组件



等下再写····