#include "compressor_control.h"
#include "driver/uart.h"
#include "esp_log.h"
#include <string.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h" // 添加互斥锁头文件

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
} s_target_status = { .run_command = false, .target_speed_rpm = 0 };

static struct {
    bool is_running;
    uint16_t current_speed_rpm;
    uint16_t fault_code;
} s_current_status = {0};

static void compressor_command_handler(const char *command, size_t len);
static uint16_t crc16_modbus(const uint8_t *data, uint16_t length);
static void send_modbus_frame(const uint8_t *data, uint16_t length);
static void compressor_comm_task(void *pvParameters);


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
        if (speed_val > 4500) {
            speed_val = 4500;
        }
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

esp_err_t compressor_module_init(void)
{
    if (s_is_initialized) {
        ESP_LOGW(TAG, "压缩机模块已初始化，无需重复。");
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
        ESP_LOGE(TAG, "创建互斥锁失败!");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        return ESP_FAIL;
    }

    ret = command_dispatcher_register("compressor", compressor_command_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 'compressor' 命令失败!");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        vSemaphoreDelete(s_target_status_mutex);
        return ret;
    }

    // 3. 创建后台通讯任务
    if (xTaskCreate(compressor_comm_task, "comp_comm_task", COMM_TASK_STACK_SIZE, NULL, 5, &s_comm_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "创建通讯任务失败!");
        uart_driver_delete(COMPRESSOR_UART_PORT);
        vSemaphoreDelete(s_target_status_mutex); 
        return ESP_FAIL;
    }

    s_is_initialized = true;
    ESP_LOGI(TAG, "压缩机模块初始化完成。");
    return ESP_OK;
}

/**
 * @brief 反初始化压缩机控制模块
 */
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
    ESP_LOGI(TAG, "压缩机模块已反初始化。");
    return ESP_OK;
}

static void compressor_comm_task(void *pvParameters)
{
    uint8_t rx_buffer[UART_BUF_SIZE];
    static bool last_run_state = false;
    static uint16_t last_speed = 0;

    for (;;)
    {
        bool current_run_command;
        uint16_t current_target_speed_rpm;

        // 获取目标状态的快照，并释放互斥锁
        if (xSemaphoreTake(s_target_status_mutex, pdMS_TO_TICKS(100)) == pdTRUE) {
            current_run_command = s_target_status.run_command;
            current_target_speed_rpm = s_target_status.target_speed_rpm;
            xSemaphoreGive(s_target_status_mutex);
        } else {
            ESP_LOGE(TAG, "获取互斥锁超时，跳过本次循环");
            vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
            continue;
        }

        if (current_run_command != last_run_state || current_target_speed_rpm != last_speed) {
            if (current_run_command) {
                uint8_t frame_run[] = {COMPRESSOR_SLAVE_ADDRESS, 0x06, 0x20, 0x01, 0x00, 0x01};
                send_modbus_frame(frame_run, sizeof(frame_run));
                vTaskDelay(pdMS_TO_TICKS(100));

                uint8_t frame_speed[] = {COMPRESSOR_SLAVE_ADDRESS, 0x06, 0x20, 0x02, (current_target_speed_rpm >> 8), (current_target_speed_rpm & 0xFF)};
                send_modbus_frame(frame_speed, sizeof(frame_speed));
            } else {
                uint8_t frame_stop[] = {COMPRESSOR_SLAVE_ADDRESS, 0x06, 0x20, 0x01, 0x00, 0x02};
                send_modbus_frame(frame_stop, sizeof(frame_stop));
            }
            last_run_state = current_run_command;
            last_speed = current_target_speed_rpm;
            vTaskDelay(pdMS_TO_TICKS(100)); 
        }

        uint8_t frame_req[] = {COMPRESSOR_SLAVE_ADDRESS, 0x03, 0x20, 0x03, 0x00, 0x06};
        send_modbus_frame(frame_req, sizeof(frame_req));

        int len = uart_read_bytes(COMPRESSOR_UART_PORT, rx_buffer, UART_BUF_SIZE, pdMS_TO_TICKS(200));
        if (len > 0 && rx_buffer[0] == COMPRESSOR_SLAVE_ADDRESS && rx_buffer[1] == 0x03) {
            if (len == (3 + 6 * 2 + 2)) {
                uint16_t crc_calc = crc16_modbus(rx_buffer, len - 2);
                uint16_t crc_recv = (rx_buffer[len - 1] << 8) | rx_buffer[len - 2];
                if (crc_calc == crc_recv) {
                    s_current_status.is_running = ((rx_buffer[3] << 8) | rx_buffer[4]) == 1;
                    s_current_status.current_speed_rpm = (rx_buffer[7] << 8) | rx_buffer[8];
                    s_current_status.fault_code = (rx_buffer[15] << 8) | rx_buffer[16];
                    ESP_LOGD(TAG, "状态更新: 运行=%d, 速度=%d RPM, 故障码=%d", s_current_status.is_running, s_current_status.current_speed_rpm, s_current_status.fault_code);
                } else {
                     ESP_LOGW(TAG, "收到数据CRC校验失败!");
                }
            }
        }
        
        vTaskDelay(pdMS_TO_TICKS(COMM_INTERVAL_MS));
    }
}

//附加CRC并发送Modbus帧
static void send_modbus_frame(const uint8_t *data, uint16_t length)
{
    uint8_t frame_to_send[length + 2];
    memcpy(frame_to_send, data, length);
    uint16_t crc = crc16_modbus(frame_to_send, length);
    frame_to_send[length] = crc & 0xFF;  
    frame_to_send[length + 1] = crc >> 8;   

    uart_write_bytes(COMPRESSOR_UART_PORT, (const char *)frame_to_send, length + 2);
}

//计算Modbus RTU的CRC16校验码

static uint16_t crc16_modbus(const uint8_t *data, uint16_t length)
{
    uint16_t crc = 0xFFFF;
    for (uint16_t i = 0; i < length; i++) {
        crc ^= (uint16_t)data[i];
        for (int j = 0; j < 8; j++) {
            if (crc & 0x0001) {
                crc = (crc >> 1) ^ 0xA001;
            } else {
                crc = (crc >> 1);
            }
        }
    }
    return crc;
}
