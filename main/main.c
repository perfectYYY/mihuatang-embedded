/*
WiFi断开原因码对照表：
| 错误码（宏/数值）           | 英文说明                        | 中文说明             |
|----------------------------|----------------------------------|----------------------|
| WIFI_REASON_UNSPECIFIED (1)         | Unspecified                      | 未知原因             |
| WIFI_REASON_AUTH_EXPIRE (2)         | Authentication Expired           | 认证超时             |
| WIFI_REASON_AUTH_LEAVE (3)          | Auth Leave                       | 认证离开             |
| WIFI_REASON_ASSOC_EXPIRE (4)        | Association Expired              | 关联超时             |
| WIFI_REASON_ASSOC_TOOMANY (5)       | Too Many Associations            | 关联设备过多         |
| WIFI_REASON_NOT_AUTHED (6)          | Not Authenticated                | 未认证               |
| WIFI_REASON_NOT_ASSOCED (7)         | Not Associated                   | 未关联               |
| WIFI_REASON_AUTH_FAIL (8)           | Authentication Failed            | 认证失败             |
| WIFI_REASON_ASSOC_FAIL (9)          | Association Failed               | 关联失败             |
| WIFI_REASON_HANDSHAKE_TIMEOUT (15)  | Handshake Timeout                | 四次握手超时         |
| WIFI_REASON_CONNECTION_FAIL (17)    | Connection Failed                | 连接失败             |
| WIFI_REASON_BEACON_TIMEOUT (200)    | Beacon Timeout                   | Beacon超时           |
| WIFI_REASON_NO_AP_FOUND (201)       | No AP Found                      | 未找到AP             |
| WIFI_REASON_AUTH_FAIL (202)         | Authentication Failed            | 认证失败             |
| WIFI_REASON_ASSOC_FAIL (203)        | Association Failed               | 关联失败             |
| WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT (204) | 4-Way Handshake Timeout    | 4路握手超时          |
| WIFI_REASON_IE_INVALID (205)        | Information Element is invalid   | 信息元素无效         |
*/
#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "led_strip.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include <time.h>
#include "esp_mac.h"
#include "esp_timer.h" 
#include "driver/mcpwm_prelude.h"
#include "relay_module.h"  
#include "relay_module_two.h"
#include "dc_motor_control.h"
#include "stepper_motor_module.h"  
#include "steam_valve_module.h"  
#include "uart_service.h"
#include "led_controller.h"
#include "command_dispatcher.h" 
#include "fan_controller.h"
#include "dht22_sensor.h"
#include "ds18b20_manager.h" 
#include "water_level_sensor_module.h"
#include "function_controller.h"
#include "compressor_control.h"
#include "shake_motor_module.h"

#define DEVICE_NAME      "衣物护理机CareProP1"
#define DEVICE_TYPE      "CareProP1"
#define LED_GPIO 48
#define WIFI_SSID "helloiip"
#define WIFI_PASS "20210928MYH"
#define MQTT_BROKER_URI "mqtt://broker.emqx.io:1883"
#define WIFI_MAX_RECONNECT 10

static const char *TAG = "MiHuaTang";   
static esp_mqtt_client_handle_t mqtt_client = NULL;
static bool mqtt_connected = false;
static int wifi_reconnect_count = 0;
char device_sn[32] = {0};

// 函数声明
static void mqtt_app_start(void);
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data);
static void send_log_to_broker(const char *log);
static void log_task(void *pvParameters);
void get_device_sn();
static void wifi_init_sta(void);

// WiFi事件处理
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                             int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT) {
        if (event_id == WIFI_EVENT_STA_START) {
            ESP_LOGI(TAG, "WiFi station started");
            esp_wifi_connect();
        } else if (event_id == WIFI_EVENT_STA_DISCONNECTED) {
            wifi_event_sta_disconnected_t* event = (wifi_event_sta_disconnected_t*) event_data;
            ESP_LOGE(TAG, "WiFi disconnected, reason: %d", event->reason);

            mqtt_connected = false;

            if (wifi_reconnect_count < WIFI_MAX_RECONNECT) {
                wifi_reconnect_count++;
                ESP_LOGW(TAG, "WiFi重连中... 当前次数: %d", wifi_reconnect_count);
                vTaskDelay(pdMS_TO_TICKS(5000));  // 等待5秒后重连
                esp_wifi_connect();
            } else {
                ESP_LOGE(TAG, "WiFi重连已达上限(%d次),不再重试", WIFI_MAX_RECONNECT);
            }
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        wifi_reconnect_count = 0;
        vTaskDelay(pdMS_TO_TICKS(1000));
        mqtt_app_start();
    }
}

