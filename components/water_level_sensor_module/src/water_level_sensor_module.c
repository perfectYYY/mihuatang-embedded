#include <string.h>
#include "water_level_sensor_module.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include <string.h>
#include "driver/gpio.h"

#include "command_dispatcher.h"
#include "uart_service.h"

// --- ADC配置 ---
#define ADC_CHANNEL         ADC_CHANNEL_0   
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 衰减设置为12dB，测量范围约为 0-3.1V
#define LEVEL_THRESHOLD_MV  1500            // 水位阈值，1500mV = 1.5V

// --- 模块内部定义 ---
static const char *TAG = "WATER_LEVEL_ADC";
static const char *COMMAND_PREFIX = "waterlevel";
static bool s_is_initialized = false;


// --- 函数声明 ---
void water_level_command_handler(const char *command, size_t len);

esp_err_t water_level_sensor_module_init(void) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "模块已初始化。");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在初始化ADC水位传感器模块...");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_1), // 选择 GPIO5
        .mode = GPIO_MODE_INPUT,              // 设置为输入模式
        .pull_up_en = GPIO_PULLUP_DISABLE,    // 不使能上拉
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // 使能下拉
        .intr_type = GPIO_INTR_DISABLE        // 不使能中断
    };
    gpio_config(&io_conf);

    ESP_ERROR_CHECK(command_dispatcher_register(COMMAND_PREFIX, water_level_command_handler));

    s_is_initialized = true;
    ESP_LOGI(TAG, "ADC水位传感器模块初始化完成。阈值: %dmV", LEVEL_THRESHOLD_MV);
    return ESP_OK;
}

int get_water_level() {
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化");
        return -1;
    }
    int level = gpio_get_level(GPIO_NUM_1);
    if (level == 1) {
        ESP_LOGI(TAG, "GPIO5 为高电平");
        return 1;
    } else {
        ESP_LOGI(TAG, "GPIO5 为低电平");
        return 0;
    }
}

void water_level_command_handler(const char *command, size_t len) {
    const char *sub_command = command + strlen(COMMAND_PREFIX);
    if (*sub_command == ':') sub_command++;

    if (strncmp(sub_command, "check", strlen("check")) == 0) {
        int water_level = get_water_level();
        
        char status_str[128];
        // 上报状态
        snprintf(status_str, sizeof(status_str), "STATUS:water_level_:%s", water_level ? "ON" : "OFF");
        uart_service_send_line(status_str);
    } else {
        ESP_LOGW(TAG, "未知子命令: %s", sub_command);
    }
}

