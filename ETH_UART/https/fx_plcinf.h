/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_plcinf.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP PLC信息页面头文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __FX_PLCINF_H
#define __FX_PLCINF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* CPU类型定义 */
typedef enum {
    FX_PLC_CPU_FX3U = 0,       /* FX3U */
    FX_PLC_CPU_FX3UC,         /* FX3UC */
    FX_PLC_CPU_UNKNOWN        /* 未知 */
} fx_plcinf_cpu_type_t;

/* 存储器类型定义 */
typedef enum {
    FX_PLC_MEM_RAM = 0,       /* RAM */
    FX_PLC_MEM_EEPROM,        /* EEPROM */
    FX_PLC_MEM_FLASH,         /* FLASH */
    FX_PLC_MEM_UNKNOWN        /* 未知 */
} fx_plcinf_mem_type_t;

/* LED状态定义 */
typedef enum {
    FX_PLC_LED_OFF = 0,       /* 关闭 */
    FX_PLC_LED_ON,            /* 打开 */
    FX_PLC_LED_GREEN,         /* 绿色 */
    FX_PLC_LED_RED            /* 红色 */
} fx_plcinf_led_state_t;

/* LED类型定义 */
typedef enum {
    FX_PLC_LED_POWER = 0,     /* 电源 */
    FX_PLC_LED_RUN,           /* 运行 */
    FX_PLC_LED_BATT,          /* 电池 */
    FX_PLC_LED_ERROR          /* 错误 */
} fx_plcinf_led_type_t;

/* 监视状态 */
typedef enum {
    FX_PLC_MONITOR_IDLE = 0,  /* 空闲 */
    FX_PLC_MONITOR_RUNNING,   /* 监视执行中 */
    FX_PLC_MONITOR_STOPPED    /* 已停止 */
} fx_plcinf_monitor_state_t;

/* 错误信息结构体 */
typedef struct {
    uint8_t     error_no;      /* 错误编号 */
    uint8_t     cls;           /* 错误类别下标(见 g_err_cls_name)：用于在页面上标出动作的特殊继电器 M */
    uint16_t    error_step;    /* 错误步(来自 D8069) */
    char        error_msg[32]; /* 错误信息(64→32: 与记录数同步压缩, 回收SRAM) */
} fx_plcinf_error_t;

/* PLC信息结构体 */
typedef struct {
    
    fx_plcinf_cpu_type_t      cpu_type;        /* CPU类型 */
    uint16_t                  cpu_version;     /* CPU版本(BCD码, 3.15 = 0x0315) */
    fx_plcinf_mem_type_t      mem_type;        /* 存储器类型 */
    uint8_t                   battery_mode;    /* 无电池模式: 0=无效, 1=有效 */

    fx_plcinf_led_state_t     led_power;       /* POWER LED状态 */
    fx_plcinf_led_state_t     led_run;         /* RUN LED状态 */
    fx_plcinf_led_state_t     led_batt;        /* BATT LED状态 */
    fx_plcinf_led_state_t     led_error;       /* ERROR LED状态 */

} fx_plcinf_info_t;

#define FX_PLCINF_MAX_ERRORS  8   /* 最大错误记录数(与 8+4 个错误类别对应：D8060~D8067 + D8438/D8449/D8487/D8489) */

/* 函数声明 */
void FX_PLCINF_Init(void);
void FX_PLCINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url);
 
void FX_PLCINF_GetError(uint8_t index, fx_plcinf_error_t *error);

/* PLC 状态位(M8000~M8015)读取与 LED 刷新 */
void FX_PLCINF_RequestStatus(uint8_t Sour_Sock, uint8_t Dest_Sock);
void FX_PLCINF_OnStatusReply(const uint8_t *data, uint16_t len);

/* 错误信息(D8000~D8069：D8004 + D8060~D8067 错误代码 + D8069 发生的步编号)解析 */
void FX_PLCINF_OnErrorReply(const uint8_t *data, uint16_t len);

/* 扩展错误信息(D8438~D8489：串行通信错误2 / 特殊模块 / USB / 特殊参数)解析 */
void FX_PLCINF_OnErrorReplyExt(const uint8_t *data, uint16_t len);
 
 
 

#ifdef __cplusplus
}
#endif

#endif /* __FX_PLCINF_H */