// 改进的WiFi初始化
static void wifi_init_sta(void)
{
    ESP_LOGI(TAG, "Initializing WiFi...");
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_t *sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL, NULL));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
            .password = WIFI_PASS,
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
            .pmf_cfg = {
                .capable = true,
                .required = false
            },
        },
    };
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    
    ESP_LOGI(TAG, "Starting WiFi...");
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialization finished, SSID: %s", WIFI_SSID);
}

// 启动MQTT客户端
static void mqtt_app_start(void)
{
    if (mqtt_client != NULL) {
        esp_mqtt_client_destroy(mqtt_client);
    }

    const esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = MQTT_BROKER_URI,
        .credentials.client_id = device_sn,
    };

    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    if (mqtt_client == NULL) {
        ESP_LOGE(TAG, "MQTT客户端初始化失败");
        return;
    }

    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_err_t err = esp_mqtt_client_start(mqtt_client);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "MQTT客户端启动失败: %s", esp_err_to_name(err));
        esp_mqtt_client_destroy(mqtt_client);
        mqtt_client = NULL;
    }
}

// MQTT事件处理
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        mqtt_connected = true;
        ESP_LOGI(TAG, "MQTT已连接");
        {
            char topic[64];
            snprintf(topic, sizeof(topic), "device/%s/message", device_sn);
            esp_mqtt_client_subscribe(mqtt_client, topic, 0);
        }
        break;
    case MQTT_EVENT_DISCONNECTED:
        mqtt_connected = false;
        ESP_LOGI(TAG, "MQTT已断开连接");
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT连接错误，请检查服务器地址和端口");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
            ESP_LOGI(TAG, "Last error code reported from esp-tls: 0x%x", event->error_handle->esp_tls_last_esp_err);
            ESP_LOGI(TAG, "Last tls stack error number: 0x%x", event->error_handle->esp_tls_stack_err);
        } else if (event->error_handle->error_type == MQTT_ERROR_TYPE_CONNECTION_REFUSED) {
            ESP_LOGI(TAG, "Connection refused error: 0x%x", event->error_handle->connect_return_code);
        } else {
            ESP_LOGW(TAG, "Unknown error type: 0x%x", event->error_handle->error_type);
        }
        break;
    case MQTT_EVENT_DATA: {
        char *payload = strndup(event->data, event->data_len);
        if (payload == NULL) {
            ESP_LOGE(TAG, "Failed to allocate memory for payload");
            break; 
        }

        ESP_LOGI(TAG, "收到MQTT消息: %s", payload);

        cJSON *json = cJSON_Parse(payload);
        if (json) {
            const cJSON *cmd = cJSON_GetObjectItem(json, "command");
            if (cmd && cJSON_IsString(cmd) && (cmd->valuestring != NULL)) {
                if (strcmp(cmd->valuestring, "on") == 0) {
                    const char* led_cmd = "led:on";
                    command_dispatcher_forward(led_cmd, strlen(led_cmd));
                    ESP_LOGI(TAG, "通过命令分发系统发送LED开启命令");
                } else if (strcmp(cmd->valuestring, "off") == 0) {
                    const char* led_cmd = "led:off";
                    command_dispatcher_forward(led_cmd, strlen(led_cmd));
                    ESP_LOGI(TAG, "通过命令分发系统发送LED关闭命令");
                } else if (strcmp(cmd->valuestring, "status") == 0) {
                    char status_payload[256];
                    snprintf(status_payload, sizeof(status_payload),
                        "{\"device_sn\":\"%s\",\"device_name\":\"%s\",\"device_type\":\"%s\",\"timestamp\":%lld}",
                        device_sn, DEVICE_NAME, DEVICE_TYPE, esp_timer_get_time() / 1000);
                    char status_topic[64];
                    snprintf(status_topic, sizeof(status_topic), "device/%s/status", device_sn);
                    esp_mqtt_client_publish(mqtt_client, status_topic, status_payload, 0, 1, 0);
                } else {
                    ESP_LOGW(TAG, "未知命令: %s", cmd->valuestring);
                }
            } else {
                ESP_LOGW(TAG, "MQTT消息格式错误，缺少 'command' 字段");
            }
            cJSON_Delete(json);
        } else {
             ESP_LOGW(TAG, "Failed to parse JSON: %s", payload);
        }
        free(payload);
        break; // <-- THIS BREAK IS NOW CORRECTLY PLACED
    }
    case MQTT_EVENT_SUBSCRIBED:
    case MQTT_EVENT_UNSUBSCRIBED:
    case MQTT_EVENT_PUBLISHED:
    case MQTT_EVENT_BEFORE_CONNECT:
    case MQTT_EVENT_DELETED:
    default:
        ESP_LOGI(TAG, "Other MQTT event id: %d", (int)event_id);
        break;
    }
}


