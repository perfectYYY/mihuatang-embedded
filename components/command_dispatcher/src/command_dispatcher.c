#include "esp_log.h"  
#include <string.h>  
#include "esp_err.h"  
#include "command_dispatcher.h"  
#include "fan_controller.h"

static const char *TAG = "CMD_DISPATCHER";  


#define MAX_COMMAND_HANDLERS 30  

// 这是命令注册表的核心结构  
typedef struct {  
    const char*       prefix;     // 命令前缀, e.g., "led"  
    command_handler_t handler;    // 对应的处理函数  
    size_t            prefix_len; // 缓存前缀长度，避免在循环中反复计算，提高效率  
} command_entry_t;  

// 静态分配的命令注册表  
static command_entry_t s_command_table[MAX_COMMAND_HANDLERS];  
static int s_handler_count = 0; // 当前已注册的处理器数量  

/**  
 * @brief 初始化命令分发器  
 */  
esp_err_t command_dispatcher_init(void) {  
    memset(s_command_table, 0, sizeof(s_command_table));  //注册表
    s_handler_count = 0;  
    ESP_LOGI(TAG, "命令分发中心已初始化，准备接收模块注册...");  
    return ESP_OK;  
}  

/**  
 * @brief 注册命令处理器  
 */  
esp_err_t command_dispatcher_register(const char *command_prefix, command_handler_t handler) {  
    if (s_handler_count >= MAX_COMMAND_HANDLERS) {  
        ESP_LOGE(TAG, "命令注册表已满，无法注册 '%s'", command_prefix);  
        return ESP_ERR_NO_MEM;  
    }  
 
    for (int i = 0; i < s_handler_count; i++) {  
        if (strcmp(s_command_table[i].prefix, command_prefix) == 0) {  
            ESP_LOGE(TAG, "命令前缀 '%s' 已经被注册，请勿重复注册！", command_prefix);  
            return ESP_ERR_INVALID_STATE;  
        }  
    }  

    s_command_table[s_handler_count].prefix = command_prefix;  
    s_command_table[s_handler_count].handler = handler;  
    s_command_table[s_handler_count].prefix_len = strlen(command_prefix);  
    s_handler_count++;  

    ESP_LOGI(TAG, "命令处理器为 '%s' 注册成功！", command_prefix);  
    return ESP_OK;  
}  

/**  
 * @brief 核心分发逻辑  
 */  
void command_dispatcher_forward(const char *full_command, size_t len) {  
    ESP_LOGD(TAG, "收到命令，准备分发: %.*s", len, full_command);  
 
    for (int i = 0; i < s_handler_count; i++) {  
        const command_entry_t* entry = &s_command_table[i];  
        
        // 检查命令是否以此前缀开头，并且后面必须跟着一个分隔符 (我们约定用':')  
        // 例如，命令 "led:on" 匹配前缀 "led"  
        if (len > entry->prefix_len &&   
            strncmp(full_command, entry->prefix, entry->prefix_len) == 0 &&  
            full_command[entry->prefix_len] == ':')   
        {  
            ESP_LOGI(TAG, "命令匹配到前缀 '%s'，转发给对应的处理器。", entry->prefix);  
             
            entry->handler(full_command, len);  
            
            // 假设一条命令只会被一个模块处理，找到后就立即返回  
            return;  
        }  
    }  

    // 如果循环结束都没有找到匹配的处理器  
    ESP_LOGW(TAG, "未找到能处理此命令的模块: '%.*s'", len, full_command);  
}  