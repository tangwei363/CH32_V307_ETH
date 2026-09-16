 
#ifndef _BSP_WCH_NET_H_
#define _BSP_WCH_NET_H_

#include "ch32v30x.h"
#include "debug.h"
#include "net_config.h"
/* 用户私有定义结束 */
#ifdef _BSP_WCH_DEBUG
    #define WCH_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define WCH_DEBUG(format, ...)
#endif


/* 全局宏定义 */
#define KEEPALIVE_ENABLE                1                   //保活功能开关: 0=关闭, 1=开启
#define PHY_DEBOUNCE_MS                 200                 //PHY 链路状态去抖时间(ms), 防止网线抖动导致频繁上下线


typedef struct WCH_SOCKET_Type  
{    
    uint8_t  net_stat;              //连接状态: 0=未连接, 1=已连接
    uint8_t  DesSockId;             //目标 SOCKET ID
    uint32_t DesPort;               //目标端口号

    uint8_t  SourSockId;            //源 SOCKET ID
    uint8_t  SourIP[4];             //源 IP 地址
    uint32_t SourPort;              //源端口号

} WCH_SOCKET_T;

/* 每个 Socket 的控制信息: 对应 eth_socket[] 索引 */
typedef struct {
    int8_t  eth_sid;               /* eth_socket[] 索引, -1=未映射 */
} WCH_SocketCtrl_t;

extern u8 MACAddr[6];                       //MAC 地址
extern u8 SocketRecvBuf[WCHNET_NUM_UDP+WCHNET_NUM_IPRAW][RECE_BUF_LEN];  //socket 接收缓冲
extern u16 DESPORT, SRCPORT;                //目的端口 / 源端口

void mStopIfError(u8 iError);
 
void TIM1_INT_Init(u16 arr, u16 psc);
void TIM2_Init(void);

/**
 * @brief   创建用于监听的 TCP Socket, 指定 socket ID 与监听端口
 *
 * @param   socket_id - 存放创建成功的 socket ID 的指针
 * @param   listen_port - 监听端口号
 *
 * @return  socket 创建结果: WCHNET_ERR_SUCCESS 表示成功, 否则为错误码
 */
u8 WCHNET_CreateTcpSocketListen(u8 *socket_id, u16 listen_port);

/**
 * @brief   创建 UDP Socket, 指定 socket ID 与端口
 *
 * @param   socket_id - 存放创建成功的 socket ID 的指针
 * @param   udp_port - 绑定的 UDP 端口号
 *
 * @return  socket 创建结果: WCHNET_ERR_SUCCESS 表示成功, 否则为错误码
 */
u8 WCHNET_CreateUdpSocket(u8 *socket_id, u16 udp_port);

void WCHNET_CreateIPRawSocket(u8 *socket_id, u8 * DESIP,u16 IPRawProto);
void WCHNET_CreateCfgSocket(u8 mode,u8 *socket_id, u8 *Desip, u16 Desport, u16 Srcport);

void WCHNET_DataLoopback(u8 id);
void WCHNET_HandleSockInt(u8 socketid, u8 intstat);
void WCHNET_HandleGlobalInt(void);
void socket_map_init(void);

extern WCH_SocketCtrl_t socket_ctrl[WCHNET_MAX_SOCKET_NUM]; /* 每个 Socket 的控制信息 */

#endif /* end of bsp_wch_net.h */