static void send_log_to_broker(const char *log)
{
    if (mqtt_connected && mqtt_client) {
        char log_topic[64];
        snprintf(log_topic, sizeof(log_topic), "device/%s/log", device_sn);
        ESP_LOGI(TAG, "MQTT发送: topic=%s, payload=%s", log_topic, log);
        esp_mqtt_client_publish(mqtt_client, log_topic, log, 0, 0, 0);
    }
}

static void log_task(void *pvParameters)
{
    const char* featureSeason = "Spring";
    const char* featureStyle = "Casual";
    const char* featureThickness = "Medium";
    const char* featureTexture = "Cotton";
    const char* featurePattern = "Striped";
    const char* featureWeight = "Light";

    while (1) {
        char log_msg[256];
        snprintf(log_msg, sizeof(log_msg),
            "{\"featureSeason\":\"%s\",\"featureStyle\":\"%s\",\"featureThickness\":\"%s\",\"featureTexture\":\"%s\",\"featurePattern\":\"%s\",\"featureWeight\":\"%s\"}",
            featureSeason, featureStyle, featureThickness, featureTexture, featurePattern, featureWeight
        );
        send_log_to_broker(log_msg);
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}

void get_device_sn()
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_sn, sizeof(device_sn), "SN%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}




static void handle_uart_message(const char *data, size_t len)
{
    ESP_LOGI(TAG, "UART消息入口收到原始数据: '%.*s', 准备转发给分发中心...", len, data);
    command_dispatcher_forward(data, len);
}

void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ESP_ERROR_CHECK(command_dispatcher_init());
    ESP_LOGI(TAG, "Initializing local communication services...");
    ESP_ERROR_CHECK(uart_service_init());  
    uart_service_register_command_handler(handle_uart_message);
    ESP_ERROR_CHECK(led_controller_init());
    ESP_ERROR_CHECK(fan_controller_init());
    //ESP_ERROR_CHECK(dht22_sensor_init());
    //ESP_ERROR_CHECK(ds18b20_manager_init());
    ESP_ERROR_CHECK(dc_motor_module_init());
    ESP_ERROR_CHECK(relay_module_init());
    ESP_ERROR_CHECK(relay_module_two_init()); 
    ESP_ERROR_CHECK(steam_valve_module_init());
    ESP_ERROR_CHECK(stepper_motor_module_init());
    ESP_ERROR_CHECK(water_level_sensor_module_init());
    ESP_ERROR_CHECK(function_controller_init());
    ESP_ERROR_CHECK(compressor_module_init());
    ESP_ERROR_CHECK(shake_motor_module_init());
    ESP_LOGI(TAG, "Local services are running.");

    get_device_sn();
    ESP_LOGI(TAG, "设备SN: %s", device_sn);

    
    // wifi_init_sta();

    // xTaskCreate(log_task, "log_task", 4096, NULL, 5, NULL);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}
