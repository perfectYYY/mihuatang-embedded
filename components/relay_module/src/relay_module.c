#include "relay_module.h"  
#include "esp_log.h"  
#include "driver/gpio.h"  
#include <string.h>  

#include "command_dispatcher.h"  
#include "uart_service.h"  

// --- 配置宏定义 ---  
// 在这里修改继电器控制引脚和有效电平  
#define RELAY_GPIO_NUM          38       // 对应STM32的 COMPRESSOR_RELAY_GPIO  
#define RELAY_ACTIVE_LEVEL      false   // false 表示低电平有效 (ON = 0V, OFF = 3.3V)  
#define RELAY_INITIAL_STATE     false   // false 表示初始状态为关闭 (OFF)  

#define RELAY_COMMAND_PREFIX "relay"  

// --- 模块内部状态 ---  
static const char *TAG = "RELAY_MODULE";  
static bool s_is_initialized = false;  
static bool s_current_state = RELAY_INITIAL_STATE; // 继电器的逻辑状态 (true=ON, false=OFF)  

// --- 功能函数声明 ---  
void relay_command_handler(const char *command, size_t len);  
static void relay_set_state_action(bool state);  
static void send_status_update(void);  


esp_err_t relay_module_init(void)  
{  
    if (s_is_initialized) {  
        ESP_LOGW(TAG, "继电器模块已初始化，无需重复。");  
        return ESP_OK;  
    }  
    ESP_LOGI(TAG, "正在初始化继电器模块 (for Compressor)...");  

    // 1. 配置GPIO  
    gpio_config_t io_conf = {  
        .intr_type = GPIO_INTR_DISABLE,  
        .mode = GPIO_MODE_OUTPUT,  
        .pin_bit_mask = (1ULL << RELAY_GPIO_NUM),  
        .pull_down_en = GPIO_PULLDOWN_DISABLE,  
        .pull_up_en = GPIO_PULLUP_DISABLE,   
    };  
    esp_err_t ret = gpio_config(&io_conf);  
    if (ret != ESP_OK) {  
        ESP_LOGE(TAG, "继电器GPIO配置失败: %s", esp_err_to_name(ret));  
        return ret;  
    }  

    // 设置推挽输出的最大驱动能力 (40mA)  
    gpio_set_drive_capability(RELAY_GPIO_NUM, GPIO_DRIVE_CAP_3);  

    // 2. 注册命令处理器  
    ret = command_dispatcher_register(RELAY_COMMAND_PREFIX, relay_command_handler);  
    if (ret != ESP_OK) {  
        ESP_LOGE(TAG, "注册 '%s' 命令失败!", RELAY_COMMAND_PREFIX);  
        gpio_reset_pin(RELAY_GPIO_NUM); // 注册失败时重置GPIO  
        return ret;  
    }  
    
    // 3. 设置初始状态  
    relay_set_state_action(RELAY_INITIAL_STATE);  
    s_is_initialized = true;  
    
    ESP_LOGI(TAG, "继电器模块初始化完成。GPIO:%d, ActiveLevel:%s",   
             RELAY_GPIO_NUM, RELAY_ACTIVE_LEVEL ? "HIGH" : "LOW");  
    return ESP_OK;  
}  

/**  
 * @brief 命令处理器，处理所有 "relay:" 前缀的命令  
 */  
void relay_command_handler(const char *command, size_t len)  
{  
    if (!s_is_initialized) {  
        ESP_LOGE(TAG, "模块未初始化，无法处理命令: %.*s", (int)len, command);  
        return;  
    }  

    const char *sub_command = command + strlen(RELAY_COMMAND_PREFIX) + 1; // +1 跳过 ':'  

    if (strncmp(sub_command, "on", strlen("on")) == 0) {  
        relay_set_state_action(true);  
    }   
    else if (strncmp(sub_command, "off", strlen("off")) == 0) {  
        relay_set_state_action(false);  
    }  
    else if (strncmp(sub_command, "toggle", strlen("toggle")) == 0) {  
        relay_set_state_action(!s_current_state);  
    }  
    else if (strncmp(sub_command, "status", strlen("status")) == 0) {  
        // 状态在每次动作后自动发送，这里无需额外操作  
    }  
    else {  
        ESP_LOGW(TAG, "未知的继电器子命令: %s", sub_command);  
        return; // 未知命令不发送状态  
    }  
    
    // 每次有效动作后，都发送一次最新状态  
    send_status_update();  
}  

/**  
 * @brief 执行设置继电器状态的动作  
 * @param state 逻辑状态: true=ON, false=OFF  
 */  
static void relay_set_state_action(bool state)  
{  
    // 根据有效电平计算最终要输出的GPIO电平  
    // 如果 state is true (ON) and active_level is HIGH (true) -> level = 1  
    // 如果 state is true (ON) and active_level is LOW (false) -> level = 0  
    // 如果 state is false (OFF) and active_level is HIGH (true) -> level = 0  
    // 如果 state is false (OFF) and active_level is LOW (false) -> level = 1  
    uint8_t gpio_level = (state == RELAY_ACTIVE_LEVEL);  
    
    gpio_set_level(RELAY_GPIO_NUM, gpio_level);  
    s_current_state = state;  
    
    ESP_LOGI(TAG, "设置继电器状态 -> %s. (GPIO%d 输出电平: %d)",   
             state ? "ON" : "OFF", RELAY_GPIO_NUM, gpio_level);  
}  


/**  
 * @brief 发送当前状态到UART  
 */  
static void send_status_update(void)  
{  
    if (s_current_state) {  
        uart_service_send_line("STATUS:RELAY_ON");  
    } else {  
        uart_service_send_line("STATUS:RELAY_OFF");  
    }  
}  