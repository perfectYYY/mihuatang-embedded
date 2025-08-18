#include "compressor_control.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" 

#include "command_dispatcher.h"
#include "sdkconfig.h"

#define COMPRESSOR_UART_PORT      (CONFIG_COMPRESSOR_UART_PORT_NUM)
#define COMPRESSOR_TX_PIN         (CONFIG_COMPRESSOR_TX_PIN)
#define COMPRESSOR_RX_PIN         (CONFIG_COMPRESSOR_RX_PIN)

#define UART_BAUD_RATE            9600
#define UART_BUF_SIZE             (256)
#define COMM_TASK_STACK_SIZE      (3072)
#define COMM_INTERVAL_MS          (500)
#define COMPRESSOR_SLAVE_ADDRESS  (1)   

static const char *TAG = "COMPRESSOR_MODULE";
static bool s_is_initialized = false;
static TaskHandle_t s_comm_task_handle = NULL;
static SemaphoreHandle_t s_target_status_mutex = NULL; 

static struct {
    bool run_command;
    uint16_t target_speed_rpm;
} s_target_status = { .run_command = false, .target_speed_rpm = 1800 }; 

static struct {
    bool is_running;
    uint16_t current_speed_rpm;
    uint16_t fault_code;
    float bus_voltage;
    float motor_current;
} s_current_status = {0};


static void compressor_comm_task(void *pvParameters);
static void send_power_on_command(void);


esp_err_t compressor_module_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "压缩机模块已初始化");
        return ESP_OK;
    }
    ESP_LOGI(TAG, "正在初始化压缩机模块 (UART%d, TX:%d, RX:%d)...",
             COMPRESSOR_UART_PORT, COMPRESSOR_TX_PIN, COMPRESSOR_RX_PIN);
    uart_config_t uart_config = {
        .baud_rate = UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    esp_err_t ret;
    ret = uart_driver_install(COMPRESSOR_UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART驱动安装失败: %s", esp_err_to_name(ret)); return ret; }
    ret = uart_param_config(COMPRESSOR_UART_PORT, &uart_config);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART参数配置失败: %s", esp_err_to_name(ret)); return ret; }
    ret = uart_set_pin(COMPRESSOR_UART_PORT, COMPRESSOR_TX_PIN, COMPRESSOR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) { ESP_LOGE(TAG, "UART引脚设置失败: %s", esp_err_to_name(ret)); return ret; }
    s_target_status_mutex = xSemaphoreCreateMutex();
    if (s_target_status_mutex == NULL) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        return ESP_FAIL;
    }
    // ret = command_dispatcher_register("compressor", compressor_command_handler);
    // if (ret != ESP_OK) {
    //     ESP_LOGE(TAG, "注册compressor失败");
    //     uart_driver_delete(COMPRESSOR_UART_PORT);
    //     vSemaphoreDelete(s_target_status_mutex);
    //     return ret;
    // }
    if (xTaskCreate(compressor_comm_task, "comp_comm_task", COMM_TASK_STACK_SIZE, NULL, 5, &s_comm_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "创建通讯任务失败");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        vSemaphoreDelete(s_target_status_mutex); 
        return ESP_FAIL;
    }
    s_is_initialized = true;
    ESP_LOGI(TAG, "压缩机模块初始化完成");
    return ESP_OK;
}

