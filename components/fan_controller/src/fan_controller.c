#include "fan_controller.h"  
#include "driver/ledc.h"  
#include "esp_log.h"  
#include <stdlib.h>  
#include <string.h>  

#include "command_dispatcher.h"  
#include "uart_service.h"  

static const char *TAG = "FAN_CONTROLLER";  

#define FAN_PWM_PIN             GPIO_NUM_41  
#define FAN_LEDC_TIMER          LEDC_TIMER_1  
#define FAN_LEDC_MODE           LEDC_LOW_SPEED_MODE  
#define FAN_LEDC_CHANNEL        LEDC_CHANNEL_1 
#define FAN_PWM_FREQUENCY_HZ    25000  
#define FAN_LEDC_RESOLUTION     LEDC_TIMER_10_BIT  

void fan_command_handler(const char *command, size_t len);  

/**  
 * @brief 初始化风扇控制器  
 */  
esp_err_t fan_controller_init(void) {  
    ESP_LOGI(TAG, "正在初始化风扇PWM硬件 (使用Timer: %d, Channel: %d)...", FAN_LEDC_TIMER, FAN_LEDC_CHANNEL);  

    ledc_timer_config_t ledc_timer = {  
        .speed_mode       = FAN_LEDC_MODE,  
        .timer_num        = FAN_LEDC_TIMER, 
        .duty_resolution  = FAN_LEDC_RESOLUTION,  
        .freq_hz          = FAN_PWM_FREQUENCY_HZ,  
        .clk_cfg          = LEDC_AUTO_CLK  
    };  
    ESP_ERROR_CHECK(ledc_timer_config(&ledc_timer));  

    ledc_channel_config_t ledc_channel = {  
        .speed_mode     = FAN_LEDC_MODE,  
        .channel        = FAN_LEDC_CHANNEL, 
        .timer_sel      = FAN_LEDC_TIMER,  
        .intr_type      = LEDC_INTR_DISABLE,  
        .gpio_num       = FAN_PWM_PIN,  
        .duty           = 0,
        .hpoint         = 0  
    };  
    ESP_ERROR_CHECK(ledc_channel_config(&ledc_channel));  
    ESP_LOGI(TAG, "风扇硬件初始化完成，使用GPIO %d", FAN_PWM_PIN);  

    ESP_LOGI(TAG, "正在向命令分发中心注册 'fan' 命令...");  
    esp_err_t err = command_dispatcher_register("fan", fan_command_handler);  
    if (err != ESP_OK) {  
        ESP_LOGE(TAG, "注册 'fan' 命令失败!");  
        return err;  
    }  

    return ESP_OK;  
}  

void fan_command_handler(const char *command, size_t len)  
{  
    ESP_LOGI(TAG, "收到分发中心转发来的风扇命令: %.*s", len, command);  

    const char *sub_command = command + strlen("fan:");  
    int speed_percentage = atoi(sub_command);  

    if (speed_percentage < 0) {  
        speed_percentage = 0;  
    } else if (speed_percentage > 100) {  
        speed_percentage = 100;  
    }  

    ESP_LOGI(TAG, "执行风扇调速操作，速度设置为: %d%%", speed_percentage);  

    uint32_t max_duty = (1 << FAN_LEDC_RESOLUTION) - 1;  
    uint32_t duty = (speed_percentage * max_duty) / 100;  

    ESP_ERROR_CHECK(ledc_set_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL, duty));  
    ESP_ERROR_CHECK(ledc_update_duty(FAN_LEDC_MODE, FAN_LEDC_CHANNEL));  
 
    char status_buffer[32];  
    snprintf(status_buffer, sizeof(status_buffer), "STATUS:FAN_SPEED_SET:%d", speed_percentage);  
    uart_service_send_line(status_buffer);  
}  