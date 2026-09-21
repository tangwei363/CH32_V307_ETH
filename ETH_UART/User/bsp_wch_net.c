/********************************** (C) COPYRIGHT *******************************
 * File Name          : bsp_wch_net.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/05/31
 * Description        : bsp_wch_net program body.
/*
 *@Note
TCP Server example, demonstrating that TCP Server
receives data and sends back after connecting to a client.
For details on the selection of engineering chips,
please refer to the "CH32V30x Evaluation Board Manual" under the CH32V307EVT\EVT\PUB folder.
 */
#include "string.h"
#include "eth_driver.h"
#include "bsp_wch_net.h"
#include "bsp_uart.h"
#include "bsp_flash.h"
#include "ethernet_app.h"
#include "fx_acclog.h"

#include "sntp.h"
#include "HTTPS.h"

u8 MACAddr[6];                                                           //MAC address
u8 SocketRecvBuf[WCHNET_NUM_UDP+WCHNET_NUM_IPRAW][RECE_BUF_LEN];         //socket receive buffer
u16 DESPORT, SRCPORT;                                                    //port

/* Per-Socket 控制信息: eth_socket[] 索引(原 socket_to_eth_sid) + 任务管线状态(原 socket_task) */
WCH_SocketCtrl_t socket_ctrl[WCHNET_MAX_SOCKET_NUM];

/*********************************************************************
 * @fn      socket_map_init
 *
 * @brief   初始化 socket_ctrl[].eth_sid 查找表，全部置为 -1
 *
 * @param   none
 *
 * @return  none
 */
void socket_map_init(void)
{
    for (int i = 0; i < WCHNET_MAX_SOCKET_NUM; i++)
        socket_ctrl[i].eth_sid = -1;
}

/*********************************************************************
 * @fn      mStopIfError
 *
 * @brief   check if error.
 *
 * @param   iError - error constants.
 *
 * @return  none
 */
void mStopIfError(u8 iError)
{
    if (iError == WCHNET_ERR_SUCCESS)
        return;
    log_err("Error: %02X\r\n", (u16) iError);
}

/*********************************************************************
 * @fn      TIM1_INT_Init
 *
 * @brief   Initializes TIM1 output compare.
 *
 * @param   arr - the period value.
 *          psc - the prescaler value.
 *
 * @return  none
 */
void TIM1_INT_Init( u16 arr, u16 psc)
{

    NVIC_InitTypeDef NVIC_InitStructure={0};
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure={0};

    RCC_APB2PeriphClockCmd(RCC_APB2Periph_TIM1, ENABLE );

    TIM_TimeBaseInitStructure.TIM_Period = arr;
    TIM_TimeBaseInitStructure.TIM_Prescaler = psc;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 50;
    TIM_TimeBaseInit( TIM1, &TIM_TimeBaseInitStructure);

    TIM_ClearITPendingBit( TIM1, TIM_IT_Update );

    NVIC_InitStructure.NVIC_IRQChannel =TIM1_UP_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority =1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority =0;
    NVIC_InitStructure.NVIC_IRQChannelCmd =ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    TIM_ITConfig(TIM1, TIM_IT_Update, ENABLE);

    TIM_Cmd( TIM1, ENABLE );
    
}

/*********************************************************************
 * @fn      TIM2_Init
 *
 * @brief   Initializes TIM2.
 *
 * @return  none
 */
void TIM2_Init(void)
{
    TIM_TimeBaseInitTypeDef TIM_TimeBaseStructure = { 0 };
    NVIC_InitTypeDef NVIC_InitStructure={0};
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);

    TIM_TimeBaseStructure.TIM_Period = SystemCoreClock / 1000000;
    TIM_TimeBaseStructure.TIM_Prescaler = WCHNETTIMERPERIOD * 1000 - 1;
    TIM_TimeBaseStructure.TIM_ClockDivision = 0;
    TIM_TimeBaseStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseStructure);


    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority =1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority =1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);
    
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    TIM_Cmd(TIM2, ENABLE);
    NVIC_EnableIRQ(TIM2_IRQn);
}

/*********************************************************************
 * @fn      WCHNET_CreateTcpSocketListen
 *
 * @brief   Create TCP Socket for Listening with specified socket ID and port
 *
 * @param   socket_id - Pointer to store the created socket ID
 * @param   listen_port - Port number to listen on
 *
 * @return  Socket creation result: WCHNET_ERR_SUCCESS for success, error code otherwise
 */
