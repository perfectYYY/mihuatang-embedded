#ifndef FAN_CONTROLLER_H  
#define FAN_CONTROLLER_H  

#include "esp_err.h"  

/**  
 * @brief 初始化风扇控制器。  
 *   
 * - 配置PWM硬件。  
 * - 向命令分发中心注册 "fan" 命令处理器。  
 *   
 * @return esp_err_t ESP_OK on success, or an error code if initialization fails.  
 */  
esp_err_t fan_controller_init(void);  

#endif // FAN_CONTROLLER_H  