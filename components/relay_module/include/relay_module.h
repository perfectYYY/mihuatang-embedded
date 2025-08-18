#ifndef RELAY_MODULE_H  
#define RELAY_MODULE_H  

#include "esp_err.h"  

#ifdef __cplusplus  
extern "C" {  
#endif  

/**  
 * @brief 初始化压缩机继电器模块  
 *   
 * 该函数会配置GPIO，并向命令分发器注册 "relay" 命令。  
 * 模块初始化后，即可通过UART等方式发送 "relay:on", "relay:off",   
 * "relay:toggle" 来控制继电器。  
 *   
 * @return esp_err_t   
 *      - ESP_OK: 初始化成功  
 *      - ESP_FAIL: 初始化失败  
 */  
esp_err_t relay_module_init(void);  
void relay_set_state_steam(int8_t state);  



#ifdef __cplusplus  
}  
#endif  

#endif // RELAY_MODULE_H  