u8 WCHNET_CreateTcpSocketListen(u8 *socket_id, u16 listen_port)
{
    u8 ret;
    SOCK_INF TmpSocketInf;
    memset((void *) &TmpSocketInf, 0, sizeof(SOCK_INF));   // 清空Socket配置结构体
    TmpSocketInf.SourPort = listen_port;                   // 设置监听端口号
    TmpSocketInf.ProtoType = PROTO_TYPE_TCP;               // 设置协议类型为TCP

    ret = WCHNET_SocketCreat(socket_id, &TmpSocketInf);     // 创建Socket并返回ID
    WCH_DEBUG("Create TCP Listen Socket ID=%d, Port=%d\r\n", *socket_id, listen_port);
    mStopIfError(ret);                                     // 检查创建是否成功

    if (ret == WCHNET_ERR_SUCCESS) {
        ret = WCHNET_SocketListen(*socket_id);             // 开始监听连接请求
        mStopIfError(ret);                                 // 检查监听是否成功
        WCH_DEBUG(" Listen Socket ID=%d, \r\n", *socket_id  );
    }

    return ret;
}

/*********************************************************************
 * @fn      WCHNET_CreateCfgSocket
 *
 * @brief   According to the configuration information of the webpage,
 *          WCHNET establishes the corresponding socket.
 *
 *@param    mode - connection mode.
 *          Desip - destination IP
 *          Desport - destination port
 *          Srcport - source port
 * @return  none
 */
void WCHNET_CreateCfgSocket(u8 mode,u8 *socket_id, u8 *Desip, u16 Desport, u16 Srcport)
{
    u8 i;
    SOCK_INF TmpSocketInf;

    memset((void *) &TmpSocketInf, 0, sizeof(SOCK_INF));
    printf("desport: %d, srcport: %d\n", Desport, Srcport);
    printf("desip:%d.%d.%d.%d\n", Desip[0], Desip[1], Desip[2], Desip[3]);
    printf("mode %d\n", mode);
    switch (mode) {
        case MODE_TCPSERVER:        // 服务器端模式
            TmpSocketInf.ProtoType = PROTO_TYPE_TCP;
            TmpSocketInf.SourPort = Srcport;
            i = WCHNET_SocketCreat(socket_id, &TmpSocketInf);
            mStopIfError(i);
            i = WCHNET_SocketListen(*socket_id);
            mStopIfError(i);
            break;

        case MODE_TCPCLIENT:       // 客户端模式
            TmpSocketInf.ProtoType = PROTO_TYPE_TCP;
            memcpy((void *) &TmpSocketInf.IPAddr, Desip, 4);
            TmpSocketInf.SourPort = Srcport;
            TmpSocketInf.DesPort = Desport;
            TmpSocketInf.RecvBufLen = RECE_BUF_LEN;
            i = WCHNET_SocketCreat(socket_id, &TmpSocketInf);
            mStopIfError(i);
            i = WCHNET_SocketConnect(*socket_id);
            mStopIfError(i);
            break;

        default:
            break;
    }
}

/*********************************************************************
 * @fn      WCHNET_UdpServerRecv
 *
 * @brief   UDP Receive data function
 *
 *@param    socinf - socket information.
 *          ipaddr - The IP address from which the data was sent
 *          port - source port
 *          buf - pointer to the data buffer
 *          len - received data length
 * @return  none
 */
