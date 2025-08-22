#ifndef STEPPER_MOTOR_MODULE_H
#define STEPPER_MOTOR_MODULE_H

#include "esp_err.h"


#define VALVE_MAX_STEPS 7  // 完全打开所需步数
#define VALVE_MIN_STEPS 0  // 完全关闭的位置


typedef enum {
    STOP = 0,
    OPEN = 1,
    CLOSE = 2, 
} stepper_motordirection_t;

/**
 * @brief 初始化步进电机阀门模块。
 * 
 * - 配置IN1-IN4对应的GPIO引脚为输出模式。
 * - 注册 "stepper" 命令到命令分发器。
 * - 模块会假设阀门在启动时处于关闭位置 (0步)。
 * 
 * @return esp_err_t ESP_OK 表示成功, 其他表示失败.
 */
esp_err_t stepper_motor_module_init(void);


int stepper_motor_get_current_position(void);

void stepper_motor_direction(stepper_motordirection_t direction, int steps);



#endif // STEPPER_MOTOR_MODULE_H
