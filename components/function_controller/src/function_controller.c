#include "function_controller.h" 
#include "esp_log.h"
#include <string.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"   
#include "command_dispatcher.h"
#include "uart_service.h"

#define CONTROLLER_COMMAND_PREFIX "function"
#define WATER_LEVEL_CHECK_INTERVAL_MS 500 

static const char *TAG = "FUNCTION_CONTROLLER";
static bool s_is_initialized = false;

// --- 任务管理 ---
static TaskHandle_t s_steam_monitor_task_handle = NULL; 

// --- 外部组件的命令处理器声明 ---
extern void relay_command_handler(const char *command, size_t len);
extern void fan_command_handler(const char *command, size_t len);
extern void stepper_command_handler(const char *command, size_t len);
extern void motor_command_handler(const char *command, size_t len);
extern void valve_command_handler(const char *command, size_t len);

// extern bool water_level_is_reached(void); // 直接获取水位状态

// --- 本模块的功能函数声明 ---
static void function_command_handler(const char *command, size_t len);
static void execute_drying_sequence(void);
static void start_steam_wrinkle_function(void);
static void stop_steam_wrinkle_function(void);
static void steam_level_monitor_task(void *pvParameters); // FreeRTOS 任务函数

esp_err_t function_controller_init(void) {
    // ... (初始化函数保持不变) ...
    if (s_is_initialized) return ESP_OK;
    ESP_LOGI(TAG, "正在初始化功能控制器...");
    esp_err_t ret = command_dispatcher_register(CONTROLLER_COMMAND_PREFIX, function_command_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 '%s' 命令处理器失败!", CONTROLLER_COMMAND_PREFIX);
        return ret;
    }
    s_is_initialized = true;
    ESP_LOGI(TAG, "功能控制器初始化完成。");
    return ESP_OK;
}

static void function_command_handler(const char *command, size_t len) {
    if (!s_is_initialized) return;

    const char *sub_command = command + strlen(CONTROLLER_COMMAND_PREFIX) + 1;

    if (strncmp(sub_command, "start_drying", strlen("start_drying")) == 0) {
        execute_drying_sequence();
    } 
    else if (strncmp(sub_command, "start_steam", strlen("start_steam")) == 0) {
        start_steam_wrinkle_function();
    }
    // 新增：处理停止命令
    else if (strncmp(sub_command, "stop_steam", strlen("stop_steam")) == 0) {
        stop_steam_wrinkle_function();
    }
    else {
        ESP_LOGW(TAG, "未知的功能命令: %s", sub_command);
    }
}

static void execute_drying_sequence(void) {
    // ... (烘干功能保持不变) ...
    ESP_LOGI(TAG, "===== 开始执行烘干流程 =====");
    char command_buffer[32];
    snprintf(command_buffer, sizeof(command_buffer), "fan:75");
    fan_command_handler(command_buffer, strlen(command_buffer));
    snprintf(command_buffer, sizeof(command_buffer), "relay:on");
    relay_command_handler(command_buffer, strlen(command_buffer));
    snprintf(command_buffer, sizeof(command_buffer), "stepper:open"); 
    stepper_command_handler(command_buffer, strlen(command_buffer));
    ESP_LOGI(TAG, "===== 烘干流程所有启动指令已发出 =====");
    uart_service_send_line("STATUS:FUNCTION_DRYING_STARTED");
}

/**
 * @brief 启动蒸汽除皱功能
 * - 检查是否已在运行
 * - 启动风扇
 * - 创建后台任务来监控和控制水位与加热
 */
static void start_steam_wrinkle_function(void) {
    if (s_steam_monitor_task_handle != NULL) {
        ESP_LOGW(TAG, "蒸汽除皱功能已在运行中，请先停止。");
        uart_service_send_line("ERROR:STEAM_FUNCTION_ALREADY_RUNNING");
        return;
    }

    ESP_LOGI(TAG, "===== 启动蒸汽除皱功能 =====");
    char command_buffer[32];

    // 步骤 1: 开启风扇 (如果需要的话)
    ESP_LOGI(TAG, "步骤: 启动风扇至50%%...");
    snprintf(command_buffer, sizeof(command_buffer), "fan:50");
    fan_command_handler(command_buffer, strlen(command_buffer));

    // 步骤 2: 创建后台监控任务
    xTaskCreate(steam_level_monitor_task,      // 任务函数
                "steam_monitor_task",          // 任务名
                4096,                          // 任务栈大小 (bytes)
                NULL,                          // 传递给任务的参数
                5,                             // 任务优先级
                &s_steam_monitor_task_handle); // 任务句柄

    uart_service_send_line("STATUS:FUNCTION_STEAM_STARTED");
}

/**
 * @brief 停止蒸汽除皱功能
 * - 停止加热（关闭继电器）
 * - 停止加水（关闭电机和电磁阀）
 * - 安全地删除后台监控任务
 */
static void stop_steam_wrinkle_function(void) {
    if (s_steam_monitor_task_handle == NULL) {
        ESP_LOGW(TAG, "蒸汽除皱功能未在运行。");
        uart_service_send_line("ERROR:STEAM_FUNCTION_NOT_RUNNING");
        return;
    }
    
    ESP_LOGI(TAG, "===== 正在停止蒸汽除皱功能 =====");
    char command_buffer[32];

    
    // 步骤 2: 确保水泵停止
    ESP_LOGI(TAG, "步骤: 关闭蒸汽泵电机...");
    snprintf(command_buffer, sizeof(command_buffer), "motor:stop");
    motor_command_handler(command_buffer, strlen(command_buffer));
    
    ESP_LOGI(TAG, "步骤: 关闭蒸汽电磁阀...");
    snprintf(command_buffer, sizeof(command_buffer), "valve:close");
    valve_command_handler(command_buffer, strlen(command_buffer));

    // 步骤 3: 删除监控任务
    ESP_LOGI(TAG, "步骤: 删除后台监控任务...");
    vTaskDelete(s_steam_monitor_task_handle);
    s_steam_monitor_task_handle = NULL; // 将句柄清空，表示任务已停止

    ESP_LOGI(TAG, "===== 蒸汽除皱功能已停止 =====");
    uart_service_send_line("STATUS:FUNCTION_STEAM_STOPPED");
}


/**
 * @brief [FreeRTOS Task] 蒸汽水位监控与控制任务
 *        这是一个闭环控制的核心。
 */
static void steam_level_monitor_task(void *pvParameters) {
    ESP_LOGI(TAG, "后台任务启动：开始监控水位。");
    char command_buffer[32];
    bool is_heating = false; // 初始状态为不加热

    // 任务主循环
    for (;;) {
        // bool level_reached = water_level_is_reached();

        // if (level_reached) {
        //     // --- 水位已满 ---
        //     if (!is_heating) {
        //         ESP_LOGI(TAG, "水位已达到，停止加水并开始加热。");

        //         // 1. 停止加水
        //         snprintf(command_buffer, sizeof(command_buffer), "motor:stop");
        //         motor_command_handler(command_buffer, strlen(command_buffer));
        //         snprintf(command_buffer, sizeof(command_buffer), "valve:close");
        //         valve_command_handler(command_buffer, strlen(command_buffer));

        //         // 2. 开始加热
        //         snprintf(command_buffer, sizeof(command_buffer), "relay2:on");
        //         relay_two_command_handler(command_buffer, strlen(command_buffer));
                
        //         is_heating = true; // 更新状态
        //         uart_service_send_line("STATUS:STEAM_HEATING_ON");
        //     }
        // } else {
        //     // --- 水位不足 ---
        //     if (is_heating) {
        //         ESP_LOGI(TAG, "水位过低，停止加热并开始加水。");

        //         // 1. 停止加热
        //         snprintf(command_buffer, sizeof(command_buffer), "relay2:off");
        //         relay_two_command_handler(command_buffer, strlen(command_buffer));

        //         is_heating = false; // 更新状态
        //         uart_service_send_line("STATUS:STEAM_HEATING_OFF");
        //     }
            
        //     // 2. 开始加水 (持续指令，即使之前已经发过)
        //     snprintf(command_buffer, sizeof(command_buffer), "motor:speed:100");
        //     motor_command_handler(command_buffer, strlen(command_buffer));
        //     snprintf(command_buffer, sizeof(command_buffer), "motor:forward");
        //     motor_command_handler(command_buffer, strlen(command_buffer));
        //     snprintf(command_buffer, sizeof(command_buffer), "valve:open");
        //     valve_command_handler(command_buffer, strlen(command_buffer));
        // }

        // 延时，避免过于频繁地检查，给系统其他任务运行的机会
        vTaskDelay(pdMS_TO_TICKS(WATER_LEVEL_CHECK_INTERVAL_MS));
    }
}
