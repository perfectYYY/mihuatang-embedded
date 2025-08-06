
#include "uart_service.h"  
#include "driver/uart.h"  
#include "esp_log.h"  
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"  
#include <string.h>  
#include "esp_check.h"
#include "sdkconfig.h"

static const char *TAG = "UART_SERVICE";  

#define UART_PORT      (CONFIG_UART_SERVICE_PORT_NUM)  
#define UART_TX_PIN    (CONFIG_UART_SERVICE_TX_PIN)  
#define UART_RX_PIN    (CONFIG_UART_SERVICE_RX_PIN)  
#define UART_BAUD_RATE (CONFIG_UART_SERVICE_BAUD_RATE)  
#define UART_BUF_SIZE  (1024)  
#define UART_TASK_STACK_SIZE 3072 

static uart_service_handler_t s_command_handler = NULL; 
static uart_service_handler_t s_status_handler = NULL;  
static const char* STATUS_PREFIX = "STATUS:";
static size_t STATUS_PREFIX_LEN = 7; // "STATUS:"的长度

// 后台任务，用于接收和处理 UART 数据  
static void uart_service_task(void *pvParameters)  
{  
    uint8_t* data = (uint8_t*) malloc(UART_BUF_SIZE);  
    if (data == NULL) {  
        ESP_LOGE(TAG, "Failed to allocate memory for UART buffer");  
        vTaskDelete(NULL);  
        return;  
    }  

    ESP_LOGI(TAG, "UART service task started");  

    while (1) {  
        int len = uart_read_bytes(UART_PORT, data, (UART_BUF_SIZE - 1), pdMS_TO_TICKS(20));  
        if (len > 0) {  
            data[len] = '\0';  
            
            if (strncmp((const char *)data, STATUS_PREFIX, STATUS_PREFIX_LEN) == 0) {
                if (s_status_handler) {
                    s_status_handler((const char *)data, len);
                } else {
                    ESP_LOGD(TAG, "Received status update, but no status handler is registered.");
                }
            } else {
                if (s_command_handler) {
                    s_command_handler((const char *)data, len);
                } else {
                    ESP_LOGD(TAG, "Received command, but no command handler is registered.");
                }
            }
        }  
    }  
    free(data);  
    vTaskDelete(NULL);  
}  


esp_err_t uart_service_init(void)  
{  
    uart_config_t uart_config = {  
        .baud_rate = UART_BAUD_RATE,  
        .data_bits = UART_DATA_8_BITS,  
        .parity    = UART_PARITY_DISABLE,  
        .stop_bits = UART_STOP_BITS_1,  
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,  
        .source_clk = UART_SCLK_DEFAULT,  
    };  

    ESP_LOGI(TAG, "Initializing UART on port %d", UART_PORT);  
    ESP_RETURN_ON_ERROR(uart_driver_install(UART_PORT, UART_BUF_SIZE * 2, 0, 0, NULL, 0), TAG, "driver install failed");  
    ESP_RETURN_ON_ERROR(uart_param_config(UART_PORT, &uart_config), TAG, "param config failed");  
    ESP_RETURN_ON_ERROR(uart_set_pin(UART_PORT, UART_TX_PIN, UART_RX_PIN, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE), TAG, "set pin failed");  

    xTaskCreate(uart_service_task, "uart_service_task", UART_TASK_STACK_SIZE, NULL, 10, NULL);  

    return ESP_OK;  
}  


void uart_service_register_command_handler(uart_service_handler_t handler)  
{  
    s_command_handler = handler;  
}  

void uart_service_register_status_handler(uart_service_handler_t handler)
{
    s_status_handler = handler;
}

int uart_service_send_line(const char *data)  
{  
    if (data == NULL) {  
        return -1;  
    }  

    char line_buffer[UART_BUF_SIZE];  
    int len = snprintf(line_buffer, sizeof(line_buffer), "%s\n", data);  

    const int bytes_sent = uart_write_bytes(UART_PORT, line_buffer, len);  
    
    if (bytes_sent != len) {  
        ESP_LOGW(TAG, "Error sending data. Expected %d, sent %d.", len, bytes_sent);  
    }
    return bytes_sent;  
}