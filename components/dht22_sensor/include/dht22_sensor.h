#ifndef SENSOR_MANAGER_H  
#define SENSOR_MANAGER_H  

#include "esp_err.h"  

/**  
 * @brief 初始化传感器管理器  
 *   
 * - 初始化与DHT22传感器通信的GPIO引脚。  
 * - 向命令分发中心注册 "sensor" 命令处理器。  
 *   
 * @return esp_err_t ESP_OK 成功, 其他为失败.  
 */  
esp_err_t dht22_sensor_init(void);  

#endif 