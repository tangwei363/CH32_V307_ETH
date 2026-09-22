/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_acclog.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP 访问履历页面头文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __FX_ACCLOG_H
#define __FX_ACCLOG_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ethernet_app.h"

//#define _FX_ACCLOG_DEBUG            //日志模块 开关，注释掉将关闭日志输出

#ifdef _FX_ACCLOG_DEBUG
    #define FX_ACCLOG_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define FX_ACCLOG_DEBUG(format, ...)
#endif

/* 协议类型 */
typedef enum {
    FX_ACCLOG_PROTO_TCP = 0,    /* TCP协议 */
    FX_ACCLOG_PROTO_UDP         /* UDP协议 */
} fx_acclog_proto_type_t;

/* 开放方式 */
typedef enum {
    FX_ACCLOG_OPEN_MELSOFT = 0, /* MELSOFT连接 */
    FX_ACCLOG_OPEN_MC,          /* MC协议 */
    FX_ACCLOG_OPEN_MONITOR,     /* 数据监视 */
    FX_ACCLOG_OPEN_UNKNOWN      /* 未知 */
} fx_acclog_open_type_t;

 
/* 访问履历记录结构体 */
typedef struct {
    eth_log_time_t  log_time;    // 日志时间
    uint16_t     conn_id;        /* 连接号 */
    uint16_t     protocol;       /* 协议类型 */
    uint16_t     open_type;      /* 开放方式 */
    uint8_t      remote_ip[4];   /* 通信对象IP地址 */
} fx_acclog_record_t;

#define FX_ACCLOG_MAX_RECORDS  8   /* 最大访问履历数(16→8: g_access_logs 由352B降至176B) */

/* 访问履历队列结构体 */
#include "fifo_queue.h"
typedef fifo_queue_t fx_acclog_fifo_t;

/* 函数声明 */
void FX_ACCLOG_Init(void );
void FX_ACCLOG_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock, char *url);
void FX_ACCLOG_GetRecord(uint8_t index, fx_acclog_record_t *record);
void FX_ACCLOG_AddRecord(uint16_t conn_id, uint8_t protocol, uint8_t open_type, uint8_t *remote_ip);
uint8_t FX_ACCLOG_GetNextRecord(fx_acclog_record_t *record);
void FX_ACCLOG_ResetReadPos(void);
void FX_ACCLOG_ClearRecords(void);
uint8_t FX_ACCLOG_GetRecordCount(void);

void FX_ACCLOG_UpdateMonitor(void);
 
void WCHNET_UpdateAccLog(void);
/* 主循环调用：有未上传的访问履历就写入 PLC（添加路径只置标志，不在这里做带延时的串口写） */
void FX_ACCLOG_Task(void);

#ifdef __cplusplus
}
#endif

#endif /* __FX_ACCLOG_H */
