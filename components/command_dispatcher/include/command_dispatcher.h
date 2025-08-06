#ifndef COMMAND_DISPATCHER_H  
#define COMMAND_DISPATCHER_H  

#include <stddef.h>  
#include "esp_err.h"  

/**  
 * @brief 命令处理函数的标准原型 (函数指针类型)  
 *  
 * 所有希望接收转发命令的模块，其处理函数都必须符合这个格式。  
 * @param command 完整的命令字符串 (例如 "led:on" 或者是一个JSON字符串)  
 * @param len     命令字符串的长度  
 */  
typedef void (*command_handler_t)(const char *command, size_t len);  

/**  
 * @brief 初始化命令分发器服务  
 *  
 * 在系统启动时调用一次，用于清空和准备命令注册表。  
 * @return esp_err_t 总是返回 ESP_OK  
 */  
esp_err_t command_dispatcher_init(void);  

/**  
 * @brief 注册一个命令处理器  
 *  
 * 各个业务模块（如 led_controller, motor_controller）在初始化时调用此函数，  
 * 将自己能处理的命令前缀和对应的处理函数告诉分发中心。  
 *  
 * @param command_prefix 命令前缀 (例如 "led", "motor", "system")  
 * @param handler       当收到此前缀的命令时，要调用的处理函数  
 * @return esp_err_t 成功返回 ESP_OK, 如果注册表满了或前缀已存在则返回错误  
 */  
esp_err_t command_dispatcher_register(const char *command_prefix, command_handler_t handler);  

/**  
 * @brief 分发一个收到的命令  
 *  
 * 这个函数由消息的源头（如 UART 消息处理器或 MQTT 消息处理器）调用。  
 * 它会根据命令的前缀，查找注册表，并将完整的命令字符串转发给正确的处理器。  
 *  
 * @param full_command  完整的命令字符串  
 * @param len           字符串长度  
 */  
void command_dispatcher_forward(const char *full_command, size_t len);  

#endif // COMMAND_DISPATCHER_H  