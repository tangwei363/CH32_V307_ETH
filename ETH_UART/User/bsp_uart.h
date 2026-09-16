/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_uart.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2022/05/10
* Description        : uart cfg.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef _BSP_UART_H_
#define _BSP_UART_H_

#include "debug.h"
#include "ch32v30x.h"
#include "net_config.h"

#include "../melsec_fx/melsec_fx_net.h"

#include "xqLoopList.h"
#include "xqBufferManage.h"

#ifdef _UART_DEBUG
    #define UART_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define UART_DEBUG(format, ...)
#endif

#define  UART_USE_FIFO   1  // 1: 使用 队列管理 发送 0: 不使用 队列管理 发送

/* Global define */
#define BAUD_RATE   3000000

#define size(a)   (sizeof(a) / sizeof(*(a)))

#define MIN(X,Y)  ((X) < (Y) ? (X) : (Y))

#define UART_RX_DMA_SIZE    600
#define UART_TX_DMA_SIZE    600

/* 半帧滞留超时(ms)：串口接收采用"完整帧才切缓冲投递"策略，
 * 若对端断线/丢字节导致缓冲内长期无法成帧，超过该时间由主循环请求强制复位，
 * 避免接收通道被半帧永久占用(取值需 > 一个完整帧的传输时间)。 */
#define UART_RX_PARTIAL_TIMEOUT_MS   200

/* 全局 DMA 缓冲区（供 ISR 双缓冲切换直接引用） */
extern u8 UART2_RX_DMA_DataBuf[UART_RX_DMA_SIZE];
extern u8 UART2_RX_DMA_DataBuf_ALT[UART_RX_DMA_SIZE];
extern u8 UART2_TX_DMA_DataBuf[UART_TX_DMA_SIZE];

#define ETH_RECEIVE_SIZE    600
 

#define UART_TX_RETRY_MAX       3
#define UART_TX_TIMEOUT_MS      30

typedef enum { IDLE = 0, BUSY = !IDLE} Uart_TX_DMA_State;

#define NET_LED_ENABLE             0        // 网络LED使能标志

#define LED_RX_GPIO_RCC             RCC_APB2Periph_GPIOB
#define LED_RX_GPIO_TypeDef         GPIOB
#define LED_RX_GPIO_PIN             GPIO_Pin_13

#define LED_TX_GPIO_RCC             RCC_APB2Periph_GPIOB
#define LED_TX_GPIO_TypeDef         GPIOB
#define LED_TX_GPIO_PIN             GPIO_Pin_12

/* LED闪烁时间定义（单位：ms） */
#define LED_RX_BLINK_TIME     20  // 接收LED闪烁周期（点亮后保持时间）
#define LED_TX_BLINK_TIME     20  // 发送LED闪烁周期（点亮后保持时间）
#define LED_OFF_BLINK_TIME    2  // 发送LED闪烁周期（熄灭后保持时间）

extern volatile uint32_t g_ulSystemTick;
void Tick_time_handler(void);
uint32_t Tick_time_get(void);

#if NET_LED_ENABLE == 1

/* LED控制函数（优化版本，使用定时器控制闪烁） */
void NEN_RX_LED_SetState(u8 state);       // 设置接收LED状态（1=点亮，0=熄灭）
void NEN_TX_LED_SetState(u8 state);       // 设置发送LED状态（1=点亮，0=熄灭）
void NEN_RX_LED_Trigger(void);            // 触发接收LED闪烁（点亮后自动熄灭）
void NEN_TX_LED_Trigger(void);            // 触发发送LED闪烁（点亮后自动熄灭）
void NEN_LED_Update(void);                // LED定时更新函数（需在主循环周期调用）

/* 保留原有函数以兼容旧代码 */
void NEN_RX_LED_SET(u8 Val);
void NEN_TX_LED_SET(u8 Val);
void NEN_RX_LED_Toggle(void);
void NEN_TX_LED_Toggle(void);

#endif //NET_LED_ENABLE 


/*********************************************************************
 * 波特率同步状态机定义
 */
