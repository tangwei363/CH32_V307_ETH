/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_status.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP 通信状态页面头文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/
 
#ifndef __FX_STATUS_H
#define __FX_STATUS_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "ethernet_app.h"


/* 适配器信息结构体 */
typedef struct {
    uint8_t  ip_addr[4];        /* IP地址 */
    uint8_t  netmask[4];        /* 子网掩码 */
    uint8_t  gateway[4];        /* 默认路由器IP地址 */
    uint8_t  mac_addr[6];       /* 以太网地址 */
} fx_status_adapter_info_t;

/* 协议统计信息 */
typedef struct {
    uint32_t tcp_rx_packets;    /* TCP接收包数 */
    uint32_t tcp_tx_packets;    /* TCP发送包数 */
    uint32_t udp_rx_packets;    /* UDP接收包数 */
    uint32_t udp_tx_packets;    /* UDP发送包数 */
} net_packet_t;

/*各连接状状态*/
typedef struct eth_link_status_t
{
    //1 :连接号功能 (8 byte )
    uint16_t connection;            // 0x5000,   //本站端口号:80--0x0050
    uint16_t remote_port;           // 0x0000,   //通讯对象端口号 
    uint8_t remote_ip[4];           // 0x0000,0x0000, //通讯对象IP地址
    uint16_t error_code;            // 0x0000,   //错误代码
    uint16_t open_mode;             // 0x02A8,   //0x02:TCP 0xA8 打开方式:数据监视
    uint16_t tcp_status;            // 0x0000,   //TCP 连接状态  0:切断 1:连接中
    uint16_t force_disable;         // 0x0000,   //强制禁用 状态 0:允许 1:禁用
}eth_link_status_t;


/* 函数声明 */
 
char* FX_STATUS_GenerateConnectionTable(void);

void FX_STATUS_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock, char *url);

#ifdef __cplusplus
}
#endif

#endif /* __FX_STATUS_H */
