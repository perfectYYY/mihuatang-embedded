#include "shake_motor_module.h"
#include "esp_log.h"  
#include "driver/gpio.h"
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"  
#include <string.h>  

#include "command_dispatcher.h"  
#include "uart_service.h"  

#define SHAKE_MOTOR_GPIO_IN1 35
#define SHAKE_MOTOR_GPIO_IN2 34
#define SHAKE_MOTOR_GPIO_PWM 33
#define SHAKE_MOTOR_PWM_FREQ_HZ       5000
#define SHAKE_MOTOR_PWM_RESOLUTION    LEDC_TIMER_8_BIT
#define SHAKE_MOTOR_LEDC_SPEED_MODE   LEDC_LOW_SPEED_MODE
#define SHAKE_MOTOR_LEDC_TIMER        LEDC_TIMER_1
#define SHAKE_MOTOR_LEDC_CHANNEL      LEDC_CHANNEL_1
// --- 模块内部状态 ---  
static const char *TAG = "SHAKE_MOTOR_MODULE";
static bool s_is_initialized = false;
static TaskHandle_t s_pulse_task_handle = NULL; 

static void vibration_command_handler(const char *command, size_t len);
static void pulse_task(void *pvParameters);