void WCHNET_UdpServerRecv(struct _SOCK_INF *socinf, u32 ipaddr, u16 port, u8 *buf, u32 len)
{
    uint8_t socketid = socinf->SockIndex;
    SOCK_INF *SocketInf_t = &SocketInf[socketid];
    /* IP 字节分解: 必须在 #ifdef 外部执行，供后续匹配和回调使用 */
    uint8_t ip_addr[4];
    ip_addr[0] = (u8) ipaddr;
    ip_addr[1] = (u8)(ipaddr >> 8);
    ip_addr[2] = (u8)(ipaddr >> 16);
    ip_addr[3] = (u8)(ipaddr >> 24);

#if NET_LED_ENABLE == 1
    NEN_RX_LED_Trigger();
#endif

#ifdef _BSP_WCH_DEBUG
    WCH_DEBUG("Udp Remote IP:%d.%d.%d.%d srcport=%d len=%d socketid=%d SourPort=%d\r\n",
              ip_addr[0], ip_addr[1], ip_addr[2], ip_addr[3],
              port, len, socketid, SocketInf_t->SourPort);
#endif

    if (eth_socket[5].local_port == SocketInf_t->SourPort)
    {
        if (port == 123) {
            WCH_DEBUG("sntp 网络校时 响应 \r\n");
            process_sntp_response(socketid, (u8 *)SocketInf_t->RecvReadPoint, len);
        } else {
            WCH_DEBUG("UDP 局域网搜索 PLC 设备 \r\n");
            process_Discover_device(5, socketid,
                                    (u8 *)SocketInf_t->RecvReadPoint,
                                    len,
                                    ip_addr, port);
        }
    }
    
    else
    {
        for (int S_id = 0; S_id < 8; S_id++)
        {
            // UDP 三重匹配: 本地端口 + 目标端口 + 目标IP
            ETH_SOCKET *socket_p = &ETH_S(S_id);   /* 缓存指针，减少重复索引 */
            if (socket_p->local_port == SocketInf_t->SourPort &&
                socket_p->destport == port &&
                memcmp(socket_p->destip, ip_addr, 4) == 0)
            {
                Analysis_eth_frame_handler(S_id, socketid,
                                           (u8 *)SocketInf_t->RecvReadPoint, len,
                                           ip_addr, port);
                socket_p->net_rx_packets += len;  /* net udp 接收包数 */

                //ethernet_connect_set(S_id,1);      // UDP 连接成功
            #if SOCKET_HTTP_EN        
                FX_ACCLOG_AddRecord(S_id, ETH_TYPE_UDP, 
                                    ETH_S(S_id).Pro_Type,
                                    SocketInf_t->IPAddr);
                WCHNET_UpdateAccLog();             //更新访问记录到PLC的寄存器中 
            #endif         
                break;
            }
        }
    }
}

/*********************************************************************
 * @fn      WCHNET_CreateUdpSocket
 *
 * @brief   Create UDP Socket with specified socket ID and port
 *
 * @param   socket_id - Pointer to store the created socket ID
 * @param   udp_port - UDP port number to bind
 *
 * @return  Socket creation result: WCHNET_ERR_SUCCESS for success, error code otherwise
 */
u8 WCHNET_CreateUdpSocket(u8 *socket_id, u16 udp_port)
{
    u8 ret;
    SOCK_INF TmpSocketInf;

    memset((void *) &TmpSocketInf, 0, sizeof(SOCK_INF));   // 清空Socket配置结构体
    TmpSocketInf.SourPort = udp_port;                      // 设置UDP端口号
    TmpSocketInf.ProtoType = PROTO_TYPE_UDP;               // 设置协议类型为UDP
    TmpSocketInf.RecvBufLen = RECE_BUF_LEN;                // 设置接收缓冲区长度
    TmpSocketInf.RecvStartPoint = (u32) SocketRecvBuf;     // 设置接收缓冲区起始地址
    TmpSocketInf.AppCallBack = WCHNET_UdpServerRecv;       // 设置UDP接收回调函数

    ret = WCHNET_SocketCreat(socket_id, &TmpSocketInf);    // 创建Socket并返回ID
    WCH_DEBUG("Create UDP Socket ID=%d, Port=%d\r\n", *socket_id, udp_port);
    mStopIfError(ret);                                     // 检查创建是否成功

    return ret;
}

/*********************************************************************
 * @fn      WCHNET_CreateIPRawSocket
 *
 * @brief   Create raw IP Socket
 *
 * @return  none
 */
void WCHNET_CreateIPRawSocket(u8 *socket_id, u8 * DESIP,u16 IPRawProto)
{
    u8 i;
    SOCK_INF TmpSocketInf;

    memset((void *)&TmpSocketInf,0,sizeof(SOCK_INF));
    memcpy((void *)TmpSocketInf.IPAddr,DESIP,4);
    TmpSocketInf.SourPort = IPRawProto;              //In IPRAW mode, SourPort is the protocol type
    TmpSocketInf.ProtoType = PROTO_TYPE_IP_RAW;
    TmpSocketInf.RecvStartPoint = (u32)SocketRecvBuf;
    TmpSocketInf.RecvBufLen = RECE_BUF_LEN ;
    i = WCHNET_SocketCreat(socket_id,&TmpSocketInf);
    WCH_DEBUG("Create IPRaw Socket ID=%d, Port=%d\r\n", *socket_id, IPRawProto);
    mStopIfError(i);
}

