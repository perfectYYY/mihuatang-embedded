#ifndef STEPPER_MOTOR_MODULE_H
#define STEPPER_MOTOR_MODULE_H

#include "esp_err.h"

/**
 * @file stepper_motor_module.h
 * @brief 步进电机驱动的电磁阀控制模块
 * 
 * 该模块专门用于控制一个具有固定行程的步进电机阀门。
 * 模块内部会跟踪阀门的绝对位置，以防止超出物理限制。
 *
 * - 0步: 阀门完全关闭
 * - 7步: 阀门完全打开
 */


// 定义阀门的物理行程
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

/**
 * @brief 获取阀门当前的位置 (步数)。
 *
 * @return int 当前的步数 (范围从 0 到 7)。
 */
int stepper_motor_get_current_position(void);

void stepper_motor_direction(stepper_motordirection_t direction, int steps);



#endif // STEPPER_MOTOR_MODULE_H