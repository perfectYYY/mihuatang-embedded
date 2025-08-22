#include "ds18b20_manager.h"
#include "esp_log.h"
#include <string.h>
#include <stdio.h>

#include "onewire_bus.h"
#include "ds18b20.h"
#include "driver/gpio.h"
#include "command_dispatcher.h"
#include "uart_service.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"

static const char *TAG = "DS18B20_MANAGER";
#define FAILURE_THRESHOLD 5
#define RECOVERY_CHECK_INTERVAL_MS 30000

typedef enum {
    SENSOR_1,
    SENSOR_2,
    SENSOR_3,
    SENSOR_COUNT
} sensor_id_t;

typedef struct {
    const char* name;
    const gpio_num_t pin;
} known_sensor_t;

static const known_sensor_t known_sensors[SENSOR_COUNT] = {
    [SENSOR_1] = {"sensor_1", GPIO_NUM_39},
    [SENSOR_2] = {"sensor_2", GPIO_NUM_2},
    [SENSOR_3] = {"sensor_3", GPIO_NUM_5},
};

static onewire_bus_handle_t bus_handles[SENSOR_COUNT] = {NULL};
static ds18b20_device_handle_t ds18b20_devices[SENSOR_COUNT] = {NULL};
static int consecutive_failures[SENSOR_COUNT] = {0};
static TimerHandle_t recovery_timer_handle = NULL;
static SemaphoreHandle_t ds18b20_mutex = NULL;

static void ds18b20_command_handler(const char *command, size_t len);
static esp_err_t ds18b20_init_single_device(sensor_id_t id);
static void recovery_timer_callback(TimerHandle_t xTimer);
static void start_recovery_mode_if_needed(void);
static void stop_recovery_mode_if_all_ok(void);

static esp_err_t ds18b20_init_single_device(sensor_id_t id)
{
    if (id >= SENSOR_COUNT) return ESP_ERR_INVALID_ARG;

    if (ds18b20_devices[id]) {
        ds18b20_del_device(ds18b20_devices[id]);
        ds18b20_devices[id] = NULL;
    }
    if (bus_handles[id]) {
        onewire_bus_del(bus_handles[id]);
        bus_handles[id] = NULL;
    }

    onewire_bus_config_t bus_config = {.bus_gpio_num = known_sensors[id].pin};
    onewire_bus_rmt_config_t rmt_config = {.max_rx_bytes = 10};

    if (onewire_new_bus_rmt(&bus_config, &rmt_config, &bus_handles[id]) != ESP_OK) {
        ESP_LOGE(TAG, "传感器 '%s' (GPIO %d) 创建总线失败", known_sensors[id].name, known_sensors[id].pin);
        return ESP_FAIL;
    }

    onewire_device_iter_handle_t iter = NULL;
    onewire_device_t device_info;
    onewire_new_device_iter(bus_handles[id], &iter);
    
    esp_err_t result = ESP_ERR_NOT_FOUND;
    if (onewire_device_iter_get_next(iter, &device_info) == ESP_OK) {
        ds18b20_config_t ds_cfg = {};
        if (ds18b20_new_device(&device_info, &ds_cfg, &ds18b20_devices[id]) == ESP_OK) {
            result = ESP_OK;
        }
    }
    onewire_del_device_iter(iter);

    if (result != ESP_OK) {
        onewire_bus_del(bus_handles[id]);
        bus_handles[id] = NULL;
    }
    
    return result;
}

static void recovery_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGI(TAG, "恢复模式: 尝试重新初始化离线设备...");
    if (xSemaphoreTake(ds18b20_mutex, 0) == pdTRUE) {
        int online_count = 0;
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (ds18b20_devices[i] == NULL) {
                if (ds18b20_init_single_device((sensor_id_t)i) == ESP_OK) {
                    ESP_LOGI(TAG, "传感器 '%s' 已重新连接!", known_sensors[i].name);
                    online_count++;
                }
            } else {
                online_count++;
            }
        }

        if (online_count == SENSOR_COUNT) {
            ESP_LOGI(TAG, "所有设备均已在线，停止恢复模式。");
            stop_recovery_mode_if_all_ok();
        }
        xSemaphoreGive(ds18b20_mutex);
    }
}

static void start_recovery_mode_if_needed(void)
{
    if (recovery_timer_handle && xTimerIsTimerActive(recovery_timer_handle) == pdFALSE) {
        ESP_LOGI(TAG, "检测到离线设备，启动恢复模式...");
        xTimerStart(recovery_timer_handle, 0);
    }
}