typedef enum {

    BAUD_SYNC_STATE_INIT = 0,           /* 初始化：装载最高档波特率，作为降档扫描起点 */
    BAUD_SYNC_STATE_APPLY_RATE,         /* 应用当前档波特率到硬件并等待链路稳定 */
    BAUD_SYNC_STATE_SEND_SYNC_05,       /* 握手①：同步探测帧 0x05 */
    BAUD_SYNC_STATE_SEND_00E0202,       /* 发送测试帧 \STX00E0202\ETX6C          \STX015F\ETXDF */
    BAUD_SYNC_STATE_SEND_00ECA02,       /* 发送测试帧 \STX00ECA02\ETX8E          \STXC13F\ETXF0 */
    BAUD_SYNC_STATE_SEND_E000EE8,       /* 发送测试帧 \STXE000EE804\ETXFE        \STXFDE8519B\ETXEB */
    //    02 45 31 30 30 45 45 43 30 34 36 38 37 32 32 35 37 39 03 42 38
    BAUD_SYNC_STATE_SEND_E100EEC,       /* 握手⑤(终判)：\STXE100EEC04EDB17066\ETXD9 通过即命中 */

    BAUD_SYNC_STATE_SCAN_NEXT,          /* 降档：档位索引-1，应用下一档后重新握手 */
    BAUD_SYNC_STATE_ROUND_END,          /* 本轮扫描结束：整轮重扫 或 判定失败 */

    BAUD_SYNC_STATE_SUCCESS,            /* 同步成功 */
    BAUD_SYNC_STATE_FAILED              /* 同步失败 */

} BaudSyncState_t;

/* 波特率同步状态机上下文 */
typedef struct {
    BaudSyncState_t state;              /* 当前状态 */
    uint8_t scan_index;                 /* 当前扫描档位索引(从最高档向最低档递减) */
    uint8_t fail_count;                 /* 本轮扫描中已失败的档位数(日志用) */
    uint8_t apply_retry;                /* 硬件切档重试计数(串口忙时) */
    uint8_t scan_round;                 /* 已完成的整轮扫描次数(错误恢复用) */
    uint8_t is_sync_flag;               /* 同步结果标志 1=已锁定成功 */
    uint32_t baudrate;                  /* 当前生效的波特率 */

} BaudSyncContext_t;

#define BAUD_SYNC_TIMEOUT_MS             150     /* 握手帧等待回应超时 [ms] */
#define BAUD_SYNC_PROBE_TIMEOUT_MS       80      /* 0x05 探测帧超时 [ms] (PLC在线时通常<10ms应答) */
#define BAUD_SYNC_DELAY_MS               10      /* 切换波特率后的稳定延时 [ms] */
#define BAUD_SYNC_MAX_RETRY              2       /* 握手帧最大重试次数 */
#define BAUD_SYNC_PROBE_MAX_RETRY        2       /* 探测帧最大重试次数 */
#define BAUD_SYNC_APPLY_MAX_RETRY        3       /* 硬件切档失败(串口忙)最大重试次数 */
#define BAUD_SYNC_SCAN_ROUND_MAX         3       /* 整轮降档扫描最大重复次数 */
#define BAUD_SYNC_FALLBACK_BAUD          9600    /* 全部档位失败后的兜底波特率 */
#define BAUD_SYNC_RESET_ON_ALL_FAILED    0       /* 1=兜底后复位重试(原行为) 0=返回-1交上层 */

/* ─── 发送请求队列节点: 包含发送数据和请求上下文，形成闭环数据流 ─── */
/* 设计理念: 发送入队保存数据，接收出队解析数据，保证请求-响应配对正确 */
#define UART_REQ_QUEUE_SIZE     80                                      /* 队列深度(请求数, 非2的幂, 用取模运算) */
#define UART_REQ_DATA_MAX_LEN   600                                     /* 单请求最大数据长度(入队截断上限, 兼容旧值) */
#define UART_REQ_RING_CAP       2048                                    /* 共享发送数据环形存储池容量(字节, 必须为2的幂便于&回绕) */