/*********************************************************************
 * @fn      WCHNET_ETHRx
 *
 * @brief   以太网接收数据处理函数，解析收到的数据包并分发到对应的应用层处理函数。
 *
 * @param   socketid - WCHNET协议栈的socket ID
 *
 * @return  none
 *
 * @note    TCP服务器连接只需匹配端口号即可：
 *          1. TCP是有连接协议，accept返回的socket已经与特定客户端绑定
 *          2. 每个accept socket只对应一个TCP连接，无需额外IP匹配
 *          3. UDP协议需要同时匹配端口和IP地址，因为UDP是无连接的
 */
void WCHNET_ETHRx(u8 socketid)
{
    u32 receive_len;
    SOCK_INF *SocketInf_t = &SocketInf[socketid];
#if NET_LED_ENABLE == 1
    NEN_RX_LED_Trigger();  // 触发接收LED闪烁
#endif

    // 计算接收缓冲区的结束地址
    u32 endAddr = SocketInf_t->RecvStartPoint + SocketInf_t->RecvBufLen;

    // 计算本次可读取的数据长度（处理环形缓冲区回绕情况）
    if ((SocketInf_t->RecvReadPoint + SocketInf_t->RecvRemLen) > endAddr) {
        receive_len = endAddr - SocketInf_t->RecvReadPoint;  // 缓冲区末尾部分
    }
    else {
        receive_len = SocketInf_t->RecvRemLen;  // 未回绕，直接取剩余长度
    }
    uint8_t * RecvReadPoint = (u8 *)SocketInf_t->RecvReadPoint;   
    // #ifdef _BSP_WCH_DEBUG
    // // 打印接收信息：socket ID、源端口、源IP、目标端口
    // //WCH_DEBUG("\n********************开始***********************\n");
    // WCH_DEBUG("t:%ums ETHRx:   D_id=%d ,local=%d, dest=%d. len=%d \r\n", 
    //             synch_time_get(), 
    //             socketid, 
    //             SocketInf_t->SourPort, 
    //             SocketInf_t->DesPort, 
    //             receive_len);
    // #endif

    // 网口数据包解析：通过 CONNECT 时缓存的映射表 O(1) 查找 eth_socket 索引
    // 避免每次接收都遍历 eth_socket[] 数组（最多 8 次端口号比较）
    int S_id = socket_ctrl[socketid].eth_sid;
    if (S_id >= 0)
    {
        // 调用通用的以太网帧解析处理函数
        Analysis_eth_frame_handler(S_id, 
                                socketid,
                                RecvReadPoint,
                                receive_len,
                                SocketInf_t->IPAddr,
                                SocketInf_t->DesPort);

        ETH_S(S_id).net_rx_packets += receive_len;        /* net tcp 接收包数 */
    }

    /***   回环测试    ***/
    //WCHNET_SocketSend(socketid, (u8 *) SocketInf_t->RecvReadPoint,&receive_len);
    // 清空接收缓冲区，通知协议栈已处理完数据
    // 传入NULL表示从缓冲区读取并丢弃数据，receive_len传入已读取的长度
    WCHNET_SocketRecv(socketid, NULL, &receive_len);
}

/*********************************************************************
 * @fn      WCHNET_HandleSockInt
 *
 * @brief   Socket 中断处理函数
 *          - 处理接收、连接成功、断开连接、超时断开四种中断事件
 *          - 通过匹配本地端口号，将底层 socket 事件映射到应用层 eth_socket[]
 *          - 注意：四个中断标志用独立 if 判断（非 if/else if），在硬件
 *            异常同时置位多个标志时，各处理器可能先后执行
 *
 * @param   socketid - 底层 socket 索引（0 ~ WCHNET_MAX_SOCKET_NUM-1）
 *          intstat  - 中断状态标志位（SINT_STAT_RECV/CONNECT/DISCONNECT/TIM_OUT）
 *
 * @return  none
 */