static void stop_recovery_mode_if_all_ok(void)
{
     if (recovery_timer_handle && xTimerIsTimerActive(recovery_timer_handle) != pdFALSE) {
        xTimerStop(recovery_timer_handle, 0);
        ESP_LOGI(TAG, "所有设备恢复在线，停止恢复定时器。");
     }
}

esp_err_t ds18b20_manager_init(void)
{
    ds18b20_mutex = xSemaphoreCreateMutex();
    if (ds18b20_mutex == NULL) return ESP_FAIL;

    recovery_timer_handle = xTimerCreate("ds18b20_recovery", pdMS_TO_TICKS(RECOVERY_CHECK_INTERVAL_MS), pdTRUE, NULL, recovery_timer_callback);
    if (recovery_timer_handle == NULL) return ESP_FAIL;

    ESP_LOGI(TAG, "正在初始化 DS18B20 管理器 (多总线模式)...");
    
    int online_count = 0;
    xSemaphoreTake(ds18b20_mutex, portMAX_DELAY);
    for (int i = 0; i < SENSOR_COUNT; i++) {
        if (ds18b20_init_single_device((sensor_id_t)i) == ESP_OK) {
            ESP_LOGI(TAG, "传感器 '%s' (GPIO %d) 初始化成功。", known_sensors[i].name, known_sensors[i].pin);
            online_count++;
        } else {
            ESP_LOGE(TAG, "传感器 '%s' (GPIO %d) 初始化失败。", known_sensors[i].name, known_sensors[i].pin);
        }
        vTaskDelay(pdMS_TO_TICKS(100)); 
    }
    
    ESP_LOGI(TAG, "DS18B20 管理器初始化完成, %d/%d 个设备在线。", online_count, SENSOR_COUNT);
    command_dispatcher_register("ds18b20", ds18b20_command_handler);
    
    if (online_count < SENSOR_COUNT) {
        start_recovery_mode_if_needed();
    }
    xSemaphoreGive(ds18b20_mutex);
    
    return ESP_OK;
}

static void ds18b20_command_handler(const char *command, size_t len)
{
    const char *sub_command = command + strlen("ds18b20:");
    char status_buffer[64];
    
    if (strncmp(sub_command, "get_temp:", 9) == 0) {
        char sensor_name[16];
        if (sscanf(sub_command + 9, "%15s", sensor_name) != 1) return;

        if (xSemaphoreTake(ds18b20_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:BUSY");
            uart_service_send_line(status_buffer);
            return;
        }

        sensor_id_t target_id = -1;
        for (int i = 0; i < SENSOR_COUNT; i++) {
            if (strcmp(sensor_name, known_sensors[i].name) == 0) {
                target_id = (sensor_id_t)i;
                break;
            }
        }

        if (target_id == -1) {
             snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:UNKNOWN_NAME:%s", sensor_name);
             uart_service_send_line(status_buffer);
             xSemaphoreGive(ds18b20_mutex);
             return;
        }

        if (ds18b20_devices[target_id] == NULL) {
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:OFFLINE:%s", sensor_name);
            uart_service_send_line(status_buffer);
            start_recovery_mode_if_needed();
            xSemaphoreGive(ds18b20_mutex);
            return;
        }

        ds18b20_device_handle_t current_device = ds18b20_devices[target_id];
        float temperature = 0;

        esp_err_t ret = ds18b20_trigger_temperature_conversion(current_device);
        if (ret == ESP_OK) {
            vTaskDelay(pdMS_TO_TICKS(800));
            ret = ds18b20_get_temperature(current_device, &temperature);
        }

        if (ret == ESP_OK) {
            consecutive_failures[target_id] = 0;
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_TEMP:%s:%.2f", sensor_name, temperature);
        } else {
            consecutive_failures[target_id]++;
            snprintf(status_buffer, sizeof(status_buffer), "STATUS:DS18B20_ERROR:READ_FAIL:%s", sensor_name);
        }

        uart_service_send_line(status_buffer);

        if (consecutive_failures[target_id] >= FAILURE_THRESHOLD) {
            ESP_LOGE(TAG, "传感器 '%s' 连续失败 %d 次, 认定已断开。", sensor_name, FAILURE_THRESHOLD);
            ds18b20_del_device(ds18b20_devices[target_id]);
            ds18b20_devices[target_id] = NULL;
            onewire_bus_del(bus_handles[target_id]);
            bus_handles[target_id] = NULL;
            start_recovery_mode_if_needed();
        }
        
        xSemaphoreGive(ds18b20_mutex);
    }
}