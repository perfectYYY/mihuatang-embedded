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

// --- 配置定义 ---
#define COMPRESSOR_UART_PORT      (CONFIG_COMPRESSOR_UART_PORT_NUM)
#define COMPRESSOR_TX_PIN         (CONFIG_COMPRESSOR_TX_PIN)
#define COMPRESSOR_RX_PIN         (CONFIG_COMPRESSOR_RX_PIN)

#define UART_BAUD_RATE            9600
#define UART_BUF_SIZE             (256)
#define COMM_TASK_STACK_SIZE      (3072)
#define COMM_INTERVAL_MS          (500)
#define COMPRESSOR_SLAVE_ADDRESS  (1)

// --- Modbus 寄存器地址 ---
#define REG_CONTROL_SPEED_START_STOP  (0x6000)

// --- 静态变量 ---
static const char *TAG = "COMPRESSOR_MODULE";
static bool s_is_initialized = false;
static TaskHandle_t s_comm_task_handle = NULL;
static SemaphoreHandle_t s_target_status_mutex = NULL; 

static struct {
    bool run_command;
    uint16_t target_speed_rpm;
} s_target_status = { .run_command = false, .target_speed_rpm = 2000 }; 

static struct {
    bool is_running;
    uint16_t current_speed_rpm;
    uint16_t fault_code;
    float bus_voltage;
    float motor_current;
} s_current_status = {0};

// --- 函数声明 ---
static void compressor_command_handler(const char *command, size_t len);
static void compressor_comm_task(void *pvParameters);
static uint16_t calculate_crc16(const uint8_t *data, uint16_t length);
static void send_modbus_write_command(uint16_t reg_addr, uint16_t value);

// --- CRC16-Modbus 校验表 ---
static const uint8_t crc_hi_table[] = {
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01,
    0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0,
    0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01,
    0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81, 0x40, 0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41,
    0x00, 0xC1, 0x81, 0x40, 0x01, 0xC0, 0x80, 0x41, 0x01, 0xC0, 0x80, 0x41, 0x00, 0xC1, 0x81,
    0x40
};
static const uint8_t crc_lo_table[] = {
    0x00, 0xC0, 0xC1, 0x01, 0xC3, 0x03, 0x02, 0xC2, 0xC6, 0x06, 0x07, 0xC7, 0x05, 0xC5, 0xC4,
    0x04, 0xCC, 0x0C, 0x0D, 0xCD, 0x0F, 0xCF, 0xCE, 0x0E, 0x0A, 0xCA, 0xCB, 0x0B, 0xC9, 0x09,
    0x08, 0xC8, 0xD8, 0x18, 0x19, 0xD9, 0x1B, 0xDB, 0xDA, 0x1A, 0x1E, 0xDE, 0xDF, 0x1F, 0xDD,
    0x1D, 0x1C, 0xDC, 0x14, 0xD4, 0xD5, 0x15, 0xD7, 0x17, 0x16, 0xD6, 0xD2, 0x12, 0x13, 0xD3,
    0x11, 0xD1, 0xD0, 0x10, 0xF0, 0x30, 0x31, 0xF1, 0x33, 0xF3, 0xF2, 0x32, 0x36, 0xF6, 0xF7,
    0x37, 0xF5, 0x35, 0x34, 0xF4, 0x3C, 0xFC, 0xFD, 0x3D, 0xFF, 0x3F, 0x3E, 0xFE, 0xFA, 0x3A,
    0x3B, 0xFB, 0x39, 0xF9, 0xF8, 0x38, 0x28, 0xE8, 0xE9, 0x29, 0xEB, 0x2B, 0x2A, 0xEA, 0xEE,
    0x2E, 0x2F, 0xEF, 0x2D, 0xED, 0xEC, 0x2C, 0xE4, 0x24, 0x25, 0xE5, 0x27, 0xE7, 0xE6, 0x26,
    0x22, 0xE2, 0xE3, 0x23, 0xE1, 0x21, 0x20, 0xE0, 0xA0, 0x60, 0x61, 0xA1, 0x63, 0xA3, 0xA2,
    0x62, 0x66, 0xA6, 0xA7, 0x67, 0xA5, 0x65, 0x64, 0xA4, 0x6C, 0xAC, 0xAD, 0x6D, 0xAF, 0x6F,
    0x6E, 0xAE, 0xAA, 0x6A, 0x6B, 0xAB, 0x69, 0xA9, 0xA8, 0x68, 0x78, 0xB8, 0xB9, 0x79, 0xBB,
    0x7B, 0x7A, 0xBA, 0xBE, 0x7E, 0x7F, 0xBF, 0x7D, 0xBD, 0xBC, 0x7C, 0xB4, 0x74, 0x75, 0xB5,
    0x77, 0xB7, 0xB6, 0x76, 0x72, 0xB2, 0xB3, 0x73, 0xB1, 0x71, 0x70, 0xB0, 0x50, 0x90, 0x91,
    0x51, 0x93, 0x53, 0x52, 0x92, 0x96, 0x56, 0x57, 0x97, 0x55, 0x95, 0x94, 0x54, 0x9C, 0x5C,
    0x5D, 0x9D, 0x5F, 0x9F, 0x9E, 0x5E, 0x5A, 0x9A, 0x9B, 0x5B, 0x99, 0x59, 0x58, 0x98, 0x88,
    0x48, 0x49, 0x89, 0x4B, 0x8B, 0x8A, 0x4A, 0x4E, 0x8E, 0x8F, 0x4F, 0x8D, 0x4D, 0x4C, 0x8C,
    0x44, 0x84, 0x85, 0x45, 0x87, 0x47, 0x46, 0x86, 0x82, 0x42, 0x43, 0x83, 0x41, 0x81, 0x80,
    0x40
};