typedef struct {
    uint16_t data_off;                     /* 数据在环形缓冲 ring 中的偏移 */
    uint16_t data_len;                     /* 实际数据长度 */
    
    /* 请求上下文快照: 接收时用于路由响应 */
    uint8_t  Sour_Sockid;                  /* 源socket ID（客户端socket） */
    uint8_t  Dest_Sockid;                  /* 目标socket ID（服务器socket） */
    NET_MC_Recv_Resp_t mc_meta;            /* MC协议上下文 */
    uint32_t eth_seq_num;                  /* 请求序号 */
    
    /* 发送控制信息 */
    uint32_t send_time;                     /* 发送时间戳（用于超时检测） */
    uint8_t  retry_count;                   /* 当前重试次数 */
} uart_req_queue_node_t;

/* 环形队列管理: O(1) 入队/出队操作 */
typedef struct {
    uart_req_queue_node_t nodes[UART_REQ_QUEUE_SIZE]; /* 队列节点数组 */
    uint8_t head;                                      /* 头指针(出队位置) */
    uint8_t tail;                                      /* 尾指针(入队位置) */
    uint16_t count;                                    /* 当前队列长度（使用uint16_t防止溢出） */
    uint8_t  ring[UART_REQ_RING_CAP];                  /* 共享发送数据环形存储池(替代每节点600字节) */
    uint16_t ring_wp;                                  /* 环形写入位置(已用计数法, 无需独立读指针) */
    uint16_t ring_used;                                /* 环形已用字节数(用于判满) */

    /* ─── 请求序号管理(原为文件级全局变量, 统一收归队列结构体) ─── */
    uint32_t seq_counter;                              /* 请求序号生成器: 每次入队前 +1, 全程单调递增,
                                                        * 由 uartTxWithSocketID 取用。
                                                        * 注意: 不随队列清空/重连而复位(见 uart_req_queue_init) */
    volatile uint32_t last_enq_seq;                    /* 最近一次入队的请求序号(0=从未入队)。
                                                        * 入队是全项目串口请求的唯一入口，故该值恒为
                                                        * "刚刚下发的那条串口命令"的序号。上层协议模块
                                                        * (如 Modbus 从站)在登记事务时记录它，响应到达时
                                                        * 与 uart_rx_ctx.eth_seq_num 比对，即可拒绝
                                                        * 迟到/重复响应被算到当前事务上。 */
} uart_req_queue_t;

/* 队列操作接口声明 */
void        uart_req_queue_init(void);                                 /* 队列初始化 */
uint8_t     uart_req_queue_is_empty(void);                             /* 队列是否为空 */
uint8_t     uart_req_queue_is_full(void);                              /* 队列是否已满 */
int         uart_req_queue_enqueue(uint8_t Sour_Sockid, uint8_t Dest_Sockid,
                                   NET_MC_Recv_Resp_t *mc_meta, uint32_t eth_seq_num,
                                   uint8_t *data, uint16_t len);        /* 入队 */
uart_req_queue_node_t* uart_req_queue_front(void);                     /* 获取队首(不删除) */
void        uart_req_queue_dequeue(void);                              /* 出队(删除队首) */
void        uart_req_queue_clear(void);                                /* 清空队列 */
uart_req_queue_node_t* uart_req_queue_peek_tail(void);                 /* 获取队尾(下一个入队位置) */

/* ─── 串口接收解析上下文: 分离自 uart_data_t，仅用于串口数据解析→网口路由 ─── */
/* 设计意图: 与 uart_mc_meta 同级，聚焦 socket 路由信息，不受 uartSendNextPacket 覆写干扰 */
typedef struct {
    uint8_t  Sour_Sockid;               /* 源socket ID（客户端socket） */
    uint8_t  Dest_Sockid;               /* 目标socket ID（服务器socket） */
    uint32_t eth_seq_num;               /* 当前 ETH 到达序号 */
} uart_rx_ctx_t;

extern uart_rx_ctx_t uart_rx_ctx;      /* 串口解析路由上下文 (与 uart_mc_meta 同级分离) */

