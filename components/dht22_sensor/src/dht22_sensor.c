#include "dht22_sensor.h" // 请确保头文件名正确  
#include "esp_log.h"  
#include <string.h>  
#include "dht.h"  
#include "command_dispatcher.h"  
#include "uart_service.h"  
#include "driver/gpio.h" // *** 新增：为了使用 gpio_reset_pin ***  


static const char *TAG = "DHT22_SENSOR";  
#define SENSOR_GPIO_PIN GPIO_NUM_40

static void sensor_command_handler(const char *command, size_t len);  

esp_err_t dht22_sensor_init(void) {  
    ESP_LOGI(TAG, "正在初始化 DHT22 传感器 (GPIO %d)...", SENSOR_GPIO_PIN);  

    // --- *** 核心修改：在这里显式重置引脚状态 *** ---  
    // 这个函数会将引脚的内部上拉和下拉都禁用，恢复到默认状态。  
    // 这样做可以避免被其他程序错误配置所干扰。  
    ESP_LOGI(TAG, "重置 GPIO %d 以确保禁用内部上拉/下拉...", SENSOR_GPIO_PIN);  
    gpio_reset_pin(SENSOR_GPIO_PIN);  
    // ---------------------------------------------------  

    // 注册 "sensor" 命令处理器  
    esp_err_t err = command_dispatcher_register("sensor", sensor_command_handler);  
    if (err != ESP_OK) {  
        ESP_LOGE(TAG, "注册 'sensor' 命令失败!");  
        return err;  
    }  
    
    ESP_LOGI(TAG, "DHT22 传感器初始化完成。");  
    return ESP_OK;  
}  

static void sensor_command_handler(const char *command, size_t len) {  
    const char *sub_command = command + strlen("sensor:");  

    if (strncmp(sub_command, "get_temp_humi", 13) == 0) {  
        ESP_LOGI(TAG, "收到温湿度读取请求...");  

        float temperature = 0;  
        float humidity = 0;  

        esp_err_t ret = dht_read_float_data(  
            DHT_TYPE_AM2301,         
            SENSOR_GPIO_PIN,  
            &humidity,  
            &temperature  
        );  

        char status_buffer[64];  
        if (ret == ESP_OK) {  
            ESP_LOGI(TAG, "读取成功 -> 温度: %.1f°C, 湿度: %.1f%%", temperature, humidity);  
            snprintf(status_buffer, sizeof(status_buffer),   
                     "STATUS:TEMP_HUMI:%.1f:%.1f", temperature, humidity);  
        } else {  
            ESP_LOGE(TAG, "读取传感器失败! 错误码: %s", esp_err_to_name(ret));  
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:TEMP_HUMI_ERROR");  
        }  
        
        uart_service_send_line(status_buffer);  
    }  
}  