// CRC计算函数
static uint16_t calculate_crc16(const uint8_t *data, uint16_t length) {
    uint8_t crc_hi = 0xFF;
    uint8_t crc_lo = 0xFF;
    uint16_t i;

    while (length--) {
        i = crc_lo ^ *data++;
        crc_lo = crc_hi ^ crc_hi_table[i];
        crc_hi = crc_lo_table[i];
    }
    return (crc_hi << 8 | crc_lo);
}

// 命令处理函数，与您的蓝本一致
static void compressor_command_handler(const char *command, size_t len)
{
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化，无法处理命令");
        return;
    }

    const char *sub_command = command + strlen("compressor:");
    uint16_t speed_val = 0;

    if (strncmp(sub_command, "start", strlen("start")) == 0) {
        if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
            s_target_status.run_command = true;
            xSemaphoreGive(s_target_status_mutex);
            ESP_LOGI(TAG, "收到启动指令");
        } else {
            ESP_LOGE(TAG, "获取互斥锁超时");
        }
    } else if (strncmp(sub_command, "stop", strlen("stop")) == 0) {
         if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
            s_target_status.run_command = false;
            xSemaphoreGive(s_target_status_mutex); 
            ESP_LOGI(TAG, "收到停止指令");
        } else {
            ESP_LOGE(TAG, "获取互斥锁超时");
        }
    } else if (sscanf(sub_command, "speed:%hu", &speed_val) == 1) {
        if (speed_val > 4800) speed_val = 4800;
        if (speed_val < 2000) speed_val = 2000;
         if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) { 
            s_target_status.target_speed_rpm = speed_val;
            xSemaphoreGive(s_target_status_mutex); 
            ESP_LOGI(TAG, "收到速度设定指令: %d RPM", speed_val);
        } else {
            ESP_LOGE(TAG, "获取互斥锁超时");
        }
    } else {
        ESP_LOGW(TAG, "未知的压缩机子命令: %s", sub_command);
    }
}

