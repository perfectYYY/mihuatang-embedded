#ifndef LED_CONTROLLER_H  
#define LED_CONTROLLER_H  

#include "esp_err.h"  

// 定义 LED 的引脚和亮灭电平  
#define LED_CONTROLLER_GPIO_PIN     2  
#define LED_CONTROLLER_ON_LEVEL     1  
#define LED_CONTROLLER_OFF_LEVEL    0  

/**  
 * @brief Initializes the LED controller.  
 * This sets up the GPIO and registers the command handler with the UART service.  
 */  
esp_err_t led_controller_init(void);  

#endif // LED_CONTROLLER_H  