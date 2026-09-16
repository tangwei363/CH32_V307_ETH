/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32v30x_it.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2022/01/18
* Description        : Main Interrupt Service Routines.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "eth_driver.h"
#include "ch32v30x_it.h"
#include "bsp_uart.h"
#include "ethernet_app.h"
#include "HTTPS.h"

void RTC_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void NMI_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void HardFault_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void ETH_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM1_UP_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void TIM2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void EXTI9_5_IRQHandler(void) __attribute__((interrupt()));
void DMA1_Channel7_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void SysTick_Handler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
void USART2_IRQHandler(void) __attribute__((interrupt("WCH-Interrupt-fast")));
/*********************************************************************
 * @fn      NMI_Handler
 *
 * @brief   This function handles NMI exception.
 *
 * @return  none
 */
void NMI_Handler(void)
{
  while (1)
  {
  }
}

/*********************************************************************
 * @fn      HardFault_Handler
 *
 * @brief   This function handles Hard Fault exception.
 *
 * @return  none
 */
void HardFault_Handler(void)
{
    BSP_DEBUG("HardFault_Handler\r\n");

    BSP_DEBUG("mepc  :%08x\r\n", __get_MEPC());
    BSP_DEBUG("mcause:%08x\r\n", __get_MCAUSE());
    BSP_DEBUG("mtval :%08x\r\n", __get_MTVAL());
    NVIC_SystemReset();
    while(1);
}

/*********************************************************************
 * @fn      EXTI9_5_IRQHandler
 *
 * @brief   This function handles GPIO exception.
 *
 * @return  none
 */
void EXTI9_5_IRQHandler(void)
{
    ETH_PHYLink( );
    EXTI_ClearITPendingBit(EXTI_Line7);     /* Clear Flag */
}

/*********************************************************************
 * @fn      ETH_IRQHandler
 *
 * @brief   This function handles ETH exception.
 *
 * @return  none
 */
void ETH_IRQHandler(void)
{
    WCHNET_ETHIsr();
}

/*********************************************************************
 * @fn      TIM1_UP_IRQHandler
 *
 * @brief   This function handles TIM1 UP exception.
 *
 *
 * @return  none
 */

void TIM1_UP_IRQHandler(void)
{
    if(TIM_GetITStatus(TIM1, TIM_IT_Update)==SET)
    {
        synch_time_handler(); //100us
        WCHNET_TimeIsr(WCHNETTIMERPERIOD);
    }
    TIM_ClearITPendingBit(TIM1, TIM_IT_Update );
}

/*********************************************************************
 * @fn      TIM2_IRQHandler
 *
 * @brief   This function handles TIM2 exception.
 *
 * @return  none
 */
void TIM2_IRQHandler(void)
{
    
 
    Tick_time_handler();
#if SOCKET_HTTP_EN
    Html_time_handler();
#endif
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
}

/*********************************************************************
 * @fn      DMA1_Channel7_IRQHandler
 *
 * @brief   uart2 DMA Tx completion interrupt.
 * @note    DMA传输完成后释放发送缓冲区，推进读指针
 *
 * @return  none
 */
void DMA1_Channel7_IRQHandler(void)
{
    if(DMA_GetITStatus(DMA1_IT_TC7))
    {
        DMA_Cmd(DMA1_Channel7,DISABLE);
        DMA_ClearITPendingBit(DMA1_IT_TC7);
        uart_data_t.dma_tx_busy = 0;             /* 清除软件忙标志(已收归结构体) */
    }
}


/*********************************************************************
 * @fn      USART2_IRQHandler
 *
 * @brief   This function handles USART2 global interrupt request.
 * @note    ① 完整性判定（修复"半帧被丢弃"）：
 *             IDLE 只代表"总线静默了 1 个字符时间"，并不代表"一帧发完"。
 *             对端在长帧中间停顿（仿真器发送任务被抢占、RS-485 换向等）会把
 *             一个 404 字节应答切成 322+82 两片；若逐片投递，解析层因片内
 *             找不到 ETX 而整片丢弃，表现为"偶发丢帧 + 随后 1 秒连续异常"。
 *             故本 ISR 只在缓冲内构成完整帧时才切缓冲并投递，否则保持
 *             MADDR/CNTR 不变、重新使能 DMA，继续向同一缓冲追加。
 *          ② 双缓冲机制：仅在完整帧(或溢出/强制复位)时切换缓冲区，
 *             把切换次数由"每个 IDLE"降到"每帧一次"，进一步缩小 DMA 关闭窗口。
 *          ③ 溢出兜底：缓冲写满仍不成帧 → 丢弃整块并切缓冲。
 *          ④ 滞留兜底：主循环 uartRxPartialWatchdog() 置 rx_part_reset 时强制复位。
 * 流程(完整帧): IDLE → 关DMA → 读长度 → 完整性判定 → 切buf → 写MADDR → 重置CNTR → 开DMA
 * 流程(半帧)  : IDLE → 关DMA → 读长度 → 未成帧 → 保持 MADDR/CNTR → 开DMA(继续累积)
                                                        ↑ ~6个寄存器操作 << 1μs
       DMA恢复后 → 安全处理已完成缓冲区的数据
 * @return  none
 */
