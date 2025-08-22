
#include "stepper_motor_module.h" 
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>
#include "command_dispatcher.h"
#include "uart_service.h"

// GPIO引脚定义
#define STEPPER_IN1_GPIO         9 
#define STEPPER_IN2_GPIO         10  
#define STEPPER_IN3_GPIO         11  
#define STEPPER_IN4_GPIO         12
#define STEPPER_DEFAULT_DELAY_MS 10 //步进延迟

#define STEPPER_COMMAND_PREFIX "stepper"


static const char *TAG = "VALVE_STEPPER_MODULE";

static bool s_is_initialized = false;

static int s_valve_current_steps = 0; 

// 步进序列 (四相四拍)
static const uint8_t step_sequence_forward[4][4] = { {1,0,0,1}, {0,1,0,1}, {0,1,1,0}, {1,0,1,0} };
static const uint8_t step_sequence_reverse[4][4] = { {1,0,1,0}, {0,1,1,0}, {0,1,0,1}, {1,0,0,1} };


void stepper_command_handler(const char *command, size_t len);
static void move_to_absolute_position(int target_steps);
static void apply_step(const uint8_t step_pattern[4]);
static void turn_off_coils(void);
static void send_status_update(void);


esp_err_t stepper_motor_module_init(void) {
    if (s_is_initialized) {
        ESP_LOGW(TAG, "步进电机阀门模块已初始化，跳过。");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "正在初始化步进电机阀门模块...");

    // 1. 配置GPIO
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << STEPPER_IN1_GPIO) | (1ULL << STEPPER_IN2_GPIO) | 
                        (1ULL << STEPPER_IN3_GPIO) | (1ULL << STEPPER_IN4_GPIO),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "GPIO配置失败: %s", esp_err_to_name(ret));
        return ret;
    }
    turn_off_coils();

    // 2. 注册命令处理器
    ret = command_dispatcher_register(STEPPER_COMMAND_PREFIX, stepper_command_handler);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "注册 '%s' 命令失败!", STEPPER_COMMAND_PREFIX);
        return ret;
    }
    
    // 3. 初始化状态变量
    s_valve_current_steps = 0; // 假设启动时阀门处于关闭状态
    s_is_initialized = true;
    ESP_LOGI(TAG, "步进电机阀门模块初始化完成, 当前位置: %d (关闭)", s_valve_current_steps);
    return ESP_OK;
}


int stepper_motor_get_current_position(void) {
    return s_valve_current_steps;
}

void stepper_command_handler(const char *command, size_t len) {
    if (!s_is_initialized) {
        ESP_LOGE(TAG, "模块未初始化，无法处理命令: %s", command);
        return;
    }
    const char *sub_command = command + strlen(STEPPER_COMMAND_PREFIX);
    if (*sub_command == ':') sub_command++;

    if (strncmp(sub_command, "open", strlen("open")) == 0) {
        // move_to_absolute_position(VALVE_MAX_STEPS);
        stepper_motor_direction(OPEN, 40);
    } 
    else if (strncmp(sub_command, "close", strlen("close")) == 0) {
        // move_to_absolute_position(VALVE_MIN_STEPS);
        stepper_motor_direction(CLOSE, 40);
    }
    else if (strncmp(sub_command, "status", strlen("status")) == 0) {
        send_status_update();
    }
    else {
        ESP_LOGW(TAG, "未知的阀门命令: %s. (可用: open, close, status)", sub_command);
    }
}

static void move_to_absolute_position(int target_steps) {
    // 步骤1: 边界检查与规范化
    if (target_steps > VALVE_MAX_STEPS) target_steps = VALVE_MAX_STEPS;
    if (target_steps < VALVE_MIN_STEPS) target_steps = VALVE_MIN_STEPS;

    ESP_LOGI(TAG, "请求移动到位置: %d, 当前位置: %d", target_steps, s_valve_current_steps);

    if (target_steps == s_valve_current_steps) {
        ESP_LOGI(TAG, "已在目标位置，无需移动。");
        send_status_update();
        return;
    }
    // 步骤2: 判断方向和计算步数
    bool is_forward = target_steps > s_valve_current_steps;
    int steps_to_move = abs(target_steps - s_valve_current_steps);
    // 步骤3: 执行步进
    for (int i = 0; i < steps_to_move; i++) {
        if (is_forward) {
            apply_step(step_sequence_forward[s_valve_current_steps % 4]);
            s_valve_current_steps++;
        } else {
            apply_step(step_sequence_reverse[(s_valve_current_steps - 1) % 4]);
            s_valve_current_steps--;
        }
        vTaskDelay(pdMS_TO_TICKS(STEPPER_DEFAULT_DELAY_MS)); 
    }
    // 步骤4: 脱机并报告状态
    turn_off_coils();
    ESP_LOGI(TAG, "移动完成, 当前位置: %d", s_valve_current_steps);
    send_status_update();
}


static void apply_step(const uint8_t step_pattern[4]) {
    gpio_set_level(STEPPER_IN1_GPIO, step_pattern[0]);
    gpio_set_level(STEPPER_IN2_GPIO, step_pattern[1]);
    gpio_set_level(STEPPER_IN3_GPIO, step_pattern[2]);
    gpio_set_level(STEPPER_IN4_GPIO, step_pattern[3]);
}


static void turn_off_coils() {
    gpio_set_level(STEPPER_IN1_GPIO, 0);
    gpio_set_level(STEPPER_IN2_GPIO, 0);
    gpio_set_level(STEPPER_IN3_GPIO, 0);
    gpio_set_level(STEPPER_IN4_GPIO, 0);
}


static void send_status_update(void) {
    char status_str[128];
    char position_desc[32];

    if (s_valve_current_steps == VALVE_MIN_STEPS) {
        strcpy(position_desc, "CLOSED");
    } else if (s_valve_current_steps == VALVE_MAX_STEPS) {
        strcpy(position_desc, "OPEN");
    } else {
        sprintf(position_desc, "TRANSIT");
    }

    snprintf(status_str, sizeof(status_str), 
             "STATUS:VALVE,Position:%d,State:%s",
             s_valve_current_steps, position_desc);
    
    uart_service_send_line(status_str);
}

void stepper_motor_direction(stepper_motordirection_t direction, int steps) {
    if (!s_is_initialized) {
        ESP_LOGW(TAG, "模块未初始化，无法前进。");
        return;
    }
    if (steps <= 0) {
        vTaskDelay(pdMS_TO_TICKS(STEPPER_DEFAULT_DELAY_MS));
        return;
    }
    // 步骤3: 执行步进
    if(direction != OPEN && direction != CLOSE) {
        ESP_LOGE(TAG, "无效的方向: %d. 只能是 OPEN 或 CLOSE.", direction);
        turn_off_coils();
        return;
    }
    else if(direction == OPEN) {
        // 打开阀门
        ESP_LOGE(TAG, "打开阀门");
        for(int i = 0; i < steps; i++) {
            apply_step(step_sequence_forward[i % 4]);
            vTaskDelay(pdMS_TO_TICKS(STEPPER_DEFAULT_DELAY_MS));
        }
    }
    else if(direction == CLOSE) {
        // 关闭阀门
        ESP_LOGE(TAG, "关闭阀门");
        for(int i = 0; i < steps; i++) {
            apply_step(step_sequence_reverse[i % 4]);
            vTaskDelay(pdMS_TO_TICKS(STEPPER_DEFAULT_DELAY_MS));
        }
    }
}

