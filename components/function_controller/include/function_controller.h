#ifndef FUNCTION_CONTROLLER_H
#define FUNCTION_CONTROLLER_H

#include "esp_err.h"

/**
 * @file function_controller.h
 * @brief 高级功能与业务逻辑控制器
 * 
 * 该模块是系统的“大脑”，负责解析高级业务命令 (例如 "start drying"),
 * 并通过调用底层硬件模块的命令来编排一系列复杂的硬件动作。
 */

/**
 * @brief 初始化功能控制器。
 * 
 * 此函数会向命令分发器注册 "function" 关键字，用于处理所有高级功能请求。
 * 
 * @return esp_err_t 
 *         - ESP_OK: 初始化成功。
 *         - 其他: 初始化失败。
 */
esp_err_t function_controller_init(void);

#endif // FUNCTION_CONTROLLER_H