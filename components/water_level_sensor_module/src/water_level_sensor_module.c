// components/water_level_sensor_module/src/water_level_sensor_module.c

#include "water_level_sensor_module.h"
#include "esp_log.h"
#include "esp_adc/adc_oneshot.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"

#include "command_dispatcher.h"
#include "uart_service.h"

// --- ADC配置 ---
#define ADC_CHANNEL         ADC_CHANNEL_0   // GPIO1 对应 ADC1_CHANNEL_0
#define ADC_ATTEN           ADC_ATTEN_DB_12 // 衰减设置为12dB，测量范围约为 0-3.1V
#define LEVEL_THRESHOLD_MV  1500            // 水位阈值，1500mV = 1.5V

// --- 模块内部定义 ---
static const char *TAG = "WATER_LEVEL_ADC";
static const char *COMMAND_PREFIX = "waterlevel";
static bool s_is_initialized = false;
static adc_oneshot_unit_handle_t s_adc_handle;
static adc_cali_handle_t s_adc_cali_handle = NULL;

// --- 函数声明 ---
void command_handler(const char *command, size_t len);
static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle);


esp_err_t water_level_sensor_module_init(void) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "模块已初始化。");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在初始化ADC水位传感器模块...");

    // 1. 初始化ADC单元
    adc_oneshot_unit_init_cfg_t init_config = {
        .unit_id = ADC_UNIT_1,
        .ulp_mode = ADC_ULP_MODE_DISABLE,
    };
    ESP_ERROR_CHECK(adc_oneshot_new_unit(&init_config, &s_adc_handle));

    // 2. 配置ADC通道
    adc_oneshot_chan_cfg_t config = {
        .bitwidth = ADC_BITWIDTH_DEFAULT,
        .atten = ADC_ATTEN,
    };
    ESP_ERROR_CHECK(adc_oneshot_config_channel(s_adc_handle, ADC_CHANNEL, &config));

    // 3. ADC校准 (用于将原始读数转换为精确的毫伏值)
    bool calibrated = adc_calibration_init(ADC_UNIT_1, ADC_ATTEN, &s_adc_cali_handle);
    if (!calibrated) {
        ESP_LOGW(TAG, "ADC校准失败，电压读数可能不准。");
    }

    // 4. 注册命令处理器
    ESP_ERROR_CHECK(command_dispatcher_register(COMMAND_PREFIX, command_handler));

    s_is_initialized = true;
    ESP_LOGI(TAG, "ADC水位传感器模块初始化完成。阈值: %dmV", LEVEL_THRESHOLD_MV);
    return ESP_OK;
}

/**
 * @brief 获取原始ADC读数并转换为电压
 */
static int get_voltage_mv() {
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化");
        return 0;
    }
    
    int adc_raw;
    ESP_ERROR_CHECK(adc_oneshot_read(s_adc_handle, ADC_CHANNEL, &adc_raw));

    int voltage_mv = 0;
    if (s_adc_cali_handle) {
        ESP_ERROR_CHECK(adc_cali_raw_to_voltage(s_adc_cali_handle, adc_raw, &voltage_mv));
    } else {
        // 如果没有校准数据，只能做一个粗略估算 (不推荐)
        voltage_mv = adc_raw * 3100 / 4095; // 假设3.1V满量程，12bit精度
    }
    return voltage_mv;
}


float water_level_get_voltage(void) {
    int voltage_mv = get_voltage_mv();
    return (float)voltage_mv / 1000.0f;
}


bool water_level_is_reached(void) {
    int voltage_mv = get_voltage_mv();
    bool is_reached = (voltage_mv >= LEVEL_THRESHOLD_MV);
    
    ESP_LOGI(TAG, "检测到电压: %d mV. 状态: %s", voltage_mv, is_reached ? "REACHED" : "LOW");
    return is_reached;
}

/**
 * @brief 命令处理器
 */
void command_handler(const char *command, size_t len) {
    const char *sub_command = command + strlen(COMMAND_PREFIX);
    if (*sub_command == ':') sub_command++;

    if (strncmp(sub_command, "check", strlen("check")) == 0) {
        int voltage_mv = get_voltage_mv();
        bool is_reached = (voltage_mv >= LEVEL_THRESHOLD_MV);
        
        char status_str[128];
        // 上报状态
        snprintf(status_str, sizeof(status_str), "STATUS:WATER,Level:%s", is_reached ? "REACHED" : "LOW");
        uart_service_send_line(status_str);
        
        // 上报具体电压值
        snprintf(status_str, sizeof(status_str), "STATUS:WATER,Voltage:%.2f", (float)voltage_mv / 1000.0f);
        uart_service_send_line(status_str);
    } else {
        ESP_LOGW(TAG, "未知子命令: %s", sub_command);
    }
}

static bool adc_calibration_init(adc_unit_t unit, adc_atten_t atten, adc_cali_handle_t *out_handle) {
    adc_cali_handle_t handle = NULL;
    esp_err_t ret = ESP_FAIL;
    bool calibrated = false;

    adc_cali_curve_fitting_config_t cali_config = {
        .unit_id = unit,
        .atten = atten,
        .bitwidth = ADC_BITWIDTH_DEFAULT,
    };
    ret = adc_cali_create_scheme_curve_fitting(&cali_config, &handle);
    if (ret == ESP_OK) {
        calibrated = true;
    }

    *out_handle = handle;
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "ADC 校准方案: Curve Fitting");
    } else {
        ESP_LOGW(TAG, "无法加载ADC校准数据");
    }
    return calibrated;
}