// 初始化函数，与您的蓝本一致
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
    
    ESP_ERROR_CHECK(uart_driver_install(COMPRESSOR_UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(COMPRESSOR_UART_PORT, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(COMPRESSOR_UART_PORT, COMPRESSOR_TX_PIN, COMPRESSOR_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));
    
    s_target_status_mutex = xSemaphoreCreateMutex();
    if (s_target_status_mutex == NULL) {
        ESP_LOGE(TAG, "创建互斥锁失败");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        return ESP_FAIL;
    }

    ESP_ERROR_CHECK(command_dispatcher_register("compressor", compressor_command_handler));
    
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

// 反初始化函数，与您的蓝本一致
esp_err_t compressor_module_deinit(void)
{
    if (!s_is_initialized) {
        return ESP_OK;
    }
    if (s_comm_task_handle) {
        vTaskDelete(s_comm_task_handle);
        s_comm_task_handle = NULL;
    }
    uart_driver_delete(COMPRESSOR_UART_PORT);
    vSemaphoreDelete(s_target_status_mutex);
    s_is_initialized = false;
    ESP_LOGI(TAG, "压缩机模块已结束");
    return ESP_OK; 
}

// **核心通信任务，已融合Modbus逻辑**
static void compressor_comm_task(void *pvParameters)
{
    uint8_t rx_buffer[UART_BUF_SIZE];
    static bool last_run_state = false;
    static uint16_t last_speed = 2000; 

    for (;;)
    {
        bool current_run_command;
        uint16_t current_target_speed_rpm;

        if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            current_run_command = s_target_status.run_command;
            current_target_speed_rpm = s_target_status.target_speed_rpm;
            xSemaphoreGive(s_target_status_mutex);
        } else {
            ESP_LOGE(TAG, "获取互斥锁超时");
            vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
            continue;
        }

        // 2. 如果状态改变，则发送Modbus指令
        if (current_run_command != last_run_state || current_target_speed_rpm != last_speed) {
            uint16_t value_to_write = current_run_command ? current_target_speed_rpm : 0;
            
            send_modbus_write_command(REG_CONTROL_SPEED_START_STOP, value_to_write);
            
            last_run_state = current_run_command;
            last_speed = current_target_speed_rpm;
            
            ESP_LOGI(TAG, "已发送指令: %s, 速度: %d RPM", current_run_command ? "启动" : "停止", current_target_speed_rpm);
        }

        // 3. 尝试读取返回数据，与您的蓝本一致
        int len = uart_read_bytes(COMPRESSOR_UART_PORT, rx_buffer, UART_BUF_SIZE, pdMS_TO_TICKS(200));
        if (len > 0) {
            ESP_LOGI(TAG, "收到 %d 字节原始数据:", len);
            ESP_LOG_BUFFER_HEX(TAG, rx_buffer, len);
            
            // TODO: 在这里添加对Modbus响应帧的解析
            // 例如，写操作的响应帧通常是请求帧的复述，长度为8字节
            // 读操作的响应帧需要根据功能码和字节数来解析
        }
        
        vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
    }
}

// **新的辅助函数：构建并发送Modbus写指令帧**
static void send_modbus_write_command(uint16_t reg_addr, uint16_t value)
{
    uint8_t frame[8]; // 写单个寄存器的指令帧固定为8字节

    // 1. 填充帧内容 (高位在前)
    frame[0] = COMPRESSOR_SLAVE_ADDRESS;  // 从机地址
    frame[1] = 0x06;                      // 功能码: 写单个寄存器
    frame[2] = (reg_addr >> 8) & 0xFF;    // 寄存器地址高位
    frame[3] = reg_addr & 0xFF;           // 寄存器地址低位
    frame[4] = (value >> 8) & 0xFF;       // 数据高位
    frame[5] = value & 0xFF;              // 数据低位

    // 2. 计算前6个字节的CRC
    uint16_t crc = calculate_crc16(frame, 6);
    frame[6] = crc & 0xFF;                // CRC低位
    frame[7] = (crc >> 8) & 0xFF;         // CRC高位

    // 3. 通过UART发送帧
    uart_write_bytes(COMPRESSOR_UART_PORT, (const char *)frame, 8);
}
