/********************************** (C) COPYRIGHT *******************************
* File Name          : main.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2022/05/10
* Description        : Main program body.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
/*
 *@Note
ETH_UART example demonstrates data transparency between Ethernet and UART.
By default, 921600 baud rate (can be changed in bsp_uart.h) is used for serial port data transmission.
note:Due to pin multiplexing, this example only supports 10M networks.
For details on the selection of engineering chips,
please refer to the "CH32V30x Evaluation Board Manual" under the CH32V307EVT\EVT\PUB folder.
*/
#include "string.h"
#include "eth_driver.h"

#include "HTTPS.h"
#include "sntp.h"
#include "bsp_RTC.h"
#include "bsp_uart.h"
#include "bsp_flash.h"
#include "bsp_wch_net.h"
#include "ethernet_app.h"

/*********************************************************************
 * @fn      IWDG_Init
 *
 * @brief   Initializes IWDG.
 *
 * @param   IWDG_Prescaler - specifies the IWDG Prescaler value.
 *            IWDG_Prescaler_4 - IWDG prescaler set to 4.
 *            IWDG_Prescaler_8 - IWDG prescaler set to 8.
 *            IWDG_Prescaler_16 - IWDG prescaler set to 16.
 *            IWDG_Prescaler_32 - IWDG prescaler set to 32.
 *            IWDG_Prescaler_64 - IWDG prescaler set to 64.
 *            IWDG_Prescaler_128 - IWDG prescaler set to 128.
 *            IWDG_Prescaler_256 - IWDG prescaler set to 256.
 *          Reload - specifies the IWDG Reload value.
 *            This parameter must be a number between 0 and 0x0FFF.
 *
 * @return  none
 */
void IWDG_Feed_Init(u16 prer, u16 rlr)
{
    IWDG_WriteAccessCmd(IWDG_WriteAccess_Enable);
    IWDG_SetPrescaler(prer);
    IWDG_SetReload(rlr);
    IWDG_ReloadCounter();
    IWDG_Enable();
}


void Basic_Int(void)
{

    BSP_DEBUG("ETH_UART\r\n");	
    BSP_DEBUG("SystemClk:%d\r\n", SystemCoreClock);
    BSP_DEBUG("ChipID:%08x\r\n", DBGMCU_GetCHIPID());

    /* ★ 复位原因：定位"无故重启"必看(1=IWDG看门狗 2=上电 4=软件 8=外部引脚) */
    {
        u8 rst = 0;
        if (RCC_GetFlagStatus(RCC_FLAG_IWDGRST) != RESET) rst |= 0x01;
        if (RCC_GetFlagStatus(RCC_FLAG_PORRST)  != RESET) rst |= 0x02;
        if (RCC_GetFlagStatus(RCC_FLAG_SFTRST)  != RESET) rst |= 0x04;
        if (RCC_GetFlagStatus(RCC_FLAG_PINRST)  != RESET) rst |= 0x08;
        BSP_DEBUG("Reset cause: 0x%02X (1=IWDG 2=POR 4=SW 8=PIN)\r\n", rst);
        RCC_ClearFlag();
    }

    //Read configuration information
    BSP_FLASH_READ( BASIC_CFG_ADDR, (u8 *)&Basic_CfgBuf, BASIC_CFG_LEN );             
    BSP_FLASH_READ( PORT_CFG_ADDR, (u8 *)&Port_CfgBuf, PORT_CFG_LEN );
    BSP_FLASH_READ( LOGIN_CFG_ADDR, (u8 *)&Login_CfgBuf, LOGIN_CFG_LEN );

}

/*********************************************************************
 * @fn      main
 *
 * @brief   Main program
 *
 * @return  none
 */
int main(void)
{
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	SystemCoreClockUpdate();
	Delay_Init();
	USART_Printf_Init(2000000);            // USART initialize
    Basic_Int();
    BSP_Uart_Init(BAUD_RATE);              // USART2 initialize 
    TIM1_INT_Init(144,100);                //（100us）
    TIM2_Init();                           //（10ms）
             
    uartBaudRateSyncStateMachine();        // USART2 baud rate synchronization
    RTC_Init(); 

    IWDG_Feed_Init(IWDG_Prescaler_32, 4000); // 3.2s IWDG reset   
	while(1)
	{
        /** 串口发送任务主函数（带超时检测和重试功能）**/
        uartTxTimeoutCheck();
        /* 处理ISR延迟的帧解析+出队+下一包发送（原分三步: ISR解析→flag→主循环发下一包）*/
        uartProcessDeferredRx();
        /* 处理网口的数据 */
        ethernet_app_task();

    #if  SOCKET_SNTP_EN  == 1
        Sntp_client_task( net_status.bit.link_b7 );   // 获取sntp网络时间
    #endif 

    #if NET_LED_ENABLE == 1
        /* LED定时更新（需在主循环中周期调用，建议1ms周期）*/
        NEN_LED_Update();
    #endif

        IWDG_ReloadCounter();   //Feed dog
        
    }
}

/**
 *
┌──────────┐     ┌──────────┐     ┌─────┐     ┌──────────┐     ┌──────────┐
│ ETH 接收 │ ──→ │  入队    │ ──→ │ DMA │ ──→ │  PLC 处理 │ ──→ │ UART RX  │
│          │     │ + TX启动 │     │ TX  │     │          │     │ + 出队   │
└──────────┘     └──────────┘     └─────┘     └──────────┘     └──────────┘
   ~0ms             <1ms          传输时间     6~20ms           <1ms
                  ───────────────────────────────────────────
 * **/