/* UART 硬件/DMA/队列元数据 (MC协议上下文已分离为 net_mc_meta/uart_mc_meta) */
struct uart_data
{
    uint8_t rx_status;                  /* 接收状态 */
    uint8_t tx_status;                  /* 发送状态 */
    volatile uint8_t dma_tx_busy;       /* TX DMA 忙标志(原为全局变量, 统一收归本结构体):
                                         * uartStartDmaTransfer 置1 → DMA1_Channel7 ISR 清0。
                                         * 软件标志消除了"仅查 CNTR/TC 寄存器"的时序窗口。 */
    uint8_t rx_buf_idx;                 /* 当前DMA活跃缓存索引: 0=rx_buf, 1=rx_buf_alt */
    uint8_t *tx_buf;                    /* 发送缓存 */
    uint16_t tx_buf_size;               /* 发送缓存大小 */
    uint8_t *rx_buf;                    /* 当前DMA接收缓存 */
    uint8_t *rx_buf_alt;                /* 备用DMA接收缓存（双缓冲） */
    uint16_t rx_buf_size;               /* 接收缓存大小 */
    uint16_t last_size;                 /* 上一次接收的大小 */

    uint16_t timeout_ms;                /* 超时时间（毫秒） */
    uint16_t max_retry;                 /* 最大重试次数 */
    uint16_t retry_count;               /* 当前重试次数 */
    uint32_t send_len;                  /* 发送长度 */
    uint32_t send_time;                 /* 发送时间戳 */

    USART_TypeDef  *huart;              /* 串口句柄 */
    DMA_Channel_TypeDef* hdma_tx;       /* 发送DMA通道 */
    DMA_Channel_TypeDef* hdma_rx;       /* 接收DMA通道 */
    
    uart_req_queue_t req_queue;         /* 新的发送请求队列(闭环数据流) */
    
    /* ─── ISR→主循环 延迟处理：帧解析从ISR移到主循环 ─── */
    uint8_t *pending_rx_buf;            /* ISR写入: 待处理的接收数据指针 */
    uint16_t pending_rx_len;            /* ISR写入: 待处理的接收数据长度 */
    volatile uint8_t rx_pending;        /* ISR置1: 有待处理的接收数据 */

    /* ─── 接收半帧累积状态("完整帧才切缓冲投递") ───
     * 串口应答可能被 IDLE 中断切成多片(对端帧内停顿)，
     * 故 ISR 仅在缓冲内构成完整帧时才切缓冲投递，否则保持 DMA 继续向
     * 同一缓冲追加，从根上消除"半帧"流入解析层。 */
    volatile uint8_t  rx_part_active;   /* ISR置1: 当前缓冲已累积但尚未成帧 */
    volatile uint32_t rx_part_tick;     /* ISR写入: 半帧起始时刻(ms)，供滞留看门狗判定 */
    volatile uint8_t  rx_part_reset;    /* 主循环置1: 请求ISR丢弃半帧并强制切换缓冲 */
    
    /* ─── 波特率预计算: 避免每次TX重复除法 ─── */
    uint32_t tx_us_per_byte;            /* 当前波特率下每字节发送微秒数 */
};

extern struct uart_data  uart_data_t;

int uartBaudRateSyncStateMachine(void);

uint8_t uartBaudRateSync_IsRunning(void);

void BSP_Uart_Init(uint32_t baudrate );

void uartTxTimeoutCheck(void);

int uartBaudRateSync(void);

int uartTxWithSocketID(uint8_t Sour_Sockid, uint8_t Dest_Sockid, uint8_t *data, uint16_t length);

uint32_t uartTxGetLastSeq(void);         /* 返回最近一次入队的串口请求序号(req_queue.last_enq_seq) */

uint8_t uartSendPacketLen(uint8_t Sour_Sockid, uint8_t Dest_Sockid, uint8_t *tx_buf, uint16_t tx_len);

void uartProcessDeferredRx(void);        /* 主循环中调用：处理ISR延迟的帧解析+出队+下一包 */

uint8_t uartRxPartialWatchdog(void);     /* 主循环中调用：半帧滞留超时则请求复位接收缓冲, 返回1=本次触发了复位 */

void DMA1_Channel7_IRQHandler(void);

void Analysis_usart_handler(uint8_t *buf, uint32_t len);

#endif /* end of bsp_uart.h */

