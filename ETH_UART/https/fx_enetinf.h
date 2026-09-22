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

/* 错误履历结构体（★ 本模块的"权威布局"：17 字 = 34 字节，含"开放方式"）
 *
 * 字段与 PLC 侧一一对应（PLC 按"字"为单位接收，见 FX_ErrorLogs_SendAccess_PLC）：
 *   conn_id / protocol / open_type / local_port / error_code /
 *   remote_ip[4] / remote_port / cmd_code / reserved_1 / reserved_2 / log_time
 *
 * 说明：本布局比 ethernet_app.h 里历史遗留的 eth_err_log_t(15 字、无 open_type) 多 2 字、
 * 且字段顺序不同 —— 上传 PLC 时以本结构体为准（eth_err_log_t 的注释已同步标注）。
 * reserved_1/reserved_2 必须为 0x0000：添加路径已 memset 保证，不要删。 */
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

/* 错误履历"错误代码"取值（页面按 %04X 显示，与 PLC 侧错误代码区同口径）：
 *   0x0000 : 无错误(占位)
 *   0x00FE : 通信超时 —— 本模块自定义：连接超时被关闭等"没有任何应答"的场景，
 *            此时 socket_p->Error_Code 与 MC 结束码都不存在，只能由调用方指定。
 *   其他   : 直接用 socket_p->Error_Code 或 MC 结束码 */
#define FX_ENETINF_ERR_TIMEOUT     0x00FE

extern fx_enetinf_error_log_t g_error_logs[];  /* 错误履历数组 */
extern fifo_queue_t g_error_fifo;  /* 错误履历队列 */

/* 函数声明 */
void FX_ENETINF_Init(void);
void FX_ENETINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url);
void FX_ENETINF_GetAdapterInfo(fx_enetinf_adapter_t *info);
void FX_ENETINF_SetAdapterInfo(fx_enetinf_adapter_t *info);
uint8_t FX_ENETINF_GetErrorLog(uint8_t index, fx_enetinf_error_log_t *log);
/* 添加一条错误履历（供 ethernet_error_code_ack 等"错误收口点"调用）：
 * 只做内存写入 + 置脏标志，可安全用于 socket 事件上下文；串口上传由 FX_ErrorLogs_Task() 完成 */
void FX_ENETINF_ACCLOG_AddRecord(uint16_t conn_id, uint8_t protocol, uint8_t open_type,
                                 uint16_t local_port, uint16_t error_code, uint8_t remote_ip[4],
                                 uint16_t remote_port, uint8_t cmd_code);
void FX_ENETINF_ClearErrorLogs(void);
void FX_ErrorLogs_SendAccess_PLC(void);
void FX_ErrorLogs_Task(void);          /* 主循环调用：把未上传的错误履历写入 PLC */
void FX_ENETINF_UpdateMonitor(void);

#ifdef __cplusplus
}
#endif

#endif /* __FX_ENETINF_H */
