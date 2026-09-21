/********************************** (C) COPYRIGHT *******************************
* File Name          : net_config.h
* Author             : WCH
* Version            : V1.30
* Date               : 2022/06/02
* Description        : 本文件包含以太网协议栈库的配置
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: 本软件(无论是否修改)及其二进制文件用于南京沁恒微电子生产的微控制器。
*******************************************************************************/
#ifndef __NET_CONFIG_H__
#define __NET_CONFIG_H__

#ifdef __cplusplus
extern "C" {
#endif

/*********************************************************************
 * socket 配置, IPRAW + UDP + TCP + TCP_LISTEN = socket 总数
 */
#define WCHNET_NUM_IPRAW              1  /* IPRAW 连接数 */

#define WCHNET_NUM_UDP                2  /* 方案A: 保留 2(UDP=4 会使 SocketRecvBuf 达11.4KB且总socket=13, 超预算) */

#define WCHNET_NUM_TCP                4  /* TCP 连接数 (原为4, 为提升多客户端稳定性而增加) */

//用于配置 TCP 监听的个数，最小值为 1。TCP 监听的 socket 仅仅用于监听，
//一旦监听到TCP 连接，会立即分配一个 TCP 连接，占用 WCHNET_NUM_TCP 的个数。
#define WCHNET_NUM_TCP_LISTEN         4  /* TCP 监听数 */

/* socket 总数, 最大为 31  */
#define WCHNET_MAX_SOCKET_NUM         (WCHNET_NUM_IPRAW+WCHNET_NUM_UDP+WCHNET_NUM_TCP+WCHNET_NUM_TCP_LISTEN)

//TCP 最大报文段的长度，
#define WCHNET_TCP_MSS                1140  /* TCP MSS 大小 */

#define WCHNET_NUM_POOL_BUF           (WCHNET_NUM_TCP*4+2)   /* POOL BUF 数量, 即接收队列数量 */

/*********************************************************************
 * MAC 队列配置
 */
#define ETH_TXBUFNB                   2    /* MAC 发送描述符数量  */

#define ETH_RXBUFNB                   3    /* MAC 收缓冲个数(方案B: 7→4→3, 再省~1.5KB SRAM, 为HTTP模块腾出RAM) */

#ifndef ETH_MAX_PACKET_SIZE
#define ETH_RX_BUF_SZE                1520  /* MAC 接收缓冲长度, 4 的整数倍 */
#define ETH_TX_BUF_SZE                1520  /* MAC 发送缓冲长度, 4 的整数倍 */
#else
#define ETH_RX_BUF_SZE                ETH_MAX_PACKET_SIZE
#define ETH_TX_BUF_SZE                ETH_MAX_PACKET_SIZE
#endif

/*********************************************************************
 *  功能配置
 */
#define WCHNET_PING_ENABLE            1     /* PING功能开启, 默认即开启 */

#define TCP_RETRY_COUNT               1    /* TCP重传次数 (原为2, 此处6*250ms=1.5s 容限) */

#define TCP_RETRY_PERIOD              3    /* TCP重传周期, 默认值为10, 单位为50ms */

#define SOCKET_SEND_RETRY             1     /* 发送失败重试配置, 1: 启用, 0: 禁用 */

#define HARDWARE_CHECKSUM_CONFIG      1     /* 硬件校验和检测与插入配置, 1: 启用, 0: 禁用 */

#define FINE_DHCP_PERIOD              8     /* DHCP精细刷新周期, 默认值为8, 单位为250ms */

#define CFG0_TCP_SEND_COPY            1     /* TCP发送缓冲拷贝, 1: 拷贝, 0: 不拷贝 */

#define CFG0_TCP_RECV_COPY            1     /* TCP接收拷贝优化, 内部调试用途 */

#define CFG0_TCP_OLD_DELETE           1     /* 多客户端活跃时误踢PLC通信链路 */

#define CFG0_IP_REASS_PBUFS           0     /* IP分片重组 PBUF 数量 */

#define CFG0_TCP_DEALY_ACK_DISABLE    0     /* 1: 禁用TCP延迟确认(DELAY ACK)  0: 启用TCP延迟确认 */

/*********************************************************************
 *  内存相关配置
 */
/* 若需更高传输速度,
 * 可尝试将 RECE_BUF_LEN 增大到 (WCHNET_TCP_MSS*4)
 * 并将 WCHNET_NUM_TCP_SEG 增大到 (WCHNET_NUM_TCP*4)*/
#define RECE_BUF_LEN                  (WCHNET_TCP_MSS*2)   /* socket 接收缓冲大小 */

#define WCHNET_NUM_PBUF               WCHNET_NUM_POOL_BUF   /* PBUF 结构数量 */

#define WCHNET_NUM_TCP_SEG            (WCHNET_NUM_TCP*2)   /* 用于发送的 TCP 段数量 */

#define WCHNET_MEM_HEAP_SIZE          (((WCHNET_TCP_MSS+0x10+54+8)*WCHNET_NUM_TCP_SEG)+ETH_TX_BUF_SZE+64+2*0x18) /* 内存堆大小 */

//ARP 缓存，存放 IP 和 MAC，此值最小可以设置为 1，最大为 0x7F。如果 WCHNET 需要和 4台 PC 进行网络通讯，其中两台会大批量收发数据，则建议设置为 4。
#define WCHNET_NUM_ARP_TABLE          4   /* ARP 表项数量 */

#define WCHNET_MEM_ALIGNMENT          4    /* 4 字节对齐 */

#if CFG0_IP_REASS_PBUFS
#define WCHNET_NUM_IP_REASSDATA       2    /* IP 重组结构数量 */
/*1: 使用分片功能时,
 *  确保 WCHNET_SIZE_POOL_BUF 的大小足以存放单个分片包*/
#define WCHNET_SIZE_POOL_BUF    (((1500 + 14 + 4) + 3) & ~3)    /* 接收单个包的缓冲大小 */
/*2: 创建可接收分片包的 socket 时,
 *  确保 "struct _SOCK_INF" 结构的 "RecvBufLen" 成员
 *  (调用 WCHNET_SocketCreat 时初始化的参数) 足以接收完整分片包  */
#else
#define WCHNET_NUM_IP_REASSDATA       0    /* IP 重组结构数量 */
#define WCHNET_SIZE_POOL_BUF     (((WCHNET_TCP_MSS + 40 + 14 + 4) + 3) & ~3) /* 接收单个包的缓冲大小 */
#endif

/* 检查接收缓冲 */
#if(WCHNET_NUM_POOL_BUF * WCHNET_SIZE_POOL_BUF < ETH_RX_BUF_SZE)
    #error "WCHNET_NUM_POOL_BUF 或 WCHNET_TCP_MSS 配置错误"
    #error "请增大 WCHNET_NUM_POOL_BUF 或 WCHNET_TCP_MSS 以确保接收缓冲足够"
#endif
/* 检查 SOCKET 数量配置 */
#if( WCHNET_NUM_TCP_LISTEN && !WCHNET_NUM_TCP )
    #error "WCHNET_NUM_TCP 配置错误, 请配置 WCHNET_NUM_TCP >= 1"
#endif
/* 检查字节对齐必须为 4 的整数倍 */
#if((WCHNET_MEM_ALIGNMENT % 4) || (WCHNET_MEM_ALIGNMENT == 0))
    #error "WCHNET_MEM_ALIGNMENT 配置错误, 请配置 WCHNET_MEM_ALIGNMENT = 4 * N, N >= 1"
#endif
/* TCP 最大报文段长度 */
#if((WCHNET_TCP_MSS > 1460) || (WCHNET_TCP_MSS < 60))
    #error "WCHNET_TCP_MSS 配置错误, 请配置 WCHNET_TCP_MSS >= 60 && WCHNET_TCP_MSS <= 1460"
#endif
/* ARP 缓存表项数 */
#if((WCHNET_NUM_ARP_TABLE > 0X7F) || (WCHNET_NUM_ARP_TABLE < 1))
    #error "WCHNET_NUM_ARP_TABLE 配置错误, 请配置 WCHNET_NUM_ARP_TABLE >= 1 && WCHNET_NUM_ARP_TABLE <= 0X7F"
#endif
/* 检查 POOL BUF 配置 */
#if(WCHNET_NUM_POOL_BUF < 1)
    #error "WCHNET_NUM_POOL_BUF 配置错误, 请配置 WCHNET_NUM_POOL_BUF >= 1"
#endif
/* 检查 PBUF 结构配置 */
#if(WCHNET_NUM_PBUF < 1)
    #error "WCHNET_NUM_PBUF 配置错误, 请配置 WCHNET_NUM_PBUF >= 1"
#endif
/* 检查 IP 分配配置 */
#if(CFG0_IP_REASS_PBUFS && ((WCHNET_NUM_IP_REASSDATA > 10) || (WCHNET_NUM_IP_REASSDATA < 1)))
    #error "WCHNET_NUM_IP_REASSDATA 配置错误, 请配置 WCHNET_NUM_IP_REASSDATA < 10 && WCHNET_NUM_IP_REASSDATA >= 1 "
#endif
/* 检查重组 IP PBUF 的数量  */
#if(CFG0_IP_REASS_PBUFS > WCHNET_NUM_POOL_BUF)
    #error "WCHNET_NUM_POOL_BUF 配置错误, 请配置 CFG0_IP_REASS_PBUFS < WCHNET_NUM_POOL_BUF"
#endif
/* 检查定时器周期, 单位 ms  */
#if(WCHNETTIMERPERIOD > 50)
    #error "WCHNETTIMERPERIOD 配置错误, 请配置 WCHNETTIMERPERIOD < 50"
#endif

/* 配置值 0 */
#define WCHNET_MISC_CONFIG0    (((CFG0_TCP_SEND_COPY) << 0) |\
                               ((CFG0_TCP_RECV_COPY)  << 1) |\
                               ((CFG0_TCP_OLD_DELETE) << 2) |\
                               ((CFG0_IP_REASS_PBUFS) << 3) |\
                               ((CFG0_TCP_DEALY_ACK_DISABLE) << 8))
/* 配置值 1 */
#define WCHNET_MISC_CONFIG1    (((WCHNET_MAX_SOCKET_NUM)<<0)|\
                               ((WCHNET_PING_ENABLE) << 13) |\
                               ((TCP_RETRY_COUNT)    << 14) |\
                               ((TCP_RETRY_PERIOD)   << 19) |\
                               ((SOCKET_SEND_RETRY)  << 25) |\
                               ((HARDWARE_CHECKSUM_CONFIG) << 26)|\
                               ((FINE_DHCP_PERIOD) << 27))

#ifdef __cplusplus
}
#endif
#endif
