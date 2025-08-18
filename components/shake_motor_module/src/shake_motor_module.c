#include "shake_motor_module.h"
#include "esp_log.h"  
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  
#include <string.h>  

#include "command_dispatcher.h"  
#include "uart_service.h"  

#define SHAKE_MOTOR_GPIO_IN1 35
#define SHAKE_MOTOR_GPIO_IN2 34
#define SHAKE_MOTOR_GPIO_PWM 33
#define SHAKE_MOTOR_PWM_FREQ_HZ       5000
#define SHAKE_MOTOR_PWM_RESOLUTION    LEDC_TIMER_8_BIT
#define SHAKE_MOTOR_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define SHAKE_MOTOR_LEDC_TIMER        LEDC_TIMER_1
#define SHAKE_MOTOR_LEDC_CHANNEL      LEDC_CHANNEL_1

// --- 模块内部状态 ---  
static const char *TAG = "SHAKE_MOTOR_MODULE";
static bool s_is_initialized = false;
static bool s_current_state = false; // 震动电机状态 (true=ON, false=OFF)
static uint8_t s_current_speed = 0; // 当前速度百分比 (0-100)

esp_err_t shake_motor_module_init(void);
void shake_motor_command_handler(const char *command, size_t len);
static esp_err_t shake_motor_set(bool enable);
static esp_err_t shake_motor_set_speed(uint8_t speed_percentage);


esp_err_t shake_motor_module_init(void){
    if (s_is_initialized) {
        ESP_LOGE(TAG, "模块已初始化，跳过重复初始化");
        return ESP_OK;
    }

    // 初始化 GPIO
    gpio_set_direction(SHAKE_MOTOR_GPIO_IN1, GPIO_MODE_OUTPUT);
    gpio_set_direction(SHAKE_MOTOR_GPIO_IN2, GPIO_MODE_OUTPUT);
    gpio_set_direction(SHAKE_MOTOR_GPIO_PWM, GPIO_MODE_OUTPUT);

    // 初始化 LEDC
    ledc_timer_config_t ledc_timer = {
        .speed_mode = SHAKE_MOTOR_LEDC_SPEED_MODE,
        .timer_num = SHAKE_MOTOR_LEDC_TIMER,
        .duty_resolution = SHAKE_MOTOR_PWM_RESOLUTION,
        .freq_hz = SHAKE_MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK
    };
    ledc_timer_config(&ledc_timer);

    ledc_channel_config_t ledc_channel = {
        .speed_mode = SHAKE_MOTOR_LEDC_SPEED_MODE,
        .channel = SHAKE_MOTOR_LEDC_CHANNEL,
        .timer_sel = SHAKE_MOTOR_LEDC_TIMER,
        .intr_type = LEDC_INTR_DISABLE,
        .gpio_num = SHAKE_MOTOR_GPIO_PWM,
        .duty = 0,
        .hpoint = 0
    };
    ledc_channel_config(&ledc_channel);

    ESP_ERROR_CHECK(command_dispatcher_register("shake", shake_motor_command_handler));
    s_is_initialized = true;
    ESP_LOGI(TAG, "摆动电机模块初始化完成");
    return ESP_OK;
}

void shake_motor_command_handler(const char *command, size_t len) {
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化");
        return;
    }

    const char *sub_command = command + strlen("shake:");

    if (strncmp(sub_command, "start", strlen("start")) == 0) {
        shake_motor_set(true);
        ESP_LOGI(TAG, "摆动电机已启动");
    } else if (strncmp(sub_command, "off", strlen("off")) == 0) {
        shake_motor_set(false);
        ESP_LOGI(TAG, "摆动电机已关闭");
    } else if (strncmp(sub_command, "speed", strlen("speed")) == 0) {
        uint8_t speed = atoi(sub_command + strlen("speed"));
        shake_motor_set_speed(speed);
        ESP_LOGI(TAG, "摆动电机速度已设置为: %d%%", speed);
    } else {
        ESP_LOGW(TAG, "未知命令: %.*s", len, command);
    }
}   


static esp_err_t shake_motor_set(bool enable) {
    if(enable){
        gpio_set_level(SHAKE_MOTOR_GPIO_IN1, 1);
        gpio_set_level(SHAKE_MOTOR_GPIO_IN2, 0);
    } else {
        gpio_set_level(SHAKE_MOTOR_GPIO_IN1, 0);
        gpio_set_level(SHAKE_MOTOR_GPIO_IN2, 0);
    }
    s_current_state = enable;
    return ESP_OK;
}


static esp_err_t shake_motor_set_speed(uint8_t speed_percentage) {
     if (speed_percentage > 100) {
        speed_percentage = 100;
    }
    uint32_t max_duty = (1 << SHAKE_MOTOR_PWM_RESOLUTION) - 1;
    uint32_t duty = (max_duty * speed_percentage) / 100;

    ledc_set_duty(SHAKE_MOTOR_LEDC_SPEED_MODE, SHAKE_MOTOR_LEDC_CHANNEL, duty);
    ledc_update_duty(SHAKE_MOTOR_LEDC_SPEED_MODE, SHAKE_MOTOR_LEDC_CHANNEL);

    ESP_LOGI(TAG, "电机速度设置为: %d%% (Duty: %lu)", speed_percentage, duty);
    s_current_speed = speed_percentage;
    return ESP_OK;
}