#include "ds18b20_manager.h"  
#include "esp_log.h"  
#include <string.h>  

#include "onewire_bus.h" 
#include "ds18b20.h"  
#include "driver/gpio.h"  
#include "command_dispatcher.h"  
#include "uart_service.h"  
#include "freertos/FreeRTOS.h"  
#include "freertos/task.h"  

static const char *TAG = "DS18B20_MANAGER";  

#define DS18B20_GPIO_PIN GPIO_NUM_39

static onewire_bus_handle_t bus_handle = NULL;  
static ds18b20_device_handle_t ds18b20_device = NULL;  

static void ds18b20_command_handler(const char *command, size_t len);  

esp_err_t ds18b20_manager_init(void) {  
    ESP_LOGI(TAG, "正在初始化 DS18B20 管理器 (on GPIO %d)...", DS18B20_GPIO_PIN);  

    onewire_bus_config_t bus_config = {  
        .bus_gpio_num = DS18B20_GPIO_PIN,  
    };  
    onewire_bus_rmt_config_t rmt_config = {  
        .max_rx_bytes = 10,  
    };  
    ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_handle));  

    onewire_device_iter_handle_t iter = NULL;  
    onewire_device_t next_onewire_device;  
    esp_err_t search_result = ESP_OK;  

    ESP_ERROR_CHECK(onewire_new_device_iter(bus_handle, &iter));  
    ESP_LOGI(TAG, "Device iterator created, start searching...");  
    do {  
        search_result = onewire_device_iter_get_next(iter, &next_onewire_device);  
        if (search_result == ESP_OK) {  
            ds18b20_config_t ds_cfg = {};  
            if (ds18b20_new_device(&next_onewire_device, &ds_cfg, &ds18b20_device) == ESP_OK) {  
                ESP_LOGI(TAG, "Found a DS18B20 device successfully");  
                break;  
            } else {  
                ESP_LOGI(TAG, "Found an unknown device, address: %016llX", next_onewire_device.address);  
            }  
        }  
    } while (search_result != ESP_ERR_NOT_FOUND);  
    ESP_ERROR_CHECK(onewire_del_device_iter(iter));  
    ESP_LOGI(TAG, "Searching done");  

    if (ds18b20_device == NULL) {  
        ESP_LOGE(TAG, "在总线上未找到任何 DS18B20 设备!");  
        return ESP_ERR_NOT_FOUND;  
    }  

    esp_err_t ret = command_dispatcher_register("ds18b20", ds18b20_command_handler);  
    if (ret != ESP_OK) {  
        ESP_LOGE(TAG, "注册 'ds18b20' 命令失败!");  
        return ret;  
    }  
    
    ESP_LOGI(TAG, "DS18B20 管理器初始化完成。");  
    return ESP_OK;  
}  

static void ds18b20_command_handler(const char *command, size_t len) {  
    const char *sub_command = command + strlen("ds18b20:");  

    if (strncmp(sub_command, "get_temp", 8) == 0) {  
        float temperature = 0;  
        char status_buffer[64];  

        if (ds18b20_device == NULL) {  
            ESP_LOGE(TAG, "错误：DS18B20设备句柄无效!");  
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:NO_DEVICE");  
            uart_service_send_line(status_buffer);  
            return;  
        }  

        esp_err_t ret = ds18b20_trigger_temperature_conversion(ds18b20_device);  
        if (ret != ESP_OK) {  
            ESP_LOGE(TAG, "触发温度转换失败: %s", esp_err_to_name(ret));  
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:CONVERT_FAIL");  
            uart_service_send_line(status_buffer);  
            return;  
        }  
        
        vTaskDelay(pdMS_TO_TICKS(800));  
        
        ret = ds18b20_get_temperature(ds18b20_device, &temperature);  
        if (ret == ESP_OK) {  
            ESP_LOGI(TAG, "读取成功 -> 温度: %.2f°C", temperature);  
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_TEMP:%.2f", temperature);  
        } else {  
            ESP_LOGE(TAG, "读取温度失败! 错误码: %s", esp_err_to_name(ret));  
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:READ_FAIL");  
        }  
        
        ESP_LOGI(TAG, "发送 UART 消息: %s", status_buffer);  
        uart_service_send_line(status_buffer);  
    }  
}  