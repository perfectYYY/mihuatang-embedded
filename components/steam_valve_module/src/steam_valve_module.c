#include "steam_valve_module.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include <string.h>

#include "command_dispatcher.h"
#include "uart_service.h"

// --- 配置宏定义 ---
// STM32上的 H14, H15。请根据您的ESP32接线修改
#define VALVE_PIN1_GPIO          13  
#define VALVE_PIN2_GPIO          14  

// 定义命令分发器使用的命令前缀
#define VALVE_COMMAND_PREFIX "valve"

// --- 模块内部状态 ---
static const char *TAG = "STEAM_VALVE_MODULE";
static bool s_is_initialized = false;
static bool s_is_open = false; // 电磁阀的逻辑状态 (true=OPEN, false=CLOSE)

// --- 内部功能函数声明 ---
void valve_command_handler(const char *command, size_t len);
static void valve_set_state_action(bool is_open);
static void send_status_update(void);


/**
 * @brief 初始化蒸汽电磁阀模块
 */
esp_err_t steam_valve_module_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "蒸汽电磁阀模块已初始化，无需重复。");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在初始化蒸汽电磁阀模块...");

    // 1. 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << VALVE_PIN1_GPIO) | (1ULL << VALVE_PIN2_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "电磁阀GPIO配置失败: %s", esp_err_to_name(ret));
        return ret;
    }

    // 2. 注册命令处理器
    ret = command_dispatcher_register(VALVE_COMMAND_PREFIX, valve_command_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 '%s' 命令失败!", VALVE_COMMAND_PREFIX);
        gpio_reset_pin(VALVE_PIN1_GPIO);
        gpio_reset_pin(VALVE_PIN2_GPIO);
        return ret;
    }
    
    // 3. 设置初始为关闭状态
    valve_set_state_action(false);
    s_is_initialized = true;
    
    ESP_LOGI(TAG, "蒸汽电磁阀模块初始化完成。Pin1:%d, Pin2:%d", VALVE_PIN1_GPIO, VALVE_PIN2_GPIO);
    return ESP_OK;
}

/**
 * @brief 命令处理器，处理所有 "valve:" 前缀的命令
 */
void valve_command_handler(const char *command, size_t len)
{
    if (!s_is_initialized) return;

    const char *sub_command = command + strlen(VALVE_COMMAND_PREFIX) + 1; 

    if (strncmp(sub_command, "open", strlen("open")) == 0) {
        valve_set_state_action(true);
    } 
    else if (strncmp(sub_command, "close", strlen("close")) == 0) {
        valve_set_state_action(false);
    }
    else if (strncmp(sub_command, "status", strlen("status")) == 0) {
    }
    else {
        ESP_LOGW(TAG, "未知的电磁阀子命令: %s", sub_command);
        return;
    }
    
    send_status_update();
}

/**
 * @brief 执行设置电磁阀状态的动作
 * @param is_open 逻辑状态: true=OPEN, false=CLOSE
 */
static void valve_set_state_action(bool is_open)
{
    if (is_open) {
        // 开启: Pin1=1, Pin2=0
        gpio_set_level(VALVE_PIN1_GPIO, 1);
        gpio_set_level(VALVE_PIN2_GPIO, 0);
        ESP_LOGI(TAG, "设置电磁阀状态 -> OPEN (Pin1=1, Pin2=0)");
    } else {
        // 关闭: Pin1=0, Pin2=0
        gpio_set_level(VALVE_PIN1_GPIO, 0);
        gpio_set_level(VALVE_PIN2_GPIO, 0);
        ESP_LOGI(TAG, "设置电磁阀状态 -> CLOSE (Pin1=0, Pin2=0)");
    }
    s_is_open = is_open;
}


/**
 * @brief 发送当前状态到UART
 */
static void send_status_update(void)
{
    if (s_is_open) {
        uart_service_send_line("STATUS:VALVE_OPEN");
    } else {
        uart_service_send_line("STATUS:VALVE_CLOSED");
    }
}

//feng
void set_steam_valve(bool is_open)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化，无法设置电磁阀状态");
        return;
    }
    valve_set_state_action(is_open);
    send_status_update();
}