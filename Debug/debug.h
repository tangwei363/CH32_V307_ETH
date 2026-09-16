/********************************** (C) COPYRIGHT  *******************************
* File Name          : debug.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2021/06/06
* Description        : This file contains all the functions prototypes for UART
*                      Printf , Delay functions.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __DEBUG_H
#define __DEBUG_H

#ifdef __cplusplus
 extern "C" {
#endif

#include "stdio.h"
#include "ch32v30x.h"


/* 日志模块 开关，注释掉将关闭日志输出 */
#define _BSP_DEBUG                     //  日志模块 设备的基本信息
#define _BSP_WCH_DEBUG                 //  日志模块 网口链接的sock信息
#define _UART_DEBUG                    //  日志模块 PLC串口数据 信息
#define _ETHERNET_DEBUG                //  日志模块 网口数据信息

#define _TABLES_DEBUG                  //  日志模块 MC协议寄存器转PLC地址的信息
#define _MELSEC_FX_DEBUG               //  日志模块 MC协议
#define _TRANSMISSION_DEBUG            //  日志模块 透传原始数据信息
// #define _FX_ACCLOG_DEBUG               //  日志模块 登录记录 
// #define  _SNTP_DEBUG_                  //  日志模块 校时sntp 记录 

/* UART Printf Definition */
#define DEBUG_UART1    1
#define DEBUG_UART2    2
#define DEBUG_UART3    3

/* DEBUG UATR Definition */
#ifndef DEBUG
#define DEBUG   DEBUG_UART1
#endif

/* SDI Printf Definition */
#define SDI_PR_CLOSE   0
#define SDI_PR_OPEN    1

#ifndef SDI_PRINT
#define SDI_PRINT   SDI_PR_CLOSE
#endif

/* BOARD test debug message printout enable */
 
// 日志级别定义
#define LOG_LEVEL_NONE    0   // 关闭所有日志
#define LOG_LEVEL_FATAL   1   // 仅致命错误
#define LOG_LEVEL_ERROR   2   // 错误及以上
#define LOG_LEVEL_WARN    3   // 警告及以上
#define LOG_LEVEL_INFO    4   // 信息及以上
#define LOG_LEVEL_DEBUG   5   // 所有日志

// 默认日志级别（可在运行时修改）
#ifndef LOG_LEVEL
#define LOG_LEVEL          LOG_LEVEL_DEBUG
#endif

// 日志宏开关（编译时控制）
#define LOG_FATAL_ENABLE   1
#define LOG_ERR_ENABLE     1
#define LOG_WARN_ENABLE    1
#define LOG_INFO_ENABLE    1
#define LOG_DEBUG_ENABLE   1

// 日志缓冲区使用 s_uart1_tx_buf (UART1_TX_BUF_SIZE)
#define LOG_MAX_OUTPUT     128  // 单条日志最大输出长度

#ifdef _BSP_DEBUG
    #define BSP_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define BSP_DEBUG(format, ...)
#endif
  
#ifdef _TRANSMISSION_DEBUG
    #define TRANSMISSION_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define TRANSMISSION_DEBUG(format, ...)
#endif

// 声明日志格式化函数（在 .c 文件中实现）
void log_output_with_level(const char *level, const char *func, int line, const char *format, ...);

// 声明日志级别设置函数（运行时动态控制）
void log_set_level(uint8_t level);

uint8_t log_get_level(void);

#if LOG_FATAL_ENABLE
#define log_fatal(format, ...) \
    do { \
        if(LOG_LEVEL >= LOG_LEVEL_FATAL) { \
            log_output_with_level("FATAL", __func__, __LINE__, format "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
#else
  #define log_fatal(format, ...)  ((void)0)
#endif

#if LOG_ERR_ENABLE
#define log_err(format, ...) \
    do { \
        if(LOG_LEVEL >= LOG_LEVEL_ERROR) { \
            log_output_with_level("ERR", __func__, __LINE__, format "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
#else
  #define log_err(format, ...)  ((void)0)
#endif

#if LOG_WARN_ENABLE
#define log_warn(format, ...) \
    do { \
        if(LOG_LEVEL >= LOG_LEVEL_WARN) { \
            log_output_with_level("WARN", __func__, __LINE__, format "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
#else
  #define log_warn(format, ...)  ((void)0)
#endif

#if LOG_INFO_ENABLE
#define log_info(format, ...) \
    do { \
        if(LOG_LEVEL >= LOG_LEVEL_INFO) { \
            log_output_with_level("INFO", __func__, __LINE__, format "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
#else
  #define log_info(format, ...)  ((void)0)
#endif

#if LOG_DEBUG_ENABLE
#define log_debug(format, ...) \
    do { \
        if(LOG_LEVEL >= LOG_LEVEL_DEBUG) { \
            log_output_with_level("DEBUG", __func__, __LINE__, format "\r\n", ##__VA_ARGS__); \
        } \
    } while (0)
#else
  #define log_debug(format, ...)  ((void)0)
#endif

void Delay_Init(void);
void Delay_Us (uint32_t n);
void Delay_Ms (uint32_t n);
void USART_Printf_Init(uint32_t baudrate);
void SDI_Printf_Enable(void);

#ifdef __cplusplus
}
#endif

#endif 



