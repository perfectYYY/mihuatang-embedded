#include "led_controller.h"  
#include "driver/gpio.h"  
#include "esp_log.h"  
#include <string.h>  
#include "command_dispatcher.h"  
#include "led_strip.h" 
#include "uart_service.h" 

static const char *TAG = "LED_CONTROLLER";  

static led_strip_handle_t s_led_strip_handle = NULL;  

static void led_command_handler(const char *command, size_t len);  


esp_err_t led_controller_init(void) {  
    ESP_LOGI(TAG, "正在初始化LED硬件...");  
    
    led_strip_config_t strip_config = {  
        .strip_gpio_num = 48, 
        .max_leds = 1,  
    };  
    led_strip_rmt_config_t rmt_config = {  
        .resolution_hz = 10 * 1000 * 1000,  
    };  
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &s_led_strip_handle));  
    led_strip_clear(s_led_strip_handle);  
    ESP_LOGI(TAG, "LED硬件初始化完成");  

    ESP_LOGI(TAG, "正在向命令分发中心注册 'led' 命令...");  
    esp_err_t err = command_dispatcher_register("led", led_command_handler);  
    if (err != ESP_OK) {  
        ESP_LOGE(TAG, "注册 'led' 命令失败!");  
        return err;  
    }  

    return ESP_OK;  
}  
 
static void led_command_handler(const char *command, size_t len)  
{  
    ESP_LOGI(TAG, "收到分发中心转发来的LED命令: %.*s", len, command);  

    const char *sub_command = command + strlen("led:"); 

    if (strncmp(sub_command, "on", 2) == 0) {  
        ESP_LOGI(TAG, "执行开灯操作 (绿色)");  
        if (s_led_strip_handle) {  
            led_strip_set_pixel(s_led_strip_handle, 0, 0, 255, 0);  
            led_strip_refresh(s_led_strip_handle);  
            uart_service_send_line("STATUS:LED_ON");
        }  
    }   
    else if (strncmp(sub_command, "off", 3) == 0) {  
        ESP_LOGI(TAG, "执行关灯操作");  
        if (s_led_strip_handle) {  
            led_strip_clear(s_led_strip_handle);  
            uart_service_send_line("STATUS:LED_OFF");
        }  
    }  
    else {  
        ESP_LOGW(TAG, "未知的LED子命令: %s", sub_command);  
    }  
}  