void WCHNET_HandleSockInt(u8 socketid, u8 intstat)
{
    SOCK_INF *SocketInf_t = &SocketInf[socketid];

    /* ================================================================
     * 1. 接收数据中断
     * ================================================================ */
    if (intstat & SINT_STAT_RECV)
    {
        WCHNET_ETHRx(socketid);                                  /* 将数据交给协议栈上层处理 */
    }

    /* ================================================================
     * 2. TCP 连接成功中断
     *    遍历 eth_socket[]，根据本地端口匹配找到对应的应用层 socket，
     *    标记连接状态 + 记录访问日志 + 同步日志到 PLC
     * ================================================================ */
    else if (intstat & SINT_STAT_CONNECT)
    {
 
    #if KEEPALIVE_ENABLE
        WCHNET_SocketSetKeepLive(socketid, ENABLE);              /* 启用 TCP KeepAlive */
    #endif
    
        WCHNET_ModifyRecvBuf(socketid, (u32)SocketRecvBuf[socketid], RECE_BUF_LEN);

        int S_id;
        for (S_id = 0; S_id < 8; S_id++)
        {
            /* 跳过未启用的 eth_socket 槽位，防止脏数据误匹配 */
            if (!ETH_S(S_id).EN) continue;

            if (ETH_S(S_id).local_port == SocketInf_t->SourPort)
            {

            #ifdef _BSP_WCH_DEBUG    
                WCH_DEBUG("\r\n S_id=%d, D_id=%d, Port:%d \r\n",
                            S_id, socketid, SocketInf_t->SourPort);
                WCH_DEBUG("IP %d.%d.%d.%d:%d ",
                        SocketInf_t->IPAddr[0], SocketInf_t->IPAddr[1],
                        SocketInf_t->IPAddr[2], SocketInf_t->IPAddr[3],
                        SocketInf_t->DesPort);
                WCH_DEBUG("TCP Connect Success ... \r\n");
            #endif   
            #if  SOCKET_PHY_LINK_EN       
                ethernet_connect_set(S_id,&SocketInf_t->IPAddr[0],SocketInf_t->DesPort, 1); /* 标记：连接已建立 */
            #endif    
                socket_ctrl[socketid].eth_sid = S_id;              /* 缓存映射：后续 RECV 时 O(1) 查找 */
            #if SOCKET_HTTP_EN  
                FX_ACCLOG_AddRecord(S_id, ETH_TYPE_TCP,
                                    ETH_S(S_id).Pro_Type,
                                    SocketInf_t->IPAddr);         /* 记录访问日志 */
                WCHNET_UpdateAccLog();                            /* 同步日志到 PLC */
            #endif         
                break;
            }
        }

    }

    /* ================================================================
     * 3. TCP 断开连接中断
     *    匹配 eth_socket[] 中对应端口的条目并清除连接状态。
     *    排除 Pro_Type == 0xA7（UDP MC 协议），因为 UDP 无连接概念，
     *    不需要维护连接状态。
     * ================================================================ */
    else if (intstat & SINT_STAT_DISCONNECT)
    {
 
        int S_id;
        for (S_id = 0; S_id < 8; S_id++)
        {
            /* 跳过未启用的槽位 */
            if (!ETH_S(S_id).EN) continue;

            if (ETH_S(S_id).local_port == SocketInf_t->SourPort &&
                ETH_S(S_id).Pro_Type != 0xA7)                /* 排除 UDP MC 协议 */
            {
            #if SOCKET_PHY_LINK_EN    
                ethernet_connect_set(S_id,&SocketInf_t->IPAddr[0],SocketInf_t->DesPort, 0);                    /* 标记：连接已断开 */
            #endif    
                socket_ctrl[socketid].eth_sid = -1;               /* 清除缓存映射 */
                break;
            }
        }
    #ifdef _BSP_WCH_DEBUG 
        WCH_DEBUG("\r\n S_id=%d, D_id=%d, Port:%d \r\n",
                S_id, socketid, SocketInf_t->SourPort);
        WCH_DEBUG("IP %d.%d.%d.%d:%d ",
                SocketInf_t->IPAddr[0], SocketInf_t->IPAddr[1],
                SocketInf_t->IPAddr[2], SocketInf_t->IPAddr[3],
                SocketInf_t->DesPort);
        WCH_DEBUG("TCP Disconnect !!! \r\n");
    #endif    
    }

    /* ================================================================
     * 4. TCP 超时断开中断
     *    仅处理 Pro_Type == 0xA8（HTTP 数据监控）的超时 ——
     *    网页链接超时后主动关闭底层 socket，释放 TCP 资源。
     *    其他协议类型（0xA0 melsoft / 0xA6 TCP MC）的超时由上层
     *    定时器或应用逻辑自行处理，此处不干预。
     * ================================================================ */
    else if (intstat & SINT_STAT_TIM_OUT)
    {
    #ifdef _BSP_WCH_DEBUG
        WCH_DEBUG("\r\n D_id=%d, Port:%d \r\n", socketid, SocketInf_t->SourPort);
        WCH_DEBUG("IP %d.%d.%d.%d:%d ",
                  SocketInf_t->IPAddr[0], SocketInf_t->IPAddr[1],
                  SocketInf_t->IPAddr[2], SocketInf_t->IPAddr[3],
                  SocketInf_t->DesPort);
        WCH_DEBUG("TCP Timeout disconnect !!! \r\n");
    #endif        
        int S_id;
        for (S_id = 0; S_id < 8; S_id++)
        {
            /* 跳过未启用的槽位 */
            if (!ETH_S(S_id).EN) continue;

            if (ETH_S(S_id).local_port == SocketInf_t->SourPort &&
                ETH_S(S_id).Pro_Type == 0xA8)                /* 仅处理 HTTP 数据监控连接 */
            {
                WCHNET_SocketClose(socketid, TCP_CLOSE_NORMAL);   /* 关闭底层 socket */
                WCH_DEBUG("HTTP socket timeout, closed directly\r\n");

            #if SOCKET_PHY_LINK_EN    
                ethernet_connect_set(S_id,&SocketInf_t->IPAddr[0],SocketInf_t->DesPort, 0);  /* 标记：连接已断开 */
            #endif  

                socket_ctrl[socketid].eth_sid = -1;               /* 清除缓存映射 */
                break;
            }
        }
    }
}