/*********************************************************************
 * @fn      uartIsFrameComplete
 *
 * @brief   判定接收缓冲内的数据是否已构成一个完整应答帧
 *
 * @param   buf - 接收缓冲首地址（必须是帧的起点）
 * @param   len - 当前已接收字节数
 *
 * @return  1 - 已构成完整帧，可以切缓冲并投递；0 - 仍为半帧，需继续接收
 *
 * @note    判据（均为 O(1)，可安全用于中断）：
 *          1) 单字节应答：ACK(0x06) / NAK(0x15) / ENQ(0x05)
 *             —— FX 写命令应答与波特率同步阶段使用；
 *          2) ASCII 长帧：必须以 STX(0x02) 起始，且 ETX(0x03) 恰好位于
 *             倒数第 3 字节（其后 2 字节为 ASCII 校验和）。
 *          由于 ASCII 正文只可能是 '0'~'9'/'A'~'F'(≥0x30)，不会出现 0x03，
 *          故"倒数第 3 字节为 ETX"在半帧上不可能误判成立。
 *          注意：串口使能了偶校验，接收字节 MSB 为校验位，故统一 & 0x7F。
 */
static uint8_t uartIsFrameComplete(const uint8_t *buf, uint16_t len)
{
    if (len == 0u) {
        return 0u;
    }

    /* ① 单字节应答：ACK / NAK / ENQ */
    if (len == 1u) {
        uint8_t b = (uint8_t)(buf[0] & 0x7Fu);
        return (uint8_t)((b == 0x06u) || (b == 0x15u) || (b == 0x05u));
    }

    /* ② ASCII 长帧：STX 起始 + ETX 位于倒数第 3 字节（其后 2 位 ASCII 校验和） */
    if (len >= 4u &&
        (uint8_t)(buf[0] & 0x7Fu) == 0x02u &&
        (uint8_t)(buf[len - 3u] & 0x7Fu) == 0x03u) {
        return 1u;
    }

    return 0u;
}

void USART2_IRQHandler(void)
{
    if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
    {
        uint16_t rx_len;
        uint8_t *proc_buf;
        uint8_t  switch_buf = 0;    /* 1 = 本片可交付：切缓冲并投递 */

        /* Clear IDLE flag by reading data register */
        USART_ReceiveData(USART2);

        /* ① 读取当前缓存指针（DMA 仍运行中，安全：仅本 ISR 会修改 rx_buf） */
        proc_buf = uart_data_t.rx_buf;

        /* ② 最小临界区：停 DMA → 读长度 → 完整性判定 */
        DMA_Cmd(DMA1_Channel6, DISABLE);
        rx_len = (uint16_t)(UART_RX_DMA_SIZE - DMA1_Channel6->CNTR);

        if (uart_data_t.rx_part_reset) {
            /* ④ 主循环判定半帧滞留超时：丢弃整块并强制复位，不投递 */
            uart_data_t.rx_part_reset  = 0;
            uart_data_t.rx_part_active = 0;
            rx_len     = 0;
            switch_buf = 1;
        } else if (rx_len >= UART_RX_DMA_SIZE) {
            /* ③ 缓冲写满仍不成帧：溢出丢弃，避免半帧永久占用接收通道 */
            uart_data_t.rx_part_active = 0;
            rx_len     = 0;
            switch_buf = 1;
        } else if (rx_len > 0u && uartIsFrameComplete(proc_buf, rx_len)) {
            /* ① 完整帧：切缓冲并交付主循环 */
            uart_data_t.rx_part_active = 0;
            switch_buf = 1;
        } else if (rx_len > 0u) {
            /* ① 半帧：不切缓冲、不清 CNTR，DMA 继续向同一缓冲追加 */
            uart_data_t.rx_part_active = 1;
            uart_data_t.rx_part_tick   = g_ulSystemTick;
        }
        /* rx_len == 0 且无需复位：空片，仅重新使能 DMA */

        if (switch_buf) {
            /* 双缓冲切换: 使用显式索引分支替代自引用表达式，
             * 修复原有 `rx_buf = idx ? alt : rx_buf` 第二次翻转后两指针指向同一内存的 bug */
            uart_data_t.rx_buf_idx ^= 1;
            if (uart_data_t.rx_buf_idx) {
                uart_data_t.rx_buf = uart_data_t.rx_buf_alt;
            } else {
                uart_data_t.rx_buf = UART2_RX_DMA_DataBuf;
            }

            DMA1_Channel6->MADDR = (uint32_t)uart_data_t.rx_buf;
            DMA_SetCurrDataCounter(DMA1_Channel6, UART_RX_DMA_SIZE);
        }

        DMA_Cmd(DMA1_Channel6, ENABLE);
        /* ← DMA 已恢复接收，临界区结束 */

        /* ③ 延迟处理: 置 rx_pending 标志，主循环 uartProcessDeferredRx 消费；
         *    波特率同步阶段由 uartBaudRateSyncSendAndWait 直接轮询消费 */
        if (switch_buf && rx_len > 0u)
        {
            uart_data_t.pending_rx_buf = proc_buf;
            uart_data_t.pending_rx_len = rx_len;
            uart_data_t.rx_pending = 1;
            //UART_DEBUG("\nt:%ums,UART IRQ RX:len= %d \n", synch_time_get(), rx_len); 
        }
    }
}

/*********************************************************************
 * @fn      RTC_IRQHandler
 *
 * @brief   This function handles RTC Handler.
 *
 * @return  none
 */
void RTC_IRQHandler(void)
{
    if(RTC_GetITStatus(RTC_IT_SEC) != RESET) /* Seconds interrupt */
    {
        RTC_Get();
    }
    if(RTC_GetITStatus(RTC_IT_ALR) != RESET) /* Alarm clock interrupt */
    {
        RTC_ClearITPendingBit(RTC_IT_ALR);
        RTC_Get();
    }

    RTC_ClearITPendingBit(RTC_IT_SEC | RTC_IT_OW);
    RTC_WaitForLastTask();
}

 
