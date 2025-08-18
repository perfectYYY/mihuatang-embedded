#ifndef STEAM_VALVE_MODULE_H
#define STEAM_VALVE_MODULE_H

#include "esp_err.h"
#include "stdbool.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化蒸汽电磁阀模块
 * 
 * 该函数会配置GPIO，并向命令分发器注册 "valve" 命令。
 * 模块初始化后，即可通过UART等方式发送 "valve:open", "valve:close"
 * 来控制电磁阀。
 * 
 * @return esp_err_t 
 *      - ESP_OK: 初始化成功
 *      - ESP_FAIL: 初始化失败
 */
esp_err_t steam_valve_module_init(void);
void set_steam_valve(bool is_open);


#ifdef __cplusplus
}
#endif

#endif // STEAM_VALVE_MODULE_H