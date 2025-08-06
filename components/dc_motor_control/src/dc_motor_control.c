#include "dc_motor_control.h"
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include "command_dispatcher.h"
#include "uart_service.h"

// --- 宏定义 ---
#define MOTOR_IN1_GPIO          15
#define MOTOR_IN2_GPIO          16
#define MOTOR_PWM_GPIO          4
#define MOTOR_PWM_FREQ_HZ       1000
#define MOTOR_PWM_RESOLUTION    LEDC_TIMER_10_BIT
#define MOTOR_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define MOTOR_LEDC_TIMER        LEDC_TIMER_0
#define MOTOR_LEDC_CHANNEL      LEDC_CHANNEL_0

// --- 模块私有状态 ---
static const char *TAG = "DC_MOTOR_MODULE";
static bool s_is_initialized = false;
typedef enum {
    MOTOR_DIR_STOP = 0,
    MOTOR_DIR_FORWARD,
    MOTOR_DIR_REVERSE,
    MOTOR_DIR_BRAKE
} motor_direction_t;

// --- 内部函数声明 ---
void motor_command_handler(const char *command, size_t len);
static esp_err_t motor_set_direction(motor_direction_t direction);
static esp_err_t motor_set_speed(uint8_t speed_percentage);
static void motor_stop_action(void);
esp_err_t dc_motor_module_init(void);

/**
 * @brief 命令处理器，处理所有 "motor:" 前缀的命令
 */
void motor_command_handler(const char *command, size_t len)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化，无法处理命令: %.*s", (int)len, command);
        return;
    }

    const char *sub_command = command + strlen("motor:");
    uint8_t speed_val = 0;

    // 优先检查带参数的 speed 命令
    if (sscanf(sub_command, "speed:%hhu", &speed_val) == 1) {
        motor_set_speed(speed_val);
        ESP_LOGI(TAG, "收到速度指令: %d%%", speed_val);
    }
    // 然后检查简单的动作指令，使用 strncmp 忽略末尾的换行符
    else if (strncmp(sub_command, "forward", strlen("forward")) == 0) {
        motor_set_direction(MOTOR_DIR_FORWARD);
        ESP_LOGI(TAG, "收到前进指令");
    }
    else if (strncmp(sub_command, "reverse", strlen("reverse")) == 0) {
        motor_set_direction(MOTOR_DIR_REVERSE);
        ESP_LOGI(TAG, "收到后退指令");
    }
    else if (strncmp(sub_command, "stop", strlen("stop")) == 0) {
        motor_stop_action();
        ESP_LOGI(TAG, "收到停止指令");
    }
    else if (strncmp(sub_command, "brake", strlen("brake")) == 0) {
        motor_set_direction(MOTOR_DIR_BRAKE);
        motor_set_speed(0);
        ESP_LOGI(TAG, "收到刹车指令");
    }
    else {
        ESP_LOGW(TAG, "未知的电机子命令: %s", sub_command);
    }
}

esp_err_t dc_motor_module_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "直流电机模块已初始化，无需重复。");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在初始化直流电机模块 (for Steam)...");

    // 1. 配置方向GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << MOTOR_IN1_GPIO) | (1ULL << MOTOR_IN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "方向GPIO配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 配置LEDC PWM定时器
    ledc_timer_config_t timer_conf = {
        .timer_num = MOTOR_LEDC_TIMER,
        .duty_resolution = MOTOR_PWM_RESOLUTION,
        .freq_hz = MOTOR_PWM_FREQ_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ret = ledc_timer_config(&timer_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC定时器配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 3. 配置LEDC PWM通道
    ledc_channel_config_t channel_conf = {
        .gpio_num = MOTOR_PWM_GPIO,
        .channel = MOTOR_LEDC_CHANNEL,
        .timer_sel = MOTOR_LEDC_TIMER,
        .duty = 0,
        .intr_type = LEDC_INTR_DISABLE,
        .hpoint = 0
    };
    ret = ledc_channel_config(&channel_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "LEDC通道配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 4. 注册命令
    ret = command_dispatcher_register("motor", motor_command_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 'motor' 命令失败!");
        return ret;
    }

    // 5. 设置初始状态
    motor_stop_action();
    s_is_initialized = true;
    ESP_LOGI(TAG, "直流电机模块初始化完成。");
    return ESP_OK;
}

static esp_err_t motor_set_direction(motor_direction_t direction)
{
    switch (direction) {
        case MOTOR_DIR_FORWARD:
            gpio_set_level(MOTOR_IN1_GPIO, 0);
            gpio_set_level(MOTOR_IN2_GPIO, 1);
            break;
        case MOTOR_DIR_REVERSE:
            gpio_set_level(MOTOR_IN1_GPIO, 1);
            gpio_set_level(MOTOR_IN2_GPIO, 0);
            break;
        case MOTOR_DIR_BRAKE:
            gpio_set_level(MOTOR_IN1_GPIO, 1);
            gpio_set_level(MOTOR_IN2_GPIO, 1);
            break;
        case MOTOR_DIR_STOP:
        default:
            gpio_set_level(MOTOR_IN1_GPIO, 0);
            gpio_set_level(MOTOR_IN2_GPIO, 0);
            break;
    }
    return ESP_OK;
}

static esp_err_t motor_set_speed(uint8_t speed_percentage)
{
    if (speed_percentage > 100) {
        speed_percentage = 100;
    }
    uint32_t max_duty = (1 << MOTOR_PWM_RESOLUTION) - 1;
    uint32_t duty = (max_duty * speed_percentage) / 100;

    ledc_set_duty(MOTOR_LEDC_SPEED_MODE, MOTOR_LEDC_CHANNEL, duty);
    ledc_update_duty(MOTOR_LEDC_SPEED_MODE, MOTOR_LEDC_CHANNEL);

    ESP_LOGI(TAG, "电机速度设置为: %d%% (Duty: %lu)", speed_percentage, duty);
    return ESP_OK;
}

static void motor_stop_action(void)
{
    motor_set_direction(MOTOR_DIR_STOP);
    motor_set_speed(0);
    ESP_LOGI(TAG, "电机已停止(高阻态)。");
}