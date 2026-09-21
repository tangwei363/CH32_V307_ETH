/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_enetinf.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP信息页面头文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __FX_ENETINF_H
#define __FX_ENETINF_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ethernet_app.h"
#include "fifo_queue.h"

/* LED类型定义 */
typedef enum {
    FX_ENET_LED_POWER = 0,    /* 电源LED */
    FX_ENET_LED_100M,         /* 100M速度LED */
    FX_ENET_LED_ERR,          /* 错误LED */
    FX_ENET_LED_OPEN          /* 打开状态LED */
} fx_enetinf_led_type_t;

/* LED状态定义 */
typedef enum {
    FX_ENET_LED_OFF = 0,      /* 关闭 */
    FX_ENET_LED_ON,           /* 打开 */
    FX_ENET_LED_GREEN         /* 绿色 */
} fx_enetinf_led_state_t;

/* 协议类型 */
typedef enum {
    FX_ENET_PROTO_TCP = 0,    /* TCP协议 */
    FX_ENET_PROTO_UDP         /* UDP协议 */
} fx_enetinf_proto_type_t;

/* 开放方式 */
typedef enum {
    FX_ENET_OPEN_MELSOFT = 0, /* MELSOFT连接 */
    FX_ENET_OPEN_MC,          /* MC协议 */
    FX_ENET_OPEN_MONITOR,     /* 数据监视 */
    FX_ENET_OPEN_UNKNOWN      /* 未知 */
} fx_enetinf_open_type_t;

 /* 适配器信息结构体 */
typedef struct {
    uint16_t                  version;        /* 版本号(BCD码, 1.22 = 0x0122) */
 
    fx_enetinf_led_state_t    led_power;     /* POWER LED状态 */
    fx_enetinf_led_state_t    led_100m;      /* 100M LED状态 */
    fx_enetinf_led_state_t    led_err;       /* ERR LED状态 */
    fx_enetinf_led_state_t    led_open;      /* OPEN LED状态 */
} fx_enetinf_adapter_t;

/* 错误履历结构体 */
typedef struct {
    uint16_t     conn_id;        /* 连接号 */
    uint16_t     protocol;       /* 协议类型 */
    uint16_t     open_type;      /* 开放方式 */
    uint16_t     local_port;      /* 本站端口号 */
    uint16_t     error_code;     /* 错误代码 */
    uint8_t     remote_ip[4];    /* 通信对象IP地址 */
    uint16_t    remote_port;     /* 通信对象端口号 */
    uint16_t     cmd_code;       /* 指令代码 */
    uint16_t    reserved_1;        // 保留      0x0000
    uint16_t    reserved_2;        // 保留      0x0000
    eth_log_time_t log_time;    // 日志时间
} fx_enetinf_error_log_t;

 
#define FX_ENETINF_MAX_ERRORS  8   /* 最大错误履历数(16→8: g_error_logs 由544B降至272B) */

extern fx_enetinf_error_log_t g_error_logs[];  /* 错误履历数组 */
extern fifo_queue_t g_error_fifo;  /* 错误履历队列 */

/* 函数声明 */
void FX_ENETINF_Init(void);
void FX_ENETINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url);
void FX_ENETINF_GetAdapterInfo(fx_enetinf_adapter_t *info);
void FX_ENETINF_SetAdapterInfo(fx_enetinf_adapter_t *info);
void FX_ENETINF_GetErrorLog(uint8_t index, fx_enetinf_error_log_t *log);
void FX_ENETINF_AddErrorLog(fx_enetinf_error_log_t *log);
void FX_ENETINF_ClearErrorLogs(void);
void FX_ErrorLogs_SendAccess_PLC(void);
void FX_ENETINF_UpdateMonitor(void);

#ifdef __cplusplus
}
#endif

#endif /* __FX_ENETINF_H */
