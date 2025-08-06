/**  
 * @file dc_motor_control.h  
 * @brief 直流电机（蒸汽电机）控制模块 (命令驱动架构)  
 * @author MiHuaTang (Refactored by Gemini)  
 * @date 2024  
 */  

#ifndef DC_MOTOR_CONTROL_H  
#define DC_MOTOR_CONTROL_H  

#include "esp_err.h"  

#ifdef __cplusplus  
extern "C" {  
#endif  

/**  
 * @brief 初始化直流电机（蒸汽电机）模块  
 *  
 * 该函数将完成所有硬件初始化（GPIO, LEDC/PWM），并向命令分发器注册 "motor" 命令前缀。  
 * 外部模块（如main.c, UI等）应仅调用此函数。  
 *  
 * @return   
 *      - ESP_OK: 初始化成功  
 *      - ESP_FAIL: 初始化失败  
 */  
esp_err_t dc_motor_module_init(void);  

#ifdef __cplusplus  
}  
#endif  

#endif // DC_MOTOR_CONTROL_H  