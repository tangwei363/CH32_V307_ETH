/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_uart.c
* Author             : WCH
* Version            : V1.0
* Date               : 2022/05/10
* Description        : uart cfg.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include "bsp_uart.h"
#include "string.h"
#include "eth_driver.h"
#include "bsp_wch_net.h"
#include "debug.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include "ethernet_app.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"
#include "mb_slave.h"            /* MB_Slave_NotifyFrameError(): 串口帧异常即时上报 */
/* Global Variable */

struct uart_data  uart_data_t;
uart_rx_ctx_t     uart_rx_ctx;             /* 串口解析路由上下文 (与 uart_mc_meta 同级分离) */
/* UART2 */
u8  UART2_RX_DMA_DataBuf[UART_RX_DMA_SIZE] = {0};        // 接收缓存A（双缓冲）
u8  UART2_RX_DMA_DataBuf_ALT[UART_RX_DMA_SIZE] = {0};    // 接收缓存B（双缓冲）

u8  UART2_TX_DMA_DataBuf[UART_TX_DMA_SIZE] = {0};        // 发送缓存

/*********************************************************************
 * 波特率同步状态机定义
 *
 * BaudrateTable 必须严格按"波特率从小到大"排列（数组下标即档位索引）：
 *     [0]9600   [1]19200  [2]38400  [3]57600  [4]115200 [5]115200
 *     [6]512000 [7]1000000 [8]2000000 [9]3000000
 * 最高档 = 索引 BAUDRATE_INDEX_MAX(3000000)，降档扫描即索引递减。
 * 注：[4] 与 [5] 数值相同为历史遗留，扫描时会自动跳过重复档位。
 *     硬件可行性：USART2 挂 APB1(PCLK1=48MHz)，16 倍过采样下上限 = PCLK1/16 = 3Mbps；
 *     3000000 对应 BRR=0x0010(USARTDIV=1)，是当前 PCLK1 下可分频出的精确值(误差 0)。
 */
static BaudSyncContext_t s_baud_sync_ctx = {0};
static const uint32_t BaudrateTable[] = {9600, 19200, 38400, 57600, 115200,115200,512000,1000000,2000000,3000000 };
#define BAUDRATE_TABLE_SIZE (sizeof(BaudrateTable) / sizeof(BaudrateTable[0]))
#define BAUDRATE_INDEX_MAX  ((uint8_t)(BAUDRATE_TABLE_SIZE - 1u))   /* 最高档索引 = 9 (3000000) */
#define BAUDRATE_INDEX_MIN  ((uint8_t)0u)                           /* 最低档索引 = 0 (9600)    */

volatile uint32_t g_ulSystemTick = 0;



void Tick_time_handler(void)
{
    g_ulSystemTick++;   // 串口超时检测
}

uint32_t Tick_time_get(void)
{
    return g_ulSystemTick;
}

/**********************************************************************
 *  @fn      UART_GPIO_Init
 *
 *  @brief   initialize uart2 GPIO
 *
 *  @return  none
 * */
void UART_GPIO_Init(void)
{
    GPIO_InitTypeDef  GPIO_InitStructure={0};
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);

    /* UART2 TX-->PA2   RX-->PA3 */
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_2;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_3;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IN_FLOATING;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

}

/**********************************************************************
 *  @fn      UART_DMA_Init
 *
 *  @brief   initialize uart2 DMA
 *
 *  @return  none
 * */
void UART_DMA_Init(void)
{
    DMA_InitTypeDef DMA_InitStructure = {0};
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    /* UART2  TX-->DMA1_Channel7   RX-->DMA1_Channel6 */
    DMA_DeInit(DMA1_Channel7);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&USART2->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)uart_data_t.tx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    //DMA_InitStructure.DMA_BufferSize = size(TX_DMA_dataBuf);
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_InitStructure.DMA_Priority = DMA_Priority_VeryHigh;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel7, &DMA_InitStructure);

    DMA_DeInit(DMA1_Channel6);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)(&USART2->DATAR);
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)uart_data_t.rx_buf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = (uint32_t)UART_RX_DMA_SIZE;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_Init(DMA1_Channel6, &DMA_InitStructure);
  
    DMA_ClearFlag(DMA1_IT_TC7);

    DMA_Cmd(DMA1_Channel6, ENABLE);
    USART_DMACmd(USART2,USART_DMAReq_Rx|USART_DMAReq_Tx,ENABLE);
}

/**********************************************************************
 *  @fn      UART_Interrupt_Init
 *
 *  @brief   initialize uart2 interrupt
 *
 *  @return  none
 * */
void UART_Interrupt_Init(void)
{
    NVIC_InitTypeDef NVIC_InitStructure = {0};

    /* UART2  TX-->DMA1_Channel7, RX uses IDLE interrupt */
    DMA_ITConfig(DMA1_Channel7,DMA_IT_TC,ENABLE);

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel7_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
}

/**********************************************************************
 *  @fn      BSP_Uart_Baudrate_Init
 *  @fn      UART_Interrupt_Init
 *
 *  @brief   initialize uart2 interrupt
 *
 *  @return  none
 * */
void BSP_Uart_Baudrate_Init(uint32_t baudrate)
{
    USART_InitTypeDef USART_InitStructure = {0};
    /* 配置串口参数 */
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_Even;  
    //USART_InitStructure.USART_Parity = USART_Parity_No;        
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(USART2, &USART_InitStructure);
    USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);  //开启空闲中断 
    USART_Cmd(USART2, ENABLE);

    /* 初始化DMA */
    UART_DMA_Init();
    uart_data_t.dma_tx_busy = 0;    /* 波特率切换后 DMA 重新初始化，清除忙标志，防止残留状态 */

    /* 初始化中断 */
    UART_Interrupt_Init();

    /* 预计算每字节发送微秒数，避免 uartStartDmaTx 每次除法：
     * 1字节 = 1起始 + 8数据 + 1校验 + 1停止 = 11位
     * tx_us_per_byte = 11 * 1_000_000 / baudrate  (向上取整) */
    uart_data_t.tx_us_per_byte = (11 * 1000000UL + baudrate - 1) / baudrate;
}
/**********************************************************************
 *  @fn      BSP_Uart_Init
 *
 *  @brief   初始化UART2
 *
 *  @param   baudrate - 串口波特率
 *
 *  @return  none
 *
 * @note    初始化流程：
 *          1. 初始化GPIO引脚（TX和RX）
 *          2. 配置串口句柄和DMA通道
 *          3. 设置发送和接收缓冲区
 *          4. 初始化发送参数和状态
 *          5. 创建数据缓存管理（发送队列）
 *          6. 配置串口参数
 *          7. 启用空闲中断
 *          8. 初始化DMA
 *          9. 初始化中断
 */
void BSP_Uart_Init(uint32_t baudrate )
{
    /* 初始化GPIO引脚 */
    UART_GPIO_Init();

    /* 配置串口句柄和DMA通道 */
    uart_data_t.huart = USART2;                             //串口句柄
    uart_data_t.hdma_tx = DMA1_Channel7 ;                   //发送DMA通道
    uart_data_t.hdma_rx = DMA1_Channel6;                    //接收DMA通道

    /* 设置发送和接收缓冲区 */
    uart_data_t.tx_buf = &UART2_TX_DMA_DataBuf[0];          //发送缓存 
    uart_data_t.tx_buf_size = UART_TX_DMA_SIZE;             //发送缓存大小
    uart_data_t.rx_buf = &UART2_RX_DMA_DataBuf[0];          // 当前DMA接收缓存
    uart_data_t.rx_buf_alt = &UART2_RX_DMA_DataBuf_ALT[0];  // 备用DMA接收缓存（双缓冲）
    uart_data_t.rx_buf_idx = 0;                             // 当前活跃缓存索引
    uart_data_t.rx_buf_size = UART_RX_DMA_SIZE;             // 接收缓存大小

    /* 初始化发送参数和状态 */
    uart_data_t.max_retry = UART_TX_RETRY_MAX;              // 最大重试次数
    uart_data_t.timeout_ms = UART_TX_TIMEOUT_MS;            // 超时时间（毫秒）
    uart_data_t.last_size = 0;                              // 累计发送长度
    uart_data_t.rx_status = 0;                              // 接收状态（0=空闲，1=忙）
    uart_data_t.tx_status = 0;                              // 发送状态（0=空闲，1=忙）
    uart_data_t.retry_count = 0;                            // 当前重试次数
    uart_data_t.send_time = g_ulSystemTick;                 // 记录发送时间戳

    /* 初始化socket ID（用于UART→ETH回传） */
    uart_rx_ctx.Sour_Sockid = 0xFF;
    uart_rx_ctx.Dest_Sockid = 0xFF;

    /* 初始化 ISR→主循环 延迟处理字段 */
    uart_data_t.pending_rx_buf = NULL;
    uart_data_t.pending_rx_len = 0;
    uart_data_t.rx_pending = 0;

    /* 初始化接收半帧累积状态 */
    uart_data_t.rx_part_active = 0;
    uart_data_t.rx_part_tick   = 0;
    uart_data_t.rx_part_reset  = 0;

    /* 初始化新的发送请求队列（闭环数据流） */
    uart_req_queue_init();

    BSP_Uart_Baudrate_Init(baudrate);

    s_baud_sync_ctx.baudrate = baudrate;    //保存波特率

    UART_DEBUG("UART 波特率 已初始化为: %u \r\n",baudrate);
}