static void compressor_comm_task(void *pvParameters)
{
    vTaskDelay(pdMS_TO_TICKS(10000));
    while(1)
    {
        send_power_on_command();
        ESP_LOGI(TAG, "压缩机通讯任务运行中...");
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

// 计算校验和（前14字节累加和，取低8位）
static uint8_t calculate_checksum(uint8_t *frame) {
    uint8_t sum = 0;
    for (int i = 0; i < 14; i++) {
        sum += frame[i];
    }
    return sum;
}


// 发送开机指令
static void send_power_on_command() {
    uint8_t tx_frame[16] = {0xAA, 0x00, 0x01,   // 起始码、固定值、开机指令(0x01)
                            0xB8, 0x0B,         // 转速低8位(0xB8)、高8位(0x0B) → 3000RPM
                            0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,  // 填充位
                            0x00, 0x55};        // 校验和（临时值，后续计算）
    
    // 计算并填充校验和
    tx_frame[14] = calculate_checksum(tx_frame);
    
    // 发送指令帧
    uart_write_bytes(COMPRESSOR_UART_PORT, (const char *)tx_frame, 16);
    ESP_LOGI(TAG,"已发送开机指令，目标转速3000RPM\n");
}

// static void compressor_command_handler(const char *command, size_t len);
// static uint8_t calculate_checksum_sum8(const uint8_t *data, uint16_t length);
// static void compressor_comm_task(void *pvParameters);

// static void compressor_command_handler(const char *command, size_t len)
// {
//     if (!s_is_initialized) {
//         ESP_LOGE(TAG, "模块未初始化，无法处理命令");
//         return;
//     }

//     const char *sub_command = command + strlen("compressor:");
//     uint16_t speed_val = 0;

//     if (strncmp(sub_command, "start", strlen("start")) == 0) {
//         if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
//             s_target_status.run_command = true;
//             xSemaphoreGive(s_target_status_mutex);
//             ESP_LOGI(TAG, "收到启动指令");
//         } else {
//             ESP_LOGE(TAG, "获取互斥锁超时");
//         }
//     } else if (strncmp(sub_command, "stop", strlen("stop")) == 0) {
//          if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
//             s_target_status.run_command = false;
//             xSemaphoreGive(s_target_status_mutex); 
//             ESP_LOGI(TAG, "收到停止指令");
//         } else {
//             ESP_LOGE(TAG, "获取互斥锁超时");
//         }
//     } else if (sscanf(sub_command, "speed:%hu", &speed_val) == 1) {
//         if (speed_val > 6000) { 
//             speed_val = 6000;
//         }
//         if (speed_val < 1800) {
//             speed_val = 1800;
//         }
//          if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
//             s_target_status.target_speed_rpm = speed_val;
//             xSemaphoreGive(s_target_status_mutex); 
//             ESP_LOGI(TAG, "收到速度设定指令: %d RPM", speed_val);
//         } else {
//             ESP_LOGE(TAG, "获取互斥锁超时");
//         }
//     } else {
//         ESP_LOGW(TAG, "未知的压缩机子命令: %s", sub_command);
//     }
// }

// esp_err_t compressor_module_init(void)
// {
//     if (s_is_initialized) {
//         ESP_LOGW(TAG, "压缩机模块已初始化");
//         return ESP_OK;
//     }
//     ESP_LOGI(TAG, "正在初始化压缩机模块 (UART%d, TX:%d, RX:%d)...",
//              COMPRESSOR_UART_PORT, COMPRESSOR_TX_PIN, COMPRESSOR_RX_PIN);
//     uart_config_t uart_config = {
//         .baud_rate = UART_BAUD_RATE,
//         .data_bits = UART_DATA_8_BITS,
//         .parity    = UART_PARITY_DISABLE,
//         .stop_bits = UART_STOP_BITS_1,
//         .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
//         .source_clk = UART_SCLK_DEFAULT,
//     };
//     esp_err_t ret;
//     ret = uart_driver_install(COMPRESSOR_UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0);
//     if (ret != ESP_OK) { ESP_LOGE(TAG, "UART驱动安装失败: %s", esp_err_to_name(ret)); return ret; }
//     ret = uart_param_config(COMPRESSOR_UART_PORT, &uart_config);
//     if (ret != ESP_OK) { ESP_LOGE(TAG, "UART参数配置失败: %s", esp_err_to_name(ret)); return ret; }
//     ret = uart_set_pin(COMPRESSOR_UART_PORT, COMPRESSOR_TX_PIN, COMPRESSOR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
//     if (ret != ESP_OK) { ESP_LOGE(TAG, "UART引脚设置失败: %s", esp_err_to_name(ret)); return ret; }
//     s_target_status_mutex = xSemaphoreCreateMutex();
//     if (s_target_status_mutex == NULL) {
//         ESP_LOGE(TAG, "创建互斥锁失败");
//         uart_driver_delete(COMPRESSOR_UART_PORT);
//         return ESP_FAIL;
//     }
//     ret = command_dispatcher_register("compressor", compressor_command_handler);
//     if (ret != ESP_OK) {
//         ESP_LOGE(TAG, "注册compressor失败");
//         uart_driver_delete(COMPRESSOR_UART_PORT);
//         vSemaphoreDelete(s_target_status_mutex);
//         return ret;
//     }
//     if (xTaskCreate(compressor_comm_task, "comp_comm_task", COMM_TASK_STACK_SIZE, NULL, 5, &s_comm_task_handle) != pdPASS) {
//         ESP_LOGE(TAG, "创建通讯任务失败");
//         uart_driver_delete(COMPRESSOR_UART_PORT);
//         vSemaphoreDelete(s_target_status_mutex); 
//         return ESP_FAIL;
//     }
//     s_is_initialized = true;
//     ESP_LOGI(TAG, "压缩机模块初始化完成");
//     return ESP_OK;
// }

// esp_err_t compressor_module_deinit(void)
// {
//     if (!s_is_initialized) {
//         return ESP_OK;
//     }
//     if (s_comm_task_handle) {
//         vTaskDelete(s_comm_task_handle);
//         s_comm_task_handle = NULL;
//     }
//     uart_driver_delete(COMPRESSOR_UART_PORT);
//     vSemaphoreDelete(s_target_status_mutex);
//     s_is_initialized = false;
//     ESP_LOGI(TAG, "压缩机模块已结束");
//     return ESP_OK; 
// }


// static void compressor_comm_task(void *pvParameters)
// {
//     vTaskDelay(pdMS_TO_TICKS(10000));
//     while(1)
//     {
//         ESP_LOGI(TAG, "压缩机通讯任务运行中...");
//         vTaskDelay(pdMS_TO_TICKS(1000));
//     }
// }

// // 计算校验和（前14字节累加和，取低8位）
// static uint8_t calculate_checksum(uint8_t *frame) {
//     uint8_t sum = 0;
//     for (int i = 0; i < 14; i++) {
//         sum += frame[i];
//     }
//     return sum;
// }

// static void compressor_comm_task(void *pvParameters)
// {
//     uint8_t rx_buffer[UART_BUF_SIZE];
    
//     static bool last_run_state = false;

//     static uint16_t last_speed = 1800; 

//     for (;;)
//     {
//         bool current_run_command;
//         uint16_t current_target_speed_rpm;

//         if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
//             current_run_command = s_target_status.run_command;
//             current_target_speed_rpm = s_target_status.target_speed_rpm;
//             xSemaphoreGive(s_target_status_mutex);
//         } else {
//             ESP_LOGE(TAG, "获取互斥锁超时");
//             vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
//             continue;
//         }

//         if (current_run_command != last_run_state || current_target_speed_rpm != last_speed) {
//             uint8_t command_frame[16];
            
//             command_frame[0] = 0xAA; 
//             command_frame[1] = 0x00; 
//             command_frame[2] = current_run_command ? 1 : 0;
//             command_frame[3] = current_target_speed_rpm & 0xFF;
//             command_frame[4] = (current_target_speed_rpm >> 8) & 0xFF;
//             memset(&command_frame[5], 0, 9); 

//             command_frame[14] = calculate_checksum_sum8(command_frame, 14);
//             command_frame[15] = 0x55;

//             uart_write_bytes(COMPRESSOR_UART_PORT, (const char *)command_frame, 16);
            
//             last_run_state = current_run_command;
//             last_speed = current_target_speed_rpm;
            
//             ESP_LOGI(TAG, "发送指令: %s, 速度: %d RPM", current_run_command ? "启动" : "停止", current_target_speed_rpm);
//         }

//         int len = uart_read_bytes(COMPRESSOR_UART_PORT, rx_buffer, UART_BUF_SIZE, pdMS_TO_TICKS(200));
//         if (len > 0) {
//             for (int i = 0; i <= len - 16; i++) {
//                 if (rx_buffer[i] == 0xAA && rx_buffer[i+15] == 0x55) {
//                     uint8_t *frame = &rx_buffer[i];
                    
//                     if (frame[1] != COMPRESSOR_SLAVE_ADDRESS) {
//                         continue; 
//                     }

//                     uint8_t checksum_calc = calculate_checksum_sum8(frame, 14);
//                     uint8_t checksum_recv = frame[14];

//                     if (checksum_calc == checksum_recv) {
//                         s_current_status.is_running = (frame[10] == 1);
//                         s_current_status.current_speed_rpm = (frame[3] << 8) | frame[2];
//                         s_current_status.motor_current = ((frame[5] << 8) | frame[4]) * 0.1f;
//                         s_current_status.bus_voltage = ((frame[7] << 8) | frame[6]) * 0.1f;
//                         s_current_status.fault_code = frame[9];
                        
//                         ESP_LOGD(TAG, "状态更新: 运行=%d, 速度=%d RPM, 电压=%.1f V, 电流=%.1f A, 故障码=%d", 
//                                  s_current_status.is_running, s_current_status.current_speed_rpm, 
//                                  s_current_status.bus_voltage, s_current_status.motor_current, s_current_status.fault_code);
//                         break; 
//                     } else {
//                         ESP_LOGW(TAG, "收到数据帧，但校验和失败 (计算值: 0x%02X, 接收值: 0x%02X)", checksum_calc, checksum_recv);
//                     }
//                 }
//             }
//         }
        
//         vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
//     }
// }

// //计算8位累加和校验
// static uint8_t calculate_checksum_sum8(const uint8_t *data, uint16_t length)
// {
//     uint8_t sum = 0;
//     for (uint16_t i = 0; i < length; i++) {
//         sum += data[i];
//     }
//     return sum;
// }
