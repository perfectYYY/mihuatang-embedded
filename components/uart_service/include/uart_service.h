// uart_service.h

#ifndef UART_SERVICE_H
#define UART_SERVICE_H

#include "esp_err.h"
#include <stddef.h>

// 定义一个通用的回调函数指针类型
typedef void (*uart_service_handler_t)(const char *data, size_t len);

/**
 * @brief 初始化UART服务，安装驱动并启动后台任务。
 */
esp_err_t uart_service_init(void);

/**
 * @brief 注册一个用于处理传入“命令”的回调函数。
 * @note  这通常在主控板上使用，用于接收来自屏幕的控制指令。
 *
 * @param handler 当收到非'STATUS:'开头的消息时被调用的函数。
 */
void uart_service_register_command_handler(uart_service_handler_t handler);

/**
 * @brief 注册一个用于处理传入“状态更新”的回调函数。
 * @note  这通常在屏幕UI端使用，用于接收来自主控板的状态回传。
 *
 * @param handler 当收到以'STATUS:'开头的消息时被调用的函数。
 */
void uart_service_register_status_handler(uart_service_handler_t handler);

/**
 * @brief 通过UART发送一行数据（自动添加换行符）。
 *
 * @param data 要发送的字符串。
 * @return 成功发送的字节数，或-1表示失败。
 */
int uart_service_send_line(const char *data);

#endif // UART_SERVICE_H