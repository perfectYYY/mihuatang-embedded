// components/water_level_sensor_module/include/water_level_sensor_module.h

#ifndef WATER_LEVEL_SENSOR_MODULE_H
#define WATER_LEVEL_SENSOR_MODULE_H

#include "esp_err.h"
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化水位传感器模块 (ADC版本)
 * 
 * 该函数会配置ADC通道用于检测水位。
 * 命令 "waterlevel:check" 会触发一次水位检测。
 * 检测结果会通过UART以 "STATUS:WATER,Level:..." 和 "STATUS:WATER,Voltage:X.XX" 的格式上报。
 * 
 * @return esp_err_t 
 */
esp_err_t water_level_sensor_module_init(void);

/**
 * @brief 直接获取当前水位状态 (ADC版本)
 * 
 * @return true 水位已到达
 * @return false 水位未到达
 */
bool water_level_is_reached(void);

/**
 * @brief 直接获取当前检测到的电压值
 * 
 * @return float 检测到的电压值(V)
 */
float water_level_get_voltage(void);

#ifdef __cplusplus
}
#endif

#endif // WATER_LEVEL_SENSOR_MODULE_H