/*********************************************************************
 * @fn      WCHNET_HandleGlobalInt
 *
 * @brief   Global Interrupt Handle
 *
 * @return  none
 *
 * @note    流控机制说明：
 *          - 所有socket的RECV事件都正常处理
 *          - 流控由uart_req_queue队列实现（队列满时拒绝新请求）
 *          - 不再使用per-socket的WAITING状态跳过RECV
 *          - 这样可以避免数据滞留和客户端断线问题
 */
void WCHNET_HandleGlobalInt(void)
{
    u8 intstat;
    static u16 i;

    intstat = WCHNET_GetGlobalInt();                              // get global interrupt flag
    /* 去抖: 网线抖动/EMI 可能导致 PHY 状态短时间反复切换,* 间隔 PHY_DEBOUNCE_MS 才真正读取状态, 避免频繁响应。*/
    static uint32_t last_phy_check_tick = 0;
    if ( g_ulSystemTick - last_phy_check_tick >= PHY_DEBOUNCE_MS) 
    {
        last_phy_check_tick = g_ulSystemTick;
        if (intstat & GINT_STAT_PHY_CHANGE)                           //PHY status change
        {
            uint8_t phy_status = WCHNET_GetPHYStatus();
            if (phy_status & PHY_Linked_Status)
            {
                WCH_DEBUG("PHY Link Success!!!\r\n");
                net_status.bit.link_b7 = 1;
                net_status.bit.com_err_b4 = 0;          // 0:通信正常显示
 
            }else{
                WCH_DEBUG("PHY Link disconnect!!!\r\n");
                net_status.bit.link_b7 = 0;
                Wizchip_PHY_Link_Disconnect();          // 断开PHY连接 处理
            #if NET_LED_ENABLE == 1
                NEN_RX_LED_SetState(0);  // 熄灭接收LED
                NEN_TX_LED_SetState(0);  // 熄灭发送LED
            #endif
            }
            #if SOCKET_PHY_LINK_EN
                wizchip_PHY_Link_PLC( net_status.bit.link_b7 );
            #endif
        }
    }
        
    if (intstat & GINT_STAT_SOCKET) {                // socket 中断
        u8 socketint;
        for (i = 0; i < WCHNET_MAX_SOCKET_NUM; i++) {
            socketint = WCHNET_GetSocketInt(i);     // 获取套接字中断状态
            if (socketint){
                WCHNET_HandleSockInt(i, socketint);
            }
        }
    }
}


/*

┌──────────┐   ①命令报文    ┌─────────────┐
│ 对方设备  │ ─────────────→│             │
│ (上位机)  │ ←─────────────│  以太网模块  │ ← ACK(*1)
│          │               │ (CH32V307)  │
│          │               └──────┬──────┘
│          │                      │ ②读/写指令(UART)
│          │                      ↓
│          │              ┌──────────────────┐
│          │              │   可编程控制器     │
│          │              │  0步→END→END处理  │← 扫描周期,每次只处理1条
│          │              └────────┬─────────┘
│          │                       │ ③响应(结果)
│          │                       ↓
│          │               ┌──────┴──────┐
│          │ ← 响应+ACK(*1)│  以太网模块  │
└──────────┘               └─────────────┘

*/

