

#ifndef DC_MOTOR_CONTROL_H  
#define DC_MOTOR_CONTROL_H  

#include "esp_err.h"  

typedef enum {
    MOTOR_DIR_STOP = 0,
    MOTOR_DIR_FORWARD,
    MOTOR_DIR_REVERSE,
    MOTOR_DIR_BRAKE
} motor_direction_t;

esp_err_t dc_motor_module_init(void);  
void set_steam_motor(motor_direction_t direction, uint8_t speed);   // 设置蒸汽电机的方向和速度 0 停止 1 前进 2 后退 3 刹车

#endif