/*
 * 说明: DMA 发送忙标志(dma_tx_busy)与请求序号(seq_counter/last_enq_seq)
 *       原为文件级/全局变量，现已统一收归结构体集中管理:
 *         - uart_data_t.dma_tx_busy            (见 struct uart_data)
 *         - uart_data_t.req_queue.seq_counter  (见 uart_req_queue_t)
 *         - uart_data_t.req_queue.last_enq_seq (见 uart_req_queue_t)
 *       定义与复位均在 uart_req_queue_init()/uart_data_t 全局实例中完成。
 */

/*********************************************************************
 * @fn      uartStartDmaTransfer
 *
 * @brief   启动 UART DMA 发送
 *
 * @param   buf - 数据缓冲区地址
 * @param   len - 数据长度
 *
 * @return  0  - 成功启动传输
 *         -1  - 上一次传输未完成（DMA 忙碌）
 *
 * @note    忙碌判定策略（多级检查，由快到慢）：
 *          ① 软件忙标志 uart_data_t.dma_tx_busy：最可靠，无寄存器时序问题
 *          ② TC 标志：如果 TC 已置位 → 传输确实完成，可直接覆盖旧状态
 *          ③ CNTR 寄存器：TC 未置位 + CNTR>0 → 传输仍在进行
 *          如果 ① 为真，直接返回 -1；
 *          如果 TC 已置位，说明 ISR 未来得及运行，清除后直接启动新传输。
 */
static int uartStartDmaTransfer(uint8_t *buf, uint16_t len)
{
    /*
     * 多级忙检查（由快到慢）：
     * ① dma_tx_busy=0 → 空闲，直接启动
     * ② dma_tx_busy=1 + TC=SET  → 传输刚完成、ISR 未来得及清标志，安全覆盖
     * ③ dma_tx_busy=1 + TC=RESET → 传输真的在进行中，返回忙
     */
    if (uart_data_t.dma_tx_busy) {
        if (DMA_GetFlagStatus(DMA1_FLAG_TC7) != RESET) {
            /* TC=SET: DMA 刚完成，ISR 未运行，主动清除后放行 */
            DMA_ClearFlag(DMA1_FLAG_TC7);
            uart_data_t.dma_tx_busy = 0;
        } else {
            /* TC=RESET: 传输确实在进行中 */
            log_warn("DMA TX busy, len:%d\r\n", len);
            return -1;
        }
    }
    /*
     * 关键：必须在 DMA_Cmd(ENABLE) 之前置位 dma_tx_busy！
     * 若在 ENABLE 之后置位，短传输（如1字节）的 DMA 可能在置位前
     * 已完成 → ISR 清 0 → 主线程再置 1 → 标志永久卡住。
     */
    uart_data_t.dma_tx_busy = 1;    /* 先标记忙，再启动硬件 */

    /* 配置并启动新传输 */
    DMA_Cmd(DMA1_Channel7, DISABLE);
    DMA1_Channel7->MADDR = (uint32_t)buf;
    DMA1_Channel7->CNTR = len;
    DMA_Cmd(DMA1_Channel7, ENABLE);  /* 启动后若 ISR 抢先执行，dma_tx_busy 被正确清 0 */

    return 0;
}

/*********************************************************************
 * @fn      BSP_Uart_SetBaudRate
 *
 * @brief   切换UART2波特率
 *
 * @param   baudrate - 新的波特率值
 *
 * @return  0 - 切换成功, -1 - 切换失败（串口忙）
 *
 * @note    切换流程：
 *          1. 检查当前发送状态，如果正在发送则返回失败
 *          2. 禁用USART2
 *          3. 重新配置波特率（含 DMA + 中断重初始化）
 *          4. 清除所有软件状态变量（请求队列、双缓冲索引、传输状态等）
 *          5. 重置协议上下文（uart_rx_ctx / uart_mc_meta）
 */
int BSP_Uart_SetBaudRate(uint32_t baudrate)
{
 
    /* 检查发送状态，如果正在发送则不能切换波特率 */
    if (uart_data_t.tx_status == BUSY) {
        log_warn("UART正在发送,无法切换波特率\r\n");
        return -1;
    }
    /* 禁用USART2以配置新参数，BSP_Uart_Baudrate_Init 内部已重配 DMA + 中断 */
    USART_Cmd(USART2, DISABLE);
    BSP_Uart_Baudrate_Init(baudrate);
    /* Clear IDLE flag by reading data register */
    USART_ReceiveData(USART2);
    
    /* ─── 清除硬件中断挂起标志，防止波特率切换后触发虚假中断 ─── */
    /* USART 中断标志：读 SR + DR 清除 IDLE/RXNE/ORE/PE/FE/NE */
    USART_ClearITPendingBit(USART2, USART_IT_TC);
    USART_ClearITPendingBit(USART2, USART_IT_TXE);
    USART_ClearITPendingBit(USART2, USART_IT_LBD);
    USART_ClearITPendingBit(USART2, USART_IT_IDLE);
    /* DMA 中断挂起标志：清除 RX(Ch6) 和 TX(Ch7) 的传输完成标志 */
    DMA_ClearITPendingBit(DMA1_IT_TC6);
    DMA_ClearITPendingBit(DMA1_IT_TC7);
    DMA_ClearITPendingBit(DMA1_IT_GL6);
    DMA_ClearITPendingBit(DMA1_IT_GL7);

    /* ─── 清除所有软件状态，防止旧波特率下的残留数据污染新通信 ─── */

    /* ISR→主循环 延迟处理：丢弃旧波特率下收到的数据 */
    uart_data_t.rx_pending     = 0;
    uart_data_t.pending_rx_buf = NULL;
    uart_data_t.pending_rx_len = 0;

    /* 半帧累积状态：旧波特率下的半帧作废，避免污染新速率下的组帧 */
    uart_data_t.rx_part_active = 0;
    uart_data_t.rx_part_reset  = 0;

    /* 传输状态：复位到空闲 */
    uart_data_t.tx_status   = IDLE;
    uart_data_t.rx_status   = 0;
    uart_data_t.retry_count = 0;
    uart_data_t.send_len    = 0;
    uart_data_t.send_time   = 0;

    /* 双缓冲：复位到主缓冲区 */
    uart_data_t.rx_buf_idx = 0;
    uart_data_t.rx_buf     = &UART2_RX_DMA_DataBuf[0];

    /* 请求队列：清空旧波特率下入队的所有请求 */
    uart_req_queue_clear();

    /* 协议上下文：复位 socket 路由和 MC 协议元数据 */
    memset(&uart_rx_ctx, 0, sizeof(uart_rx_ctx));
    memset(&uart_mc_meta, 0, sizeof(uart_mc_meta));

    UART_DEBUG("UART波特率已切换为: %u\r\n", baudrate);
    return 0;
}


/*********************************************************************
 * @fn      uartBaudRateSyncClearRx
 *
 * @brief   清空接收缓冲区并重置DMA
 *
 * @return  none
 */
static void uartBaudRateSyncClearRx(void)
{
    /* 当前DMA接收缓存 */
    memset(uart_data_t.rx_buf, 0, uart_data_t.rx_buf_size);
    /* 备用DMA接收缓存（双缓冲） */
    memset(uart_data_t.rx_buf_alt, 0, uart_data_t.rx_buf_size);

    DMA_Cmd(DMA1_Channel6, DISABLE);
    DMA1_Channel6->CNTR = UART_RX_DMA_SIZE;
    DMA_Cmd(DMA1_Channel6, ENABLE);

    /* 缓冲已清零（首字节非 STX），作废旧半帧累积状态 */
    uart_data_t.rx_part_active = 0;
    uart_data_t.rx_part_reset  = 0;
}

/*********************************************************************
 * @fn      uartBaudRateSyncSendAndWait
 *
 * @brief   发送数据并忙等响应。直接轮询 uart_data_t.rx_pending，
 *          检测 pending_rx_buf[0] 去校验位后是否匹配预期首字节。
 *          ISR 仅置 rx_pending，无任何同步分支，透传效率最优。
 *
 * @param   tx_buf - 发送数据缓冲区指针
 * @param   tx_len - 发送数据长度
 * @param   timeout_ms - 超时时间（毫秒）
 * @param   max_retry - 最大重试次数
 *
 * @return  1 - 收到有效响应
 *          0 - 超时
 */
static int uartBaudRateSyncSendAndWait(uint8_t *tx_buf, uint16_t tx_len,
                                        uint32_t timeout_ms, uint8_t max_retry)
{
    uint8_t retry = 0;
    uint8_t ret_heap = 0;
    while (retry <= max_retry) {
        /* 丢弃上次残留的 rx_pending，避免误判为本次响应 */
        uart_data_t.rx_pending = 0;

        if (uartStartDmaTransfer(tx_buf, tx_len) < 0) {
            Delay_Ms(10);
            retry++;
            continue;
        }

        if (tx_len) {
            UART_DEBUG("time_log = %u ,tx_len = %d \r\n", synch_time_get(), tx_len);
            UART_DEBUG("0x%02X ", tx_buf[0]);
            for (int i = 1; i < tx_len; i++) {
                if (i > tx_len - 1 - 3) {
                    UART_DEBUG(" 0x%02x %C%C ",tx_buf[i],tx_buf[i+1],tx_buf[i+2]);
                    i += 3;
                } else {
                    UART_DEBUG("%C",tx_buf[i]);
                }
            }
            UART_DEBUG("\r\n");
        }
        
        /* 忙等: ISR 置 rx_pending 后直接检测 pending_rx_buf[0] */
        uint32_t start_time = g_ulSystemTick;
        while (!uart_data_t.rx_pending) {
            if ((g_ulSystemTick - start_time) >= timeout_ms)
                break;
        }
        
        if (uart_data_t.rx_pending) {
            uint8_t first_byte = uart_data_t.pending_rx_buf[0] & 0x7F;
            if (uart_data_t.pending_rx_len) {
                UART_DEBUG("time_log = %u ,rx_len = %d, buf[0]=0x%02X \r\n",
                     synch_time_get(), 
                     uart_data_t.pending_rx_len,
                     first_byte);
                for (int i = 0; i < uart_data_t.pending_rx_len; i++) {
                    UART_DEBUG("%02X ", uart_data_t.pending_rx_buf[i] & 0x7F);
                }
                UART_DEBUG("\r\n");
            }
            uart_data_t.rx_pending = 0;
            ret_heap = (first_byte == 0x02 || first_byte == 0x05 ||
                        first_byte == 0x06 || first_byte == 0x15);
        }
        if( ret_heap == 0 ){
            retry++;  
            Delay_Ms(100);
        }else{
            return 1;
        }
    }
    return 0;
}

/*********************************************************************
 * @fn      uartBaudRateSync_IsRunning  // 波特率ID范围: 0-4 (对应9600,19200,38400,57600,115200)
 *
 * @brief   判断波特率同步状态机是否正在运行
 *
 * @return  1 - 同步进行中, 0 - 空闲或已完成
 */
uint8_t uartBaudRateSync_IsRunning(void)
{
    return (s_baud_sync_ctx.state > BAUD_SYNC_STATE_INIT &&
            s_baud_sync_ctx.state < BAUD_SYNC_STATE_SUCCESS) ? 1 : 0;
}

/*********************************************************************
 * @fn      uartBaudRateSyncStateMachine
 *
 * @brief   波特率同步状态机 —— 从最高波特率(3000000)开始，逐档向下扫描搜索
 *
 * @return  0  - 同步成功，s_baud_sync_ctx.baudrate 即锁定的波特率
 *          -1 - 同步失败（已回落至 BAUD_SYNC_FALLBACK_BAUD，交由上层处理）
 *
 * @note    【扫描顺序】档位索引由高到低递减：
 *            [9]3000000 → [8]2000000 → [7]1000000 → [6]512000 → [5]115200
 *            → [3]57600 → [2]38400 → [1]19200 → [0]9600
 *            （[4] 与 [5] 数值相同，扫描时自动跳过，避免重复耗时）
 *
 *          【命中判据】某一档位下 5 帧握手序列全部收到有效应答即判为命中，
 *            立即锁定该档位并返回成功，不再向更低档继续扫描。
 *
 *          【状态转换】
 *            INIT ─? APPLY_RATE ─成功─? SEND_SYNC_05 ─成功─? SEND_00E0202
 *              ▲         └─切档失败(重试耗尽)─? SCAN_NEXT
 *              │
 *              │   SEND_00E0202 ─成功─? SEND_00ECA02 ─成功─? SEND_E000EE8
 *              │        │                    │                    │
 *              │        └──── 超时/应答无效 ──┴────────────────────┘
 *              │                                   ▼
 *              │                              SCAN_NEXT
 *              │                                   │  索引可再降 ─? APPLY_RATE
 *              │                                   └─ 已到最低档 ─? ROUND_END
 *              │                                                     │
 *              └──── 回到最高档重扫 (轮次未满) ?──────────────────────┤
 *                                                                    └─ 轮次用尽 ─? FAILED
 *
 *            SEND_E000EE8 ─成功─? SEND_E100EEC ─成功─? SUCCESS（锁定当前档位）
 *
 *          【超时处理】单帧的超时与重试由 uartBaudRateSyncSendAndWait 内部完成：
 *            每次尝试等待 timeout_ms，失败后延时 100ms 再试，共 max_retry+1 次；
 *            全部失败返回 0，由本状态机执行降档。
 *          【错误恢复】整轮（全部档位）扫描结束后最多重扫 BAUD_SYNC_SCAN_ROUND_MAX
 *            轮；仍失败则回落 BAUD_SYNC_FALLBACK_BAUD 并返回 -1。
 */
int uartBaudRateSyncStateMachine(void)
{
    BaudSyncContext_t *ctx = &s_baud_sync_ctx;
    uint8_t ret;

    /* ─── 首次进入：定位到最高档，作为降档扫描的起点 ─── */
    if (ctx->state == BAUD_SYNC_STATE_INIT) {
        ctx->is_sync_flag = 0;
        ctx->scan_index   = 7;                  /* 最高档索引(9) */
        ctx->baudrate     = BaudrateTable[7];   /* 3000000 bps */
        ctx->fail_count   = 0;
        ctx->apply_retry  = 0;
        ctx->scan_round   = 0;
        BSP_DEBUG("波特率同步: 从最高档 %u bps 开始降档扫描\r\n", ctx->baudrate);
    }

    while (1)
    {
        switch (ctx->state) {
            case BAUD_SYNC_STATE_INIT: {
                uartBaudRateSyncClearRx();
                ctx->apply_retry = 0;
                ctx->state = BAUD_SYNC_STATE_APPLY_RATE;
                break;
            }
            
            /* ── 应用当前档波特率到硬件，并等待链路稳定 ──
             *     成功 → 进入握手；失败(串口忙)重试，重试耗尽 → 降档 */
            case BAUD_SYNC_STATE_APPLY_RATE: {
                if (BSP_Uart_SetBaudRate(ctx->baudrate) == 0) {
                    uartBaudRateSyncClearRx();          /* 丢弃旧速率下的残留数据 */
                    Delay_Ms(BAUD_SYNC_DELAY_MS);       /* 等收发器/PLC 侧时钟稳定 */
                    ctx->apply_retry = 0;
                    ctx->state = BAUD_SYNC_STATE_SEND_SYNC_05;
                } else if (++ctx->apply_retry >= BAUD_SYNC_APPLY_MAX_RETRY) {
                    BSP_DEBUG("波特率同步: 硬件切换到 %u 失败, 降档\r\n", ctx->baudrate);
                    ctx->apply_retry = 0;
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                } else {
                    Delay_Ms(BAUD_SYNC_DELAY_MS);       /* 等发送结束后再试 */
                }
                break;
            }
            
            /* ── 握手①：0x05 探测帧（最廉价，用于快速排除错误档位）── */
            case BAUD_SYNC_STATE_SEND_SYNC_05: {
                uint8_t sync_data[2] = {0x05,0x05};
                ret = uartBaudRateSyncSendAndWait((uint8_t*)sync_data, 1,
                                                  BAUD_SYNC_PROBE_TIMEOUT_MS,
                                                  BAUD_SYNC_PROBE_MAX_RETRY);
                if (ret) {
                    ctx->state = BAUD_SYNC_STATE_SEND_00E0202;
                } else {
                    UART_DEBUG("time_log=%u, %u bps 探测帧无应答, 降档\r\n",
                               synch_time_get(), ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                }
                break;
            }
            
            /* ── 握手②：\STX00E0202\ETX6C ── */
            case BAUD_SYNC_STATE_SEND_00E0202: {
                const uint8_t send_data[] = {0x02,0x30,0x30,0x45,0x30,0x32,0x30,0x32,0x03,0x36,0x43};
                ret = uartBaudRateSyncSendAndWait((uint8_t*)send_data, sizeof(send_data),
                                                  BAUD_SYNC_TIMEOUT_MS, BAUD_SYNC_MAX_RETRY);
                if (ret) {
                    ctx->state = BAUD_SYNC_STATE_SEND_00ECA02;
                } else {
                    UART_DEBUG("time_log=%u, %u bps 握手②失败, 降档\r\n",
                               synch_time_get(), ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                }
                break;
            }
            
            /* ── 握手③：\STX00ECA02\ETX8E ── */
            case BAUD_SYNC_STATE_SEND_00ECA02: {
                const uint8_t send_data[] = {0x02,0x30,0x30,0x45,0x43,0x41,0x30,0x32,0x03,0x38,0x45};
                ret = uartBaudRateSyncSendAndWait((uint8_t*)send_data, sizeof(send_data),
                                                  BAUD_SYNC_TIMEOUT_MS, BAUD_SYNC_MAX_RETRY);
                if (ret) {
                    ctx->state = BAUD_SYNC_STATE_SEND_E000EE8;
                } else {
                    UART_DEBUG("time_log=%u, %u bps 握手③失败, 降档\r\n",
                               synch_time_get(), ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                }
                break;
            }
            
            /* ── 握手④：\STXE000EE804\ETXFE ── */
            case BAUD_SYNC_STATE_SEND_E000EE8: {
                const uint8_t send_data[] = {0x02,0x45,0x30,0x30,0x30,0x45,0x45,0x38,0x30,0x34,0x03,0x46,0x45};
                ret = uartBaudRateSyncSendAndWait((uint8_t*)send_data, sizeof(send_data),
                                                  BAUD_SYNC_TIMEOUT_MS, BAUD_SYNC_MAX_RETRY);
                if (ret) {
                    ctx->state = BAUD_SYNC_STATE_SEND_E100EEC;
                } else {
                    UART_DEBUG("time_log=%u, %u bps 握手④失败, 降档\r\n",
                               synch_time_get(), ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                }
                break;
            }
            
            /* ── 握手⑤（终判）：\STXE100EEC04EDB17066\ETXD9 ──
             *     通过 → 该档位即为目标波特率，锁定并结束同步 */
            case BAUD_SYNC_STATE_SEND_E100EEC:
            {
                //\STXE100EEC04 68722579 \ETXB8  
                //const uint8_t send_data[] = {0x02,0x45,0x31,0x30,0x30,0x45,0x45,0x43,0x30,0x34,0x36,0x38,0x37,0x32,0x32,0x35,0x37,0x39,0x03,0x42,0x38};
                // 0x02 E100EEC04 EDB17066 0x03 D9
                const uint8_t send_data[] = {0x02,0x45,0x31,0x30,0x30,0x45,0x45,0x43,0x30,0x34,0x45,0x44,0x42,0x31,0x37,0x30,0x36,0x36,0x03,0x44,0x39};
                ret = uartBaudRateSyncSendAndWait((uint8_t*)send_data, sizeof(send_data),
                                                  BAUD_SYNC_TIMEOUT_MS, BAUD_SYNC_MAX_RETRY);
                if (ret) 
                {
                    /* 5 帧握手全部通过：当前档位即为目标波特率，锁定并结束同步 */
                    ctx->is_sync_flag = 1;
                    ctx->state = BAUD_SYNC_STATE_SUCCESS;
                    BSP_DEBUG("波特率同步成功: 锁定档位[%u] = %u bps\r\n",
                              ctx->scan_index, ctx->baudrate);
                } else {
                    UART_DEBUG("time_log=%u, %u bps 握手⑤失败, 降档\r\n",
                               synch_time_get(), ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_SCAN_NEXT;
                }
                break;
            }
            /* ── 降档：索引递减到下一档，切换硬件波特率后重新握手 ──
             *     已到最低档仍失败 → 本轮扫描结束，转 ROUND_END */
            case BAUD_SYNC_STATE_SCAN_NEXT: {
                ctx->fail_count++;

                if (ctx->scan_index > BAUDRATE_INDEX_MIN) {
                    /* 索引递减；并跳过与刚扫描档位数值相同的档位(表中 115200 出现两次) */
                    do {
                        ctx->scan_index--;
                    } while (ctx->scan_index > BAUDRATE_INDEX_MIN &&
                             BaudrateTable[ctx->scan_index] == ctx->baudrate);

                    ctx->baudrate    = BaudrateTable[ctx->scan_index];
                    ctx->apply_retry = 0;
                    BSP_DEBUG("波特率同步: 已失败 %u 档, 降至[%u] = %u bps\r\n",
                              ctx->fail_count, ctx->scan_index, ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_APPLY_RATE;
                } else {
                    BSP_DEBUG("波特率同步: 最低档 %u bps 仍无应答, 本轮扫描结束\r\n", ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_ROUND_END;
                }
                break;
            }
            
            /* ── 错误恢复：整轮（全部档位）扫描结束 ──
             *     轮次未满 → 回到最高档重新扫描(PLC 可能刚上电/速率未稳定)
             *     轮次用尽 → 回落兜底波特率，判定同步失败 */
            case BAUD_SYNC_STATE_ROUND_END: {
                ctx->scan_round++;

                if (ctx->scan_round < BAUD_SYNC_SCAN_ROUND_MAX) {
                    ctx->scan_index  = BAUDRATE_INDEX_MAX;
                    ctx->baudrate    = BaudrateTable[BAUDRATE_INDEX_MAX];
                    ctx->fail_count  = 0;
                    ctx->apply_retry = 0;
                    BSP_DEBUG("波特率同步: 第 %u 轮重扫, 回到最高档 %u bps\r\n",
                              ctx->scan_round + 1, ctx->baudrate);
                    ctx->state = BAUD_SYNC_STATE_APPLY_RATE;
                } else {
                    /* 兜底：回落兼容性最好的 9600，保证上层通信流程仍可启动 */
                    ctx->baudrate = BAUD_SYNC_FALLBACK_BAUD;
                    BSP_Uart_SetBaudRate(ctx->baudrate);
                    uartBaudRateSyncClearRx();
                    ctx->state = BAUD_SYNC_STATE_FAILED;
                }
                break;
            }

            /* ── 同步成功（终态）── */
            case BAUD_SYNC_STATE_SUCCESS: {
                BSP_DEBUG("同步PLC成功, 当前波特率 = %u bps\r\n", ctx->baudrate);
                return 0;
            }
            
            /* ── 同步失败（终态）── */
            case BAUD_SYNC_STATE_FAILED:
            default: {
                ctx->is_sync_flag = 0;
                UART_DEBUG("波特率同步失败, 已回落波特率 = %u bps\r\n", ctx->baudrate);
#if BAUD_SYNC_RESET_ON_ALL_FAILED
                /* 可选：沿用改造前的复位重试行为（PLC 长期离线时会形成复位循环） */
                Delay_Ms(10);
                NVIC_SystemReset();
#endif
                return -1;
            }
        }

    }
}
/*********************************************************************
 * @fn      uartResetToIdle
 *
 * @brief   重置UART状态为空闲，清理发送状态和接收状态
 *
 * @param   debug_msg - 调试信息字符串（可为NULL）
 *
 * @return  none
 *
 * @note    调用时机：
 *          1. uartHandleRxSuccess() - 接收成功且队列为空时
 *          2. uartTxTimeoutCheck() - 接收成功/超时丢弃后队列为空时
 *          
 *          状态清理内容：
 *          - tx_status: 发送状态 → IDLE（允许新请求进入快速路径）
 *          - rx_status: 接收状态 → 0（复位接收完成标志）
 *          - send_len: 发送长度 → 0
 *          - last_size: 累计发送长度 → 0
 *          
 *          注意：此函数不负责清理 in_flight_ctx，该上下文在响应处理时已使用完毕
 */
static void uartResetToIdle(const char *debug_msg)
{
    /* 重置发送相关状态 */
    uart_data_t.send_len = 0;
    
    uart_data_t.last_size = 0;
    
    /* 设置发送状态为空闲，允许新请求进入快速路径 */
    uart_data_t.tx_status = IDLE;
    
    /* 复位接收状态，为下一次接收做准备 */
    uart_data_t.rx_status = 0;

    uart_data_t.retry_count = 0;

    uart_data_t.send_time = 0;

    /* 清除 ISR→主循环 延迟处理标志（避免残留状态） */
    uart_data_t.rx_pending = 0;


    /* 调试日志输出 */
    if (debug_msg != NULL) {
        UART_DEBUG("time_log = %u IdleState \n %s\r\n", synch_time_get(), debug_msg);
    }
}

/* ========================================================================
 * 发送请求队列操作: 闭环数据流管理
 * 设计理念: 发送入队保存数据，接收出队解析数据，保证请求-响应配对正确
 * ======================================================================== */

/*********************************************************************
 * @fn      uart_req_queue_init
 *
 * @brief   初始化发送请求队列
 *
 * @return  none
 */
void uart_req_queue_init(void)
{
    uart_data_t.req_queue.head = 0;
    uart_data_t.req_queue.tail = 0;
    uart_data_t.req_queue.count = 0;
    uart_data_t.req_queue.ring_wp = 0;
    uart_data_t.req_queue.ring_used = 0;
    uart_data_t.req_queue.ring_wp = 0;
    uart_data_t.req_queue.ring_used = 0;
    
    /* 请求序号字段的复位策略（重要，勿随意改为归零）:
     * seq_counter / last_enq_seq 均不在本函数内复位。原因:
     *   本函数除开机(BSP_Uart_Init)外，还会在网线拔出时被
     *   Wizchip_PHY_Link_Disconnect() 调用。若在此复位计数器，
     *   重连后序号会从头开始，与断线前遗留的事务记录发生数值重叠，
     *   破坏"序号全程单调唯一"这一请求-响应对令牌的根本前提。
     *   二者仅依赖 bss 上电清零；uart_rx_ctx 的复位由 BSP_Uart_Init
     *   中的 memset 统一负责，无需在此重复。 */

    /* 初始化中断→主循环的异步处理标志 */
    uart_data_t.rx_pending = 0;

    uartResetToIdle(NULL);
}

/*********************************************************************
 * @fn      uart_req_queue_is_empty
 *
 * @brief   检查队列是否为空
 *
 * @return  1 - 空, 0 - 非空
 */
uint8_t uart_req_queue_is_empty(void)
{
    return (uart_data_t.req_queue.count == 0);
}

/*********************************************************************
 * @fn      uart_req_queue_is_full
 *
 * @brief   检查队列是否已满
 *
 * @return  1 - 满, 0 - 未满
 */
uint8_t uart_req_queue_is_full(void)
{
    return (uart_data_t.req_queue.count >= UART_REQ_QUEUE_SIZE);
}

/*********************************************************************
 * @fn      uart_req_queue_enqueue
 *
 * @brief   将请求数据入队(高效环形队列)
 *
 * @param   Sour_Sockid - 源socket ID
 * @param   Dest_Sockid - 目标socket ID
 * @param   mc_meta - MC协议上下文
 * @param   eth_seq_num - 请求序号
 * @param   data - 待发送数据
 * @param   len - 数据长度
 *
 * @return  0 - 成功, -1 - 队列已满
 *
 * @note    O(1)时间复杂度，仅更新tail指针
 */
int uart_req_queue_enqueue(uint8_t Sour_Sockid, uint8_t Dest_Sockid,
                            NET_MC_Recv_Resp_t *mc_meta, uint32_t eth_seq_num,
                            uint8_t *data, uint16_t len)
{
    /* 禁用中断保护队列操作 */
    __disable_irq();
    
    if (uart_req_queue_is_full()) {
        __enable_irq();
        UART_DEBUG("UART req queue full, reject\r\n");
        return -1;
    }
    
    uart_req_queue_node_t *node = &uart_data_t.req_queue.nodes[uart_data_t.req_queue.tail];
    
    /* 复制请求上下文 */
    node->Sour_Sockid = Sour_Sockid;
    node->Dest_Sockid = Dest_Sockid;
    node->mc_meta = *mc_meta;
    node->eth_seq_num = eth_seq_num;

    /* 记录"最近入队序号"：入队是全项目串口请求的唯一入口，
     * 上层据此把刚下发的串口命令与其响应严格配对 */
    uart_data_t.req_queue.last_enq_seq = eth_seq_num;
    
    /* 复制发送数据到共享环形缓冲(节点仅记录偏移/长度, 省去每节点600字节) */
    if (len > UART_REQ_DATA_MAX_LEN) {
        len = UART_REQ_DATA_MAX_LEN;
    }
    /* 环形数据空间判满: 已用字节 + 本次长度 不得超过容量 */
    if (uart_data_t.req_queue.ring_used + len > UART_REQ_RING_CAP) {
        __enable_irq();
        UART_DEBUG("UART req ring data full, reject\r\n");
        return -1;
    }
    node->data_off = uart_data_t.req_queue.ring_wp;
    node->data_len = len;
    if (data != NULL && len > 0) {
        uint16_t wp = uart_data_t.req_queue.ring_wp;
        if (wp + len <= UART_REQ_RING_CAP) {
            memcpy(&uart_data_t.req_queue.ring[wp], data, len);
        } else {
            uint16_t first = (uint16_t)(UART_REQ_RING_CAP - wp);
            memcpy(&uart_data_t.req_queue.ring[wp], data, first);
            memcpy(&uart_data_t.req_queue.ring[0], data + first, (uint16_t)(len - first));
        }
        uart_data_t.req_queue.ring_wp = (uint16_t)((wp + len) & (UART_REQ_RING_CAP - 1));
        uart_data_t.req_queue.ring_used = (uint16_t)(uart_data_t.req_queue.ring_used + len);
    }
    
    /* 初始化发送控制信息 */
    node->send_time = g_ulSystemTick;
    node->retry_count = 0;
    
    /* 环形队列指针更新(取模, UART_REQ_QUEUE_SIZE=10 非2的幂) */
    uart_data_t.req_queue.tail = (uart_data_t.req_queue.tail + 1) % UART_REQ_QUEUE_SIZE;
    uart_data_t.req_queue.count++;
    
    /* 启用中断 */
    __enable_irq();
    
    // UART_DEBUG("UART req enqueue: Sour=%d, Dest=%d, seq=%u, len=%d, count=%u\r\n",
    //            Sour_Sockid, Dest_Sockid, (unsigned int)eth_seq_num, len, 
    //            (unsigned int)uart_data_t.req_queue.count);
    
    return 0;
}

/*********************************************************************
 * @fn      uart_req_queue_front
 *
 * @brief   获取队首节点指针(不删除)
 *
 * @return  队首节点指针，队列为空时返回NULL
 */
uart_req_queue_node_t* uart_req_queue_front(void)
{
    if (uart_req_queue_is_empty()) {
        return NULL;
    }
    return &uart_data_t.req_queue.nodes[uart_data_t.req_queue.head];
}

/*********************************************************************
 * @fn      uart_req_queue_dequeue
 *
 * @brief   队首节点出队
 *
 * @note    O(1)时间复杂度，仅更新head指针
 */
void uart_req_queue_dequeue(void)
{
    /* 禁用中断保护队列操作 */
    __disable_irq();
    
    if (uart_req_queue_is_empty()) {
        __enable_irq();
        return;
    }
    
    /* 释放该节点占用的环形数据(FIFO顺序保证其数据位于已用区起点) */
    uart_req_queue_node_t *node = &uart_data_t.req_queue.nodes[uart_data_t.req_queue.head];
    uint16_t freed = node->data_len;
    if (uart_data_t.req_queue.ring_used >= freed) {
        uart_data_t.req_queue.ring_used = (uint16_t)(uart_data_t.req_queue.ring_used - freed);
    } else {
        uart_data_t.req_queue.ring_used = 0;
    }
    /* 环形队列指针更新(取模, UART_REQ_QUEUE_SIZE=10 非2的幂) */
    uart_data_t.req_queue.head = (uart_data_t.req_queue.head + 1) % UART_REQ_QUEUE_SIZE;
    uart_data_t.req_queue.count--;
    
    /* 启用中断 */
    __enable_irq();
    
    UART_DEBUG("UART req dequeue: count=%d\r\n", uart_data_t.req_queue.count);
}

/*********************************************************************
 * @fn      uart_req_queue_clear
 *
 * @brief   清空发送请求队列
 *
 * @note    用于PHY Link断开或网络异常时清除所有缓存数据
 *          网络断开后，继续处理队列中的请求没有意义
 *
 * @return  none
 */
void uart_req_queue_clear(void)
{
    /* 禁用中断保护队列操作 */
    __disable_irq();
        
    /* 清空队列：重置头尾指针和计数, 以及环形缓冲 */
    uart_data_t.req_queue.head = 0;
    uart_data_t.req_queue.tail = 0;
    uart_data_t.req_queue.count = 0;
    uart_data_t.req_queue.ring_wp = 0;
    uart_data_t.req_queue.ring_used = 0;
    uart_data_t.req_queue.ring_wp = 0;
    uart_data_t.req_queue.ring_used = 0;
    
    /* 启用中断 */
    __enable_irq();
    
    UART_DEBUG("UART req queue cleared\r\n");
}

/*********************************************************************
 * @fn      uart_req_queue_peek_tail
 *
 * @brief   获取队尾节点指针(下一个入队位置，不入队)
 *
 * @return  队尾节点指针，队列已满时返回NULL
 */
uart_req_queue_node_t* uart_req_queue_peek_tail(void)
{
    if (uart_req_queue_is_full()) {
        return NULL;
    }
    return &uart_data_t.req_queue.nodes[uart_data_t.req_queue.tail];
}

/* ========================================================================
 * End of 发送请求队列操作
 * ======================================================================== */

/*********************************************************************
 * @fn      uartStartDmaTx
 *
 * @brief   启动DMA发送（公共函数）
 *
 * @note    调用前需确保已设置好:
 *          - uart_data_t.send_len  (数据长度)
 *          - uart_data_t.tx_buf    (数据已在缓冲区)
 *          - uart_rx_ctx.Sour_Sockid / Dest_Sockid 路由信息
 *            uart_mc_meta.sub_header / Format_Code / device_type / start_device / device_count
 *
 *          本函数负责: 计算超时 → 设置tx_status=BUSY → 启动DMA
 */
static int uartStartDmaTx(void)
{

    int ret = uartStartDmaTransfer(uart_data_t.tx_buf, uart_data_t.send_len);
    if (ret < 0) {
        return ret;  /* DMA 启动失败，不修改 tx_status，由调用方决定重试 */
    }
    uart_data_t.send_time = g_ulSystemTick;  /* 发送时间戳 */
    uart_data_t.timeout_ms = BAUD_SYNC_TIMEOUT_MS ;
    uart_data_t.rx_status  = 0;
    uart_data_t.retry_count = 0;
    uart_data_t.tx_status  = BUSY;

    UART_DEBUG("t:%ums,UART Dma Tx, len=%d, seq=%u, count=%u\r\n",
               synch_time_get(), uart_data_t.send_len,
               (unsigned int)uart_data_t.req_queue.nodes[uart_data_t.req_queue.head].eth_seq_num,
               (unsigned int)uart_data_t.req_queue.count );

    #ifdef _TRANSMISSION_DEBUG
        TRANSMISSION_DEBUG("0x%02x ", uart_data_t.tx_buf[0]);
        for (uint16_t i = 1; i < uart_data_t.send_len - 1; i++) {
            if (i > uart_data_t.send_len - 1 - 3) {
                TRANSMISSION_DEBUG(" 0x%02x %C%C ", uart_data_t.tx_buf[i], uart_data_t.tx_buf[i+1], uart_data_t.tx_buf[i+2]);
                i += 3;
            } else {
                TRANSMISSION_DEBUG("%C", uart_data_t.tx_buf[i]);
            }
        }
        TRANSMISSION_DEBUG("\r\n");
    #endif

    return 0;
}

/*********************************************************************
 * @fn      uartSendPacketLen
 *
 * @brief   通过DMA发送指定长度的UART数据包
 *
 * @param   Sour_Sockid / Dest_Sockid - socket ID
 * @param   tx_buf - 待发送数据指针
 * @param   tx_len - 待发送数据长度
 *
 * @return  1-成功启动发送, 0-参数无效
 */
uint8_t uartSendPacketLen(uint8_t Sour_Sockid,uint8_t Dest_Sockid,uint8_t *tx_buf, uint16_t tx_len)
{
    if (tx_len == 0 || tx_len > UART_TX_DMA_SIZE || tx_buf == NULL) {
        return 0;
    }

    uart_data_t.send_len = tx_len;
    uart_rx_ctx.Sour_Sockid  = Sour_Sockid;
    uart_rx_ctx.Dest_Sockid  = Dest_Sockid;
    memcpy(uart_data_t.tx_buf, tx_buf, tx_len);
    if (uartStartDmaTx() < 0) {
        return 0;  /* DMA 启动失败 */
    }
    return 1;
}


/* UART发送队列追加的元数据长度: socket ID(2B) + NET_MC_Recv_Resp_t(14B) + eth_seq_num(4B) */
#define UART_TX_META_LEN  (2 + sizeof(NET_MC_Recv_Resp_t) + sizeof(uint32_t))

/*********************************************************************
 * @fn      uartSendNextPacket
 *
 * @brief   从队列读取下一个数据包并启动发送（带 DMA 启动重试 + 失败跳过）
 *
 * @return   0 - 成功发送
 *          -1 - 队列为空（所有数据包已发完或被跳过）
 *
 * @note     DMA 启动可能因上一帧 ISR 未运行完而返回 -1，
 *           每个数据包最多重试 3 次；若仍失败则跳过该包继续下一个。
 *           循环直到成功发送一个包或队列耗尽。
 * 
 * @note    使用新的 req_queue 替代旧的 BufferManage 队列
 */
static int uartSendNextPacket(uint8_t save_ctx)
{
    /* 获取队首节点 */
    uart_req_queue_node_t *node = uart_req_queue_front();
    if (node == NULL) {
        return -1;
    }
    /* 从环形缓冲读取该节点数据(可能跨回绕, 分两段拷到连续 tx_buf 喂DMA) */
    {
        uint16_t off = node->data_off;
        uint16_t n   = node->data_len;
        if (off + n <= UART_REQ_RING_CAP) {
            memcpy(uart_data_t.tx_buf, &uart_data_t.req_queue.ring[off], n);
        } else {
            uint16_t first = (uint16_t)(UART_REQ_RING_CAP - off);
            memcpy(uart_data_t.tx_buf, &uart_data_t.req_queue.ring[off], first);
            memcpy(uart_data_t.tx_buf + first, &uart_data_t.req_queue.ring[0], (uint16_t)(n - first));
        }
        uart_data_t.send_len = n;
    }
    /* 保存路由上下文（调用方可传 0 跳过，由外部自行管理上下文） */
    if (save_ctx) {
        uart_rx_ctx.Sour_Sockid = node->Sour_Sockid;
        uart_rx_ctx.Dest_Sockid = node->Dest_Sockid;
        uart_rx_ctx.eth_seq_num = node->eth_seq_num;
        uart_mc_meta = node->mc_meta;
    }
    node->send_time = g_ulSystemTick;

    /* DMA 启动重试：最多 3 次，重试间让出 CPU 等待 ISR 清 dma_tx_busy */
    for (int retry = 0; retry < 3; retry++) 
    {
        if (uartStartDmaTx() == 0) {
            return 0;  /* 发送成功，不出队，等响应到达后再出队 */
        }
        UART_DEBUG("UART DMA start busy, retry=%d\r\n", retry + 1);
    }
    /* DMA 启动失败，不丢弃请求，返回错误让调用者稍后重试。 */
    if (!save_ctx) {
        /* 调用方跳过了上下文保存，失败时应补保存以保证重试时上下文正确 */
        uart_rx_ctx.Sour_Sockid = node->Sour_Sockid;
        uart_rx_ctx.Dest_Sockid = node->Dest_Sockid;
        uart_rx_ctx.eth_seq_num = node->eth_seq_num;
        uart_mc_meta = node->mc_meta;
    }

    return -1;  /* 返回错误，但不移除队首，保持请求顺序 */
}



/*********************************************************************
 * @fn      uartTxWithSocketID
 *
 * @brief   将数据从以太网发送到UART，并使用新队列机制保存请求上下文
 *
 * @param   Sour_Sockid - 源socket ID（以太网socket）
 *          Dest_Sockid - 目标socket ID（服务器socket）
 *          data - 待发送的数据指针
 *          length - 数据长度
 *
 * @return  0 - 成功, -1 - 队列满或DMA启动失败
 *
 * @note    闭环数据流机制（发送入队，接收出队）：
 *          1. 所有请求都入队到 req_queue（包含数据和上下文）
 *          2. UART空闲时，从队列取出队首并发送
 *          3. PLC响应到达时，从队列取出队首获取上下文进行路由
 *          4. 队列由请求-响应对组成，保证多客户端并发时不会混淆
 *
 *          优势：
 *          - 统一的队列入队/出队机制，逻辑清晰
 *          - O(1)时间复杂度的入队/出队操作
 *          - 请求和响应严格配对，不会出现上下文覆盖问题
 */
int uartTxWithSocketID(uint8_t Sour_Sockid, uint8_t Dest_Sockid, uint8_t *data, uint16_t length)
{
    /* 1. 生成请求序号(计数器已收归 req_queue，与 last_enq_seq 同域集中管理) */
    uint32_t seq = ++uart_data_t.req_queue.seq_counter;
    //UART_DEBUG("入队编号=%u, SocketID=%d  len=%u \r\n", (unsigned int)seq, Sour_Sockid, length);
 
    /* 2. 将请求入队（统一入口，无论是快速路径还是慢速路径） */
    if (uart_req_queue_enqueue(Sour_Sockid, Dest_Sockid,
                                &net_mc_meta, seq,
                                data, length) < 0) {
        /* 队列已满，入队失败 */
        log_err("UART req queue full, reject\r\n");
        return -1;
    }
    /* 3. 发送前设置 BUSY，防止后续请求覆盖队列 */
    uart_data_t.tx_status = BUSY;
    /* 4. 如果是第一个请求（队列中只有1个），立即发送 */
    /* 如果队列中已有其他请求等待，当前请求已在队列中，会在响应到达后自动发送下一个 */
    if (uart_data_t.req_queue.count == 1) {
        /* 队列为空闲状态后第一个请求，立即取出并发送 */
        uart_req_queue_node_t *node = uart_req_queue_front();
        if (node != NULL) {
            /* 优化：快速路径直接从源指针拷贝到 tx_buf，省去额外中间拷贝 */
            /* （数据已写入环形缓冲 ring, 供慢速路径 uartSendNextPacket 按 data_off 读取） */
            memcpy(uart_data_t.tx_buf, data, length);
            uart_data_t.send_len = length;
            /* DMA 启动重试（对齐 uartSendNextPacket 的 3 次重试机制） */
            int dma_retry;
            for (dma_retry = 0; dma_retry < 3; dma_retry++) {
                if (uartStartDmaTx() == 0) {
                    break;
                }
                UART_DEBUG("UART DMA start busy (fast path), retry=%d\r\n", dma_retry + 1);
            }
            if (dma_retry >= 3) {
                UART_DEBUG("UART DMA start failed, req in queue\r\n");
                uart_data_t.tx_status = IDLE;
                return -1;
            }
            //发送成功后,记录发送时间戳（对齐 uartSendNextPacket） */
            uart_mc_meta = node->mc_meta;
            uart_rx_ctx.eth_seq_num = node->eth_seq_num;
            uart_rx_ctx.Sour_Sockid = node->Sour_Sockid;
            uart_rx_ctx.Dest_Sockid = node->Dest_Sockid;
        }
    } else {
        /* 队列中已有其他请求等待，当前的已在队尾，会在响应到达后自动发送 */
        UART_DEBUG("t:%ums,UART req queued, seq=%u, count=%u\r\n", 
                  synch_time_get(), (unsigned int)seq, (unsigned int)uart_data_t.req_queue.count);
    }
    return 0;
}

/*********************************************************************
 * @fn      uartTxGetLastSeq
 *
 * @brief   返回最近一次入队的串口请求序号
 *
 * @return  最近一次入队的序号（从未入队过时为 0）
 *
 * @note    入队(uart_req_queue_enqueue)是全项目串口请求的唯一入口，
 *          因此该值恒为"刚刚下发的那条串口命令"的序号。
 *          上层协议模块(如 Modbus 从站)在登记事务时记录它，
 *          响应到达时与 uart_rx_ctx.eth_seq_num 比对，即可拒绝
 *          迟到/重复的串口响应被算到当前事务上。
 */
uint32_t uartTxGetLastSeq(void)
{
    return uart_data_t.req_queue.last_enq_seq;
}

/*********************************************************************
 * @fn      uartProcessDeferredRx
 *
 * @brief   主循环中处理 ISR 延迟的帧解析+出队+下一包（一体化）
 *
 * @note    流程：
 *          ISR → 仅设置 rx_pending 标志
 *          主循环 → uartProcessDeferredRx
 *                → ① 从队首保存当前响应上下文到 uart_rx_ctx / uart_mc_meta
 *                   uartSendNextPacket(0) 跳过上下文覆写，无需 restore
 *                → ② Analysis_usart_handler (利用 PLC 处理时间做帧解析)
 *
 *          优势：
 *          - 下一包尽早发出，降低 PLC 等待延迟
 *          - 上下文一次写入，无 save/restore 冗余拷贝
 *          - 帧解析与 PLC 反应时间并行，提升整体吞吐
 *
 * @return  none
 */
/*********************************************************************
 * @fn      uartRxPartialWatchdog
 *
 * @brief   串口接收"半帧滞留"看门狗（主循环调用）
 *
 * @return  1 - 本次检测到滞留并请求复位；0 - 无需处理
 *
 * @note    ISR 采用"完整帧才投递"策略：半帧会保留在同一 DMA 缓冲中继续累积。
 *          若对端断线/丢字节导致缓冲内长期无法成帧，该通道将一直不产生
 *          rx_pending。本函数在滞留超过 UART_RX_PARTIAL_TIMEOUT_MS 时置位
 *          rx_part_reset，交由 ISR 安全复位（DMA 与缓冲归属由 ISR 独占，
 *          主循环只置标志，避免竞态），并通知在途 Modbus 事务立即回 0x0B，
 *          避免主站空等自身超时。
 */
uint8_t uartRxPartialWatchdog(void)
{
    if (!uart_data_t.rx_part_active) {
        return 0;
    }

    /* 尚未超过滞留门限：继续等待后续分片到齐 */
    if ((uint32_t)(g_ulSystemTick - uart_data_t.rx_part_tick) < UART_RX_PARTIAL_TIMEOUT_MS) {
        return 0;
    }

    /* 请求 ISR 丢弃半帧并强制切换接收缓冲 */
    uart_data_t.rx_part_reset  = 1;
    uart_data_t.rx_part_active = 0;

    UART_DEBUG("t:%ums, UART 半帧滞留超时(>%ums), 强制复位接收缓冲\r\n",
               synch_time_get(), (unsigned int)UART_RX_PARTIAL_TIMEOUT_MS);

    /* 该次串口异常若属于某笔在途 Modbus 事务，立即回 0x0B 结束它 */
    MB_Slave_NotifyFrameError();

    return 1;
}

void uartProcessDeferredRx(void)
{
    /* 无待处理数据则快速返回 */
    /* ① 半帧滞留兜底：必须放在 rx_pending 提前返回之前（滞留时本就没有 pending） */
    (void)uartRxPartialWatchdog();

    if (!uart_data_t.rx_pending) {
        return;
    }
    uart_data_t.rx_pending = 0;
    uart_data_t.rx_status = 0;

    /* ─── 步骤1: 出队 + 立即发送下一包（让 PLC 尽快收到下一帧）─── */
    uart_req_queue_node_t *node = uart_req_queue_front();
    if (node != NULL) {
        /* 从队首节点直接保存当前响应上下文（一次写入到位） */
        uart_rx_ctx.Sour_Sockid = node->Sour_Sockid;
        uart_rx_ctx.Dest_Sockid = node->Dest_Sockid;
        uart_rx_ctx.eth_seq_num = node->eth_seq_num;
        uart_mc_meta            = node->mc_meta;

        // UART_DEBUG("t:%ums,RxSuccess: seq=%u, Sour=%d, Dest=%d, queue_count=%u\r\n",
        //           synch_time_get(), (unsigned int)node->eth_seq_num,
        //           node->Sour_Sockid, node->Dest_Sockid,
        //           (unsigned int)uart_data_t.req_queue.count);

        uart_req_queue_dequeue();    // 更新队列头指针

        /* save_ctx=0: 跳过 uartSendNextPacket 内的上下文覆写，已在上方保存 */
        if (uartSendNextPacket(0) < 0) {
            uartResetToIdle(NULL);
        }
    } else {
        uartResetToIdle(NULL);
    }

    /* ─── 步骤2: 处理接收数据并转发到网口（利用 PLC 处理下一包的时间）─── */
    Analysis_usart_handler(uart_data_t.pending_rx_buf, uart_data_t.pending_rx_len);
}
 


/*********************************************************************
 * @fn      uartTxTimeoutCheck
 *
 * @brief   检查UART TX超时并实现重试机制
 *
 * @note    此函数需在主循环中周期性调用（建议1ms周期）
 *          功能包括：
 *          1. 接收成功或达到最大重试次数时，处理当前数据并发送下一个
 *          2. 未达到最大重试次数时，检测超时并触发重试
 *          3. 超过最大重试次数后丢弃数据
 *
 * @note    DMA传输完成中断(DMA1_Channel7_IRQHandler)负责释放发送缓冲区
 *          需要等待上位机响应才能继续发送下一个数据包
 *
 * @return  none
 *
 * @note    队列操作流程：
 *          1. 检查接收状态或重试次数
 *          2. 如果接收成功或达到最大重试次数：
 *             a. 从队列读取下一个数据包
 *             b. 如果队列中有数据：
 *                i. 提取socket ID
 *                ii. 初始化发送参数
 *                iii. 启动DMA传输
 *             c. 如果队列为空：
 *                i. 重置发送状态为空闲
 *                ii. 重置接收状态为空闲
 *          3. 如果未达到最大重试次数：
 *             a. 检查是否超时
 *             b. 如果超时：
 *                i. 增加重试计数
 *                ii. 更新发送时间戳
 *                iii. 重新启动DMA传输
 */
void uartTxTimeoutCheck(void)
{
    /* 只有发送忙状态才需要处理 */
    if (uart_data_t.tx_status == 0 ) {
        return;
    }
    /* 情况1：接收成功，处理当前数据并发送下一个 */
    if (uart_data_t.rx_status == 1)
    {   
        if (uartSendNextPacket(1) < 0) {
            /* 队列为空，发送完成 */
            uartResetToIdle("UART All Send Done");
        }
        return;
    }
    // 超时最小值 判断
    uint32_t timeout_ms = uart_data_t.timeout_ms < BAUD_SYNC_TIMEOUT_MS ? BAUD_SYNC_TIMEOUT_MS : uart_data_t.timeout_ms;
    /* 检查是否超时 */
    if ((g_ulSystemTick - uart_data_t.send_time) < timeout_ms) {
        return;  /* 未超时，直接返回 */
    }
    
    /* 超时处理 */
    if (uart_data_t.retry_count < uart_data_t.max_retry) {
        /* 重试次数未达上限，执行重试 */
        uart_data_t.retry_count++;
        uart_data_t.send_time = g_ulSystemTick;
        uart_data_t.rx_status  = 0;           /* 必须清零，避免上次残留响应导致误判成功 */

        UART_DEBUG("t:%ums\n,UART Retry=%d,Len=%d,Timeout=%ums--%ums\r\n",
                synch_time_get(), uart_data_t.retry_count,
                uart_data_t.send_len, timeout_ms ,
                uart_data_t.timeout_ms);

        /* 重试发送：若 DMA 忙则回退计数，等待下次超时检查再次尝试 */
        if (uartStartDmaTransfer(uart_data_t.tx_buf, uart_data_t.send_len) < 0) {
            uart_data_t.retry_count--;         /* DMA 忙未真正重发，不消耗重试配额 */
            UART_DEBUG("UART retry DMA busy, defer to next cycle\r\n");
        }
        #ifdef _TRANSMISSION_DEBUG
            TRANSMISSION_DEBUG("0x%02x ", uart_data_t.tx_buf[0]);
            for (uint16_t i = 1; i < uart_data_t.send_len - 1; i++) {
                if (i > uart_data_t.send_len - 1 - 3) {
                    TRANSMISSION_DEBUG(" 0x%02x %C%C ", uart_data_t.tx_buf[i], uart_data_t.tx_buf[i+1], uart_data_t.tx_buf[i+2]);
                    i += 3;
                } else {
                    TRANSMISSION_DEBUG("%C", uart_data_t.tx_buf[i]);
                }
            }
            TRANSMISSION_DEBUG("\r\n");
        #endif

    } else {
        /* 超过最大重试次数，丢弃当前数据包，尝试发送下一个 */
        UART_DEBUG("time_log = %u\n,UART Max Retry Reached, Drop Packet\r\n", synch_time_get());
 
        if( uart_rx_ctx.Sour_Sockid < 8 && uart_rx_ctx.Dest_Sockid < 8 && net_status.bit.link_b7 == 1 ) {
            //在响应监视定时器值以内未能接收响应
            ETH_S(uart_rx_ctx.Sour_Sockid).Error_Code = 2559;
            //报错 处理  = 0x60,   /* 60H: 以太网适配器和可编程控制器的通信时间  超过监视定时器值
            ethernet_error_code_ack(uart_rx_ctx.Sour_Sockid ,
                                    uart_rx_ctx.Dest_Sockid,
                                    net_mc_meta.sub_header,
                                    MC_END_TIMEOUT ); 
        }

        /* 更新累计发送长度（减去当前丢弃的数据包） */
        uart_data_t.last_size -= (uart_data_t.send_len + 2);
        /* 队首节点出队 */
        uart_req_queue_dequeue();
        if (uartSendNextPacket(1) < 0) {
            /* 队列为空，发送完成 */
            uartResetToIdle("UART All Send Done After Skip");
        }
    }
}
 
/**
 * @brief ASCII hex字符转4位数值（内部辅助宏）
 */
#define H2V(c) ((c) - (((c) >= 'A') ? 'A' - 10 : '0'))

/**
 * @brief 串口 接收任务回调函数
 * @note 在串口 接收数据 中调用，解析数据任务
 * @note 单次遍历: 去校验位 + 找STX(0x02)/ETX(0x03) + 累加校验和
 * 
 * @note 上下文来源：
 *       uart_rx_ctx + uart_mc_meta 在发送时由 uartTxWithSocketID 或
 *       uartSendNextPacket 设置，本函数直接读取，无需队列解引用。
 */
void Analysis_usart_handler(uint8_t *buf, uint32_t len)
{ 
    UART_DEBUG("t:%ums,UART rx:len= %d Sour_Sock=%d Dest_Sock=%d seq_num=%d \n", 
                        synch_time_get(), len, 
                        uart_rx_ctx.Sour_Sockid, 
                        uart_rx_ctx.Dest_Sockid,
                        uart_rx_ctx.eth_seq_num); 
    /* 短帧(<3字节): 无STX/ETX结构，直接透传 */
    if (len < 3) {
        for (int i = 0; i < (int)len; i++) {
            buf[i] &= 0x7F;    // 去除奇偶校验位
            UART_DEBUG("0x%02X ", buf[i]);
        }
        UART_DEBUG("\n");
        sim_Process_switch(buf, len);
        return;
    }

    /* 单次遍历: 去校验位 + 找STX/ETX（校验和验证已关闭，省略 calc_sum 累加） */
    int head_idx = -1;
    int tail_idx = -1;
    for (int i = 0; i < (int)len; i++) {
        buf[i] &= 0x7F;                     // 去除奇偶校验位
        if (buf[i] == 0x02) {               // STX: 新帧开始
            head_idx = i;
            tail_idx = -1;                  // 重置包尾，丢弃旧帧
        } else if (head_idx >= 0 && buf[i] == 0x03) {  // 帧内 ETX: 帧结束
            tail_idx = i;
            /* 去除校验和字节(ETX后2字节)的奇偶校验位，MELSOFT透传需要完整帧 */
            if (i + 1 < (int)len) buf[i + 1] &= 0x7F;
            if (i + 2 < (int)len) buf[i + 2] &= 0x7F;
            break;
        }
    }
    /* 验证帧完整性: 最小帧 STX(1)+数据(1)+ETX(1)+校验和(2)=5字节 */
    if (head_idx < 0 || tail_idx < 0) {
        UART_DEBUG("sim rx: 帧格式错误(head=%d,tail=%d)\r\n", head_idx, tail_idx);
        for (int i = 0; i < (int)len; i++) {
            UART_DEBUG("%02X ", buf[i]);
        }
        UART_DEBUG("\n");
        /* 通知在途 Modbus 事务立即回 0x0B，避免主站空等自身超时后才恢复 */
        MB_Slave_NotifyFrameError();
        return;
    }
    /* 帧尾完整性: ETX 之后必须有 2 字节 ASCII 校验和，
     * 防止"恰好在 ETX 与校验和之间被分片"时按有效帧交付 */
    if (tail_idx + 2 >= (int)len) {
        UART_DEBUG("sim rx: 帧尾校验和不完整(tail=%d,len=%d)\r\n", tail_idx, len);
        MB_Slave_NotifyFrameError();
        return;
    }
    /* 校验和验证: 2字节ASCII hex → 8位数值 */
    // {
    //     int sum_inxde = tail_idx + 1;
    //     uint8_t rx_sum = (H2V(buf[sum_inxde]) << 4) | H2V(buf[sum_inxde + 1]);
    //     if (calc_sum != rx_sum) {
    //         UART_DEBUG("sim rx:校验失败(calc=%02X,rx_sum=%02X,sum_inxde=%d)\r\n", calc_sum, rx_sum, sum_inxde);
    //         for (int i = 0; i < (int)len; i++) {
    //             UART_DEBUG("%02X ", buf[i]);
    //         }
    //         UART_DEBUG("\n");
    //         return;
    //     }
    // }
    /* 校验通过: 直接传递偏移指针，避免数据搬移 */
    {
        uint32_t valid_len = (uint32_t)(tail_idx + 3 - head_idx);  // 包头到校验和结束
        //UART_DEBUG("sim rx: 校验通过(len=%d,sum=%02X)\r\n", valid_len, calc_sum);
        #ifdef _TRANSMISSION_DEBUG
            TRANSMISSION_DEBUG("0x%02x ",buf[0]);
            for (uint16_t i = 1; i < len - 1; i++) {
                if (i > len - 1 - 3) {
                    TRANSMISSION_DEBUG(" 0x%02x %C%C ",buf[i], buf[i+1],buf[i+2]);
                    i += 3;
                } else {
                    TRANSMISSION_DEBUG("%C",buf[i]);
                }
            }
            TRANSMISSION_DEBUG("\r\n");
        #endif

        sim_Process_switch(buf + head_idx, valid_len);
    }
}
