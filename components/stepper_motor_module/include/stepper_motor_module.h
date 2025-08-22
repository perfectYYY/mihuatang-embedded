#ifndef STEPPER_MOTOR_MODULE_H
#define STEPPER_MOTOR_MODULE_H

#include "esp_err.h"


#define VALVE_MAX_STEPS 7  // 完全打开所需步数
#define VALVE_MIN_STEPS 0  // 完全关闭的位置

esp_err_t stepper_motor_module_init(void);


int stepper_motor_get_current_position(void);

#endif 