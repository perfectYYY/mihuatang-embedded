#ifndef DS18B20_MANAGER_H  
#define DS18B20_MANAGER_H  

#include "esp_err.h"  

/**  
 * @brief 初始化 DS18B20 管理器  
 *   
 * - 初始化与DS18B20通信的GPIO引脚。  
 * - 搜索总线上的DS18B20设备。  
 * - 向命令分发中心注册 "ds18b20" 命令处理器。  
 *   
 * @return esp_err_t ESP_OK 成功, 其他为失败.  
 */  
esp_err_t ds18b20_manager_init(void);  

#endif // DS18B20_MANAGER_H  