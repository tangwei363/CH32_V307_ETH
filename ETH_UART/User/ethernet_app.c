#include "ethernet_app.h"

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <ctype.h>

#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"

#include "mb_slave.h"           /* Modbus TCP 从站 → 三菱 MELSEC-FX 串口 网关 */

#include "eth_driver.h"
#include "bsp_wch_net.h"
#include "bsp_uart.h"
#include "bsp_flash.h"
#include "HTTPS.h"
#include "sntp.h"

#include "index.h"
#include "fx_acclog.h"
#include "fx_devmon.h"
#include "fx_acclog.h"
#include "fx_enetinf.h"
#include "fx_plcinf.h"
#include "fx_status.h"
 
//#include "PING.h"

volatile uint32_t synch_state_time = 0;
extern _calendar_obj calendar;

net_monitor_state_t net_monitor_state = FX_MONITOR_IDLE; // 监视状态

SNTP_time    sntp_time = {0};        // 时间设置
Log_data     log_data = {0};         // 日志记录数据

ETH_status   net_status = {0};       // 网络使能状态
ETH_connect  net_connect = {0};      // 网络连接状态
ETH_SOCKET   eth_socket[8] = {0};    // 网络 SOCKET 数组
 
uint8_t      eth_ch  = 0;            // 网络通道号
static uint8_t eth_socket_init_flg = 0; //获取PLC网络配置信息标志   

const uint8_t eth_Local_IP[4] = {192, 168, 1, 150};         // 默认设备IP地址
const uint8_t eth_subnet_mask[4] = {255, 255, 255, 0};      // 默认设备子网掩码
const uint8_t eth_gateway[4] = {192, 168, 1, 1};            // 默认设备网关

// const uint8_t sntp_server_name[] = "ntp.ntsc.ac.cn";
// const uint8_t sntp_server_ip[4] = {202,120,2,101};    //中国国家授时中心 NTP 服务器
const uint8_t sntp_server_ip[4] = {120, 25, 115, 20};    //阿里云 NTP 服务器：
// const uint8_t sntp_server_ip[4] = {119,28,117,206};   //腾讯云 NTP 服务器
// const uint8_t sntp_server_ip[4] = {120,196,166,232};  //华为云 NTP 服务器
//const uint8_t sntp_server_ip[4] = {192, 168, 1, 223};  
 
const uint8_t MC_cmd_header [10] = {
    MC_CMD_BIT_BATCH_READ,      // 位单位的成批读出
    MC_CMD_WORD_BATCH_READ,     // 字单位的成批读出
    MC_CMD_BIT_BATCH_WRITE,     // 位单位的成批写入
    MC_CMD_WORD_BATCH_WRITE,    // 字单位的成批写入
    MC_CMD_BIT_RANDOM_WRITE,    // 位单位的随机写入
    MC_CMD_WORD_RANDOM_WRITE,   // 字单位的随机写入
    MC_CMD_REMOTE_RUN,          // 针对可编程控制器请求远程RUN
    MC_CMD_REMOTE_STOP,         // 针对可编程控制器请求远程STOP
    MC_CMD_PLC_MODEL,           // PLC可编程控制器的型号名称
    MC_CMD_ECHO_TEST            // 从其他节点接收的字符直接返回到其他节点
};



 
/*********************************************************************
 * @fn      FX_ENETINF_SetMonitorState
 *
 * @brief   设置监视状态
 *
 * @param   state - 监视状态
 *
 * @return  无
 */
void FX_ENETINF_SetMonitorState(net_monitor_state_t state)
{
    uint8_t i;

    net_monitor_state = state;

    /* 监视开始时清除各套接字的错误码 → 通信状态页 / FX3U-ENET-ADP信息页的 ERR. LED 复位。
     * ERR. LED 是锁存型(与真实模块一致)：出现过一次通信错误后会一直亮，
     * 点一次“监视开始”即可消除。 */
    if (state == FX_MONITOR_RUNNING) {
        for (i = 0; i < ETH_MAX_CONNECTIONS; i++) {
            eth_socket[i].Error_Code = 0;
        }
    }
    printf("监视状态设置为: %d\r\n", state);
}

/*********************************************************************
 * @fn      FX_ENETINF_GetMonitorState
 *
 * @brief   获取监视状态
 *
 * @param   无
 *
 * @return  监视状态
 */
net_monitor_state_t FX_ENETINF_GetMonitorState(void)
{
    return net_monitor_state;
}

/*********************************************************************
 * @fn      FX_ENETINF_UpdateMonitor
 *
 * @brief   更新监视数据
 *
 * @param   无
 *
 * @return  无
 */
void FX_ENETINF_UpdateMonitor(void)
{
    if (net_monitor_state != FX_MONITOR_RUNNING) {
        return;
    }
}
/**
 * @brief 查找MC命令码是否在MC_cmd_header数组中
 * @param cmd_code 待查找的命令码
 * @return 1-找到匹配, 0-未找到
 */
uint8_t find_mc_cmd(uint8_t cmd_code)
{
    uint8_t array_size = sizeof(MC_cmd_header) / sizeof(MC_cmd_header[0]);
    
    for (uint8_t i = 0; i < array_size; i++) {
        if (MC_cmd_header[i] == cmd_code) {
            return 1;  // 找到匹配
        }
    }
    return 0;  // 未找到
}

void synch_time_handler(void)
{
    synch_state_time ++;
}

uint32_t synch_time_get(void)
{
   return synch_state_time;
}

/**
 * @brief 通过指定socket发送以太网数据帧（支持TCP/UDP协议）
 * @param Sour_Sock 目标socket编号（范围0-7，需预先配置协议类型）
 * @param Dest_Sock 目标socket编号（范围0-7，需预先配置协议类型）
 * @param buf 待发送数据缓冲区指针（需包含完整协议头）
 * @param len 待发送数据长度（字节）
 * @param destip 目标IP地址指针（网络字节序，UDP协议必须指定）
 * @param destport 目标端口号（主机字节序，UDP协议必须指定）
 * @return 执行结果：
 *         - 成功时返回实际发送的字节数
 *         - 失败时返回-1（需检查socket状态）
 * @note 功能特性：
 *       1. 自动适配TCP/UDP协议（根据socket配置）
 *       2. 内置W5500硬件加速校验和计算
 *       3. 支持阻塞/非阻塞模式（通过SN_MR_TCPND配置）
 *       4. 使用临界区保护防止并发访问导致卡死
 * @warning 使用限制：
 *         - TCP模式要求socket处于连接状态
 *         - UDP模式必须指定目标地址和端口
 *         - 缓冲区数据长度不得超过W5500 MTU限制（默认1460字节）
 *         - 调用此函数时，buf指向的缓冲区在发送期间不能被修改
 * @see wizchip_send_data() 底层发送函数
 */
void ethernet_send(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *buf, uint16_t len, uint8_t *destip, uint16_t destport) {
    
    int32_t ret = 0;
    uint8_t retry_count = 0;
    uint32_t buflen = len;
    if( len == 0 )
        return;

    /* ── socket 合法性兜底(必须保留) ──
     * WCHNET_SocketSend() 会以 socket 号索引其内部连接表，传入非法号(如 0xFF)
     * 将越界访问 SRAM 之外的地址，触发 Load access fault(现场实测 HardFault,
     * mcause=5, mtval 超出 64KB SRAM)。
     * 非法号来源：串口链路由 MC/Modbus 共用，周期性内部上报
     * (wizchip_*_to_PLC → MELSEC_FX_BuildEEWriteCmd(0xFF,0xFF,...)) 会把
     * uart_rx_ctx 覆盖为 0xFF，而这类上报本就无以太网来源、无需回包。*/
    if (Sour_Sock >= (uint8_t)WCHNET_MAX_SOCKET_NUM ||
        Dest_Sock >= (uint8_t)WCHNET_MAX_SOCKET_NUM) {
        log_warn("ethernet_send: 非法socket(Sour=%d Dest=%d len=%d), 丢弃\r\n",
                 Sour_Sock, Dest_Sock, len);
        return;
    }
    ETHERNET_DEBUG("t:%ums ETHTx S_id =%d D_id = %d \n", synch_time_get() ,Sour_Sock,Dest_Sock);
    eth_socket[Sour_Sock].net_tx_packets += len;        /* net发送包数 */   
    #if NET_LED_ENABLE == 1
    NEN_TX_LED_Trigger();  // 触发发送LED闪烁
    #endif //NET_LED_ENABLE == 1
    // 根据协议类型发送数据
    switch (eth_socket[Sour_Sock].Eth_Type) {
        case ETH_TYPE_TCP:
            // TCP 重发机制 
            do {
                ETHERNET_DEBUG(" TCP Tx len= %d \n", len);
                ret = WCHNET_SocketSend(Dest_Sock, buf, &buflen);
                if (ret >= 0) {
                    break;  // 发送成功
                }
                // 检查重发次数
                if (++retry_count >= NET_MAX_RETRY_COUNT) {
                    log_warn("\nTCP 重发次数超限: Dest_Sock=%d\n", Dest_Sock);
                    return;
                }
                log_err("\nTCP 发送失败: Dest_Sock=%d len=%d ERR=%d 重发[%d/%d]\n", Dest_Sock,len, ret, retry_count + 1, NET_MAX_RETRY_COUNT);
            } while (ret < 0 && retry_count < NET_MAX_RETRY_COUNT);
            break;

        case ETH_TYPE_UDP:
            // UDP 重发机制
            do {
                ETHERNET_DEBUG(" UDP Tx len= %d\n", len);
                ret = WCHNET_SocketUdpSendTo(Dest_Sock, buf, &buflen, destip, destport);
                if (ret >= 0) {
                    break;  // 发送成功
                }
                // 检查重发次数
                if (++retry_count >= NET_MAX_RETRY_COUNT) {
                    log_warn("\nUDP 重发次数超限: Dest_Sock=%d\n", Dest_Sock);
                    return;
                }
                log_err("\n UDP 发送失败: Dest_Sock=%d ERR=%d 重发[%d/%d]\n", Dest_Sock, ret, retry_count + 1, NET_MAX_RETRY_COUNT);
            } while (ret < 0 && retry_count < NET_MAX_RETRY_COUNT);
            break;

        default:
            log_warn(" 未知协议类型: Sour_Sock= %d\n", Sour_Sock );
            break;
    }
}

/**
 * @brief 发送 MC 协议错误代码应答帧
 *
 * 按源套接字链路类型, 以二进制或 ASCII 形式将
 * "子标题(sub_header 仅最高位有效) + 错误代码" 打包并发送。
 *
 * @param Sour_Sock  源套接字ID(合法范围 0~7)
 * @param Dest_Sock  目标套接字ID
 * @param sub_header 子标题字节(应答时最高位需置 1)
 * @param code       错误代码(对应 MC_EndingCode 枚举)
 */
void ethernet_error_code_ack (uint8_t Sour_Sock,uint8_t Dest_Sock,uint8_t sub_header, uint8_t code ) 
{
    // 套接字ID越界保护(eth_socket 仅 8 路)
    if(Sour_Sock > 7 )
        return;

    ETH_SOCKET *socket_p = &eth_socket[Sour_Sock];

    uint8_t  sub   = (uint8_t)(sub_header | 0x80); // 子标题最高位置一(应答标志)
    uint8_t  txbuf[4] = {0};                       // ASCII 模式最多 4 字节(每字节 2 字符)
    uint16_t lend  = 0;

    /* [A] Modbus session guard: a Modbus master cannot parse an MC error frame,
     * and sending it made a single request yield two replies (the first one
     * malformed) on the wire. mb_slave now reports such failures as a proper
     * Modbus exception, so only the MC frame itself is suppressed here; the
     * Error_Code upload to the PLC below is kept intact.
     * NOTE: ASCII only -- this file is GBK-encoded. */
    if (MB_Slave_IsModbusSock(Sour_Sock) != 0u) {
        MB_DEBUG("[SUPPRESS-MC-ERR] sock=%u Modbus session, MC err frame (sub=0x%02X code=0x%02X) not sent\r\n",
                 Sour_Sock, sub, code);
        if (socket_p->Error_Code) {
            wizchip_updata_socket_to_PLC(Sour_Sock, 1 );
        }
        return;
    }

    // 重新打包成MC协议格式的数据
    if( uart_mc_meta.Format_Code == 0x00 ){ // 0 : 二进制 格式 
        // 打包二进制格式数据
        txbuf[0] = sub;
        txbuf[1] = code;
        lend = 2;
    }
    else  {  // 1 : ASCII 格式, 每字节转 2 位十六进制字符
        byte_to_hex_chars(sub,  txbuf);      // 子标题 → 2 字符 (txbuf[0..1])
        byte_to_hex_chars(code, txbuf + 2);  // 错误码 → 2 字符 (txbuf[2..3])
        lend = 4;
    }

    ethernet_send(Sour_Sock, Dest_Sock,
                  txbuf, lend,
                  socket_p->destip, 
                  socket_p->destport);

    if( socket_p->Error_Code ){
        wizchip_updata_socket_to_PLC(Sour_Sock, 1 ); //更新socket 状态 到PLC 
    }    
   
}


void ethernet_info_default(void) 
{

    ETHERNET_DEBUG("网络通道没有使能 配置为 wiz 默认设置 \r\n");
    //  默认设置
    eth_socket_init_flg = 1;
    eth_ch = 1;
    eth_socket[0].EN = 1;                            // 启用 
    eth_socket[0].Eth_Type = ETH_TYPE_TCP;           // melsoft链接 类型
    eth_socket[0].local_port = 5556;                 // melsoft链接 固定 端口号:5556
    eth_socket[0].Pro_Type = 0xA0 ;                  // melsoft链接 类型
 
    eth_socket[1].EN = 1;                            // 启用 
    eth_socket[1].Eth_Type = ETH_TYPE_TCP;           // mc链接 类型 
    eth_socket[1].local_port = 5001;                 // mc链接 端口号:5001
    eth_socket[1].Pro_Type = 0xA6 ;                  // mc链接 类型 A6 :TCP MC协议

    sntp_time.sntp_enable = 0xFFF0 ;                 // 默认开启
    // SNTP服务器IP地址（IPv4格式）
    memcpy(&sntp_time.sntp_server_ip[0],&sntp_server_ip[0],4);
    sntp_time.GMT_Time_Hour = 0;            // 时区（GMT偏移）小时  
    sntp_time.GMT_Time_Minute = 0;          // 时区（GMT偏移）分钟
    sntp_time.execution_interval = 100;     // 执行间隔 0100 不启用 其他数据则启用

    WCHNET_GetMacAddr(MACAddr);             //  get the chip MAC address
    memcpy(Basic_CfgBuf.mac, MACAddr, 6); 
    memcpy(&Basic_CfgBuf.ip,&eth_Local_IP,4);
    memcpy(&Basic_CfgBuf.mask,&eth_subnet_mask,4);
    memcpy(&Basic_CfgBuf.gateway,&eth_gateway,4);
 
}

void ethernet_info_handler( uint8_t *buf, uint16_t len ,uint32_t offset_addr )
{    
    // 定义ethernet_Info结构体的起始地址和长度
    #define ETHERNET_INFO_ADDR    0x000C8  //200
    #define ETHERNET_INFO_SIZE    sizeof(ethernet_Info)
    ETHERNET_DEBUG("以太网配置信息处理 = EE_4x[%d], len=%d\r\n",offset_addr, len);

    if( offset_addr <= ETHERNET_INFO_ADDR && offset_addr + len >= ETHERNET_INFO_ADDR )
    {
        // 第一包数据 只包含了通道选择  通道选择ch1/ch2 ==  0x1001,0x8400,0xB80B,0xFEFF 数据出现的位置不一样.
        const uint16_t u8_ch_en[4] = {0x0110,0x0084,0x0BB8,0xFFFE};
        memset(&eth_socket,0,sizeof(ETH_SOCKET)*8 );
        for(int i=0;i<8;i++){
            eth_socket[i].SOCKET_ID = i;
            eth_socket[i].SocketIdForListen = 0xFF;  //Socket for Listening
        }
        /* b8～b11:连接号1～4 0:关闭中1:打开中*/
        net_status.u8val[1] = 0x00;                      // 全部关闭
        net_monitor_state = FX_MONITOR_IDLE;             // 监视状态  空闲

        uint16_t byte_offset = ETHERNET_INFO_ADDR - offset_addr; // 字节偏移量

        uint16_t *eth_data = (uint16_t *)(buf + byte_offset);
        ethernet_ch *eth_ch_info = (ethernet_ch *)eth_data;
        ETHERNET_DEBUG("byte_offset= %d, ch len=%d\r\n",byte_offset, sizeof(ethernet_ch));
        for(int i = 0; i < ( sizeof(ethernet_ch) / 2); i++)
        {
            ETHERNET_DEBUG("%04X ", eth_data[i]);
        }
        ETHERNET_DEBUG("\r\n");
        //通道选择ch1/ch2 ==  0x1001,0x8400,0xB80B,0xFEFF 数据出现的位置不一样.
        if( eth_ch_info->ch1_en[0] == u8_ch_en[0] || eth_ch_info->ch1_en[2] == u8_ch_en[2]  )
        {
            BSP_DEBUG("通道选择 ch1 \r\n");
            eth_ch = 1;
        }
        else if( eth_ch_info->ch2_en[0] == u8_ch_en[0] || eth_ch_info->ch2_en[2] == u8_ch_en[2]  )
        {
            BSP_DEBUG("通道选择 ch2 \r\n");
            eth_ch = 2;
        }
        else
        {
            ethernet_info_default();
            return;
        }
    }
    else if(offset_addr >= ETHERNET_INFO_ADDR && offset_addr + len >= ETHERNET_INFO_ADDR + ETHERNET_INFO_SIZE )
    {
        //第二包数据 包含了网络配置信息
        ethernet_Info *eth_info = (ethernet_Info *) buf ;
        if(eth_info->reserved_4[0] == 0xFFFF  && eth_info->reserved_4[1] == 0xFFFF )
        {
            ETHERNET_DEBUG("全FF数据，跳过\r\n");
            return;
        }
        eth_socket_init_flg = 1;
        //IP 地址赋值逻辑  高低位互换 
        for (int i = 0; i < 4; i++) {
            Basic_CfgBuf.ip[i] = eth_info->net_info.Local_IP[3 - i];
        }
        /* IP 地址 0.0.0.0 或 255.255.255.255 无效 → 回退默认 (Class B) */
        if ((Basic_CfgBuf.ip[0] == 0 && Basic_CfgBuf.ip[1] == 0 &&
             Basic_CfgBuf.ip[2] == 0 && Basic_CfgBuf.ip[3] == 0) ||
            (Basic_CfgBuf.ip[0] == 0xFF && Basic_CfgBuf.ip[1] == 0xFF &&
             Basic_CfgBuf.ip[2] == 0xFF && Basic_CfgBuf.ip[3] == 0xFF)) {
            memcpy(&Basic_CfgBuf.ip,&eth_Local_IP,4);
        }

        // 子网掩码 地址赋值逻辑  高低位互换 
        for (int i = 0; i < 4; i++) {
            Basic_CfgBuf.mask[i] = eth_info->net_info.Subnet_Mask[3 - i];
        }
        /* 子网掩码 0.0.0.0 或 255.255.255.255 无效 → 回退默认 255.255.0.0 (Class B) */
        if ( (Basic_CfgBuf.mask[0] == 0 && Basic_CfgBuf.mask[1] == 0 &&
            Basic_CfgBuf.mask[2] == 0 && Basic_CfgBuf.mask[3] == 0 ) ||
             (Basic_CfgBuf.mask[0] == 0xFF && Basic_CfgBuf.mask[1] == 0xFF &&
             Basic_CfgBuf.mask[2] == 0xFF && Basic_CfgBuf.mask[3] == 0xFF  ) )
        {
            Basic_CfgBuf.mask[0] = 0xFF;  /* 255 */
            Basic_CfgBuf.mask[1] = 0xFF;  /* 255 */
            Basic_CfgBuf.mask[2] = 0xFF;  /* 255 */ 
            Basic_CfgBuf.mask[3] = 0x00;  /* 0   */
        }
        // 路由器IP 地址赋值逻辑  高低位互换 
        for (int i = 0; i < 4; i++) {
            Basic_CfgBuf.gateway[i] = eth_info->net_info.Gateway[3 - i];
        }
        /* 网关未配置 (0.0.0.0或 255.255.255.255 无效 ) → 回退为本网段 .1 */
        if( (Basic_CfgBuf.gateway[0] == 0 && Basic_CfgBuf.gateway[1] == 0 &&
            Basic_CfgBuf.gateway[2] == 0 && Basic_CfgBuf.gateway[3] == 0) ||
            (Basic_CfgBuf.gateway[0] == 0xFF && Basic_CfgBuf.gateway[1] == 0xFF &&
            Basic_CfgBuf.gateway[2] == 0xFF && Basic_CfgBuf.gateway[3] == 0xFF) )
        {
            Basic_CfgBuf.gateway[0] = Basic_CfgBuf.ip[0];
            Basic_CfgBuf.gateway[1] = Basic_CfgBuf.ip[1];
            Basic_CfgBuf.gateway[2] = Basic_CfgBuf.ip[2];
            Basic_CfgBuf.gateway[3] = 1;
        }

        for(uint8_t port_id=0;port_id<4;port_id++)
        {   
            eth_socket[port_id].SOCKET_ID = port_id;
            eth_socket[port_id].EN = 1;                 // 0-不启用 1-启用 
            eth_socket[port_id].Pro_Type = eth_info->process[port_id].protocol_type;            // 协议类型： A0 
            eth_socket[port_id].local_port = eth_info->process[port_id].local_port;             // 本站端口号
            eth_socket[port_id].destport = eth_info->process[port_id].remote_port;              // 通信对象端口号
            // 通信对象IP地址（IPv4格式） 高低位互换
            eth_socket[port_id].destip[0] = eth_info->process[port_id].remote_ip[3];            
            eth_socket[port_id].destip[1] = eth_info->process[port_id].remote_ip[2];  
            eth_socket[port_id].destip[2] = eth_info->process[port_id].remote_ip[1];  
            eth_socket[port_id].destip[3] = eth_info->process[port_id].remote_ip[0];  
            // 协议配置数组（最多支持4个协议）
            BSP_DEBUG("port_id:%d 协议配置 = 0x%X ",port_id,eth_socket[port_id].Pro_Type);

            // 协议类型：A0 :TCP melsoft链接 A6 :TCP MC协议 A7:UDP MC协议   A8 :TCP 数据监控 
            switch (eth_socket[port_id].Pro_Type)
            {
                case 0xA0:
                    BSP_DEBUG("TCP melsoft链接 固定 端口号:5556");
                    eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
                    eth_socket[port_id].EN = 1;             // 0-不启用 1-启用
                    eth_socket[port_id].local_port = 5556;  // melsoft链接 固定 端口号:5556
                    eth_socket[port_id].Pro_ID = 0x00;    
                break;
                case 0xA6:
                    BSP_DEBUG("TCP MC协议 端口号:%d",eth_socket[port_id].local_port);
                    eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
                    eth_socket[port_id].Pro_ID = 0x01;    
                    if(eth_socket[port_id].local_port == 0)
                    {
                        eth_socket[port_id].EN = 0;           // 0-不启用 1-启用
                    }
                break;
                case 0xA7:
                    BSP_DEBUG("UDP MC协议 端口号:%d \n",eth_socket[port_id].local_port);
                    BSP_DEBUG("通讯对象 IP地址:%d.%d.%d.%d 端口号:%d",
                                        eth_socket[port_id].destip[0],
                                        eth_socket[port_id].destip[1], 
                                        eth_socket[port_id].destip[2], 
                                        eth_socket[port_id].destip[3],
                                        eth_socket[port_id].destport);

                    eth_socket[port_id].Eth_Type = ETH_TYPE_UDP;
                    eth_socket[port_id].Pro_ID = 0x02;    
                    // 通信对象IP地址 + 通信对象端口号 仅MC协议(UDP)时有效。 
                    if(  eth_socket[port_id].destport == 0 || 
                    (  eth_socket[port_id].destip[0] == 0 && 
                        eth_socket[port_id].destip[1] == 0 && 
                        eth_socket[port_id].destip[2] == 0 ) )
                    {
                        eth_socket[port_id].EN = 0;           // 0-不启用 1-启用
                    }
                break;
                case 0xA8:
                    BSP_DEBUG("TCP 数据监控( 网页 http )端口号:%d ",eth_socket[port_id].local_port);
                    eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
                    eth_socket[port_id].Pro_ID = 0x03;    
                break;
                default:
                    BSP_DEBUG("未知协议 protocol_type = 0x%X",eth_info->process[port_id].protocol_type);
                    eth_socket[port_id].Eth_Type = ETH_TYPE_NULL;
                    eth_socket[port_id].EN = 0;           // 0-不启用 1-启用
                    eth_socket[port_id].Pro_ID = 0xff;    
                break;
            }
            BSP_DEBUG("\r\n");
    
            // 检查是否已存在相同的协议类型和端口号组合
            for (int i = 0; i < port_id; i++) {
                if (eth_socket[i].Pro_Type == eth_socket[port_id].Pro_Type &&
                    eth_socket[i].local_port == eth_socket[port_id].local_port) {
                    eth_socket[port_id].EN = 0;           // 0-不启用 1-启用
                    break;
                }
            }
        }
    
        BSP_DEBUG("sntp_time = 0x%04X \r\n",eth_info->sntp_time.sntp_enable);
                 
        sntp_cfg_t sntp_cfg;
        sntp_cfg.raw = (uint8_t)(eth_info->sntp_time.sntp_enable & 0xff);  // 获取SNTP功能配置
        if ( !sntp_cfg.bits.enable || sntp_cfg.raw == 0xFF )  // bit7 == 0: 未启用
        {
            BSP_DEBUG("sntp_time 未启用 !!!\r\n" );
        }
        else
        {
            // 拷贝 sntp 参数
            memcpy(&sntp_time, &eth_info->sntp_time, sizeof(sntp_time));
            
            // SNTP服务器IP 地址赋值逻辑  高低位互换 
            for (int i = 0; i < 4; i++) {
                sntp_time.sntp_server_ip[i] = eth_info->sntp_time.sntp_server_ip[3 - i];
            }
            /* SNTP服务器IP未配置 (0.0.0.0) → 回退默认阿里云NTP */
            if (sntp_time.sntp_server_ip[0] == 0 && sntp_time.sntp_server_ip[1] == 0 &&
                sntp_time.sntp_server_ip[2] == 0 && sntp_time.sntp_server_ip[3] == 0) {
                memcpy(&sntp_time.sntp_server_ip[0], &sntp_server_ip[0], 4);
            }
            BSP_DEBUG("sntp_time 启用 \r\n");
            BSP_DEBUG("sntp_server_ip = %03d.%03d.%03d.%03d   ",
                      sntp_time.sntp_server_ip[0],
                      sntp_time.sntp_server_ip[1],
                      sntp_time.sntp_server_ip[2],
                      sntp_time.sntp_server_ip[3]);

            BSP_DEBUG("时区偏移 GMT %d:%d \r\n", sntp_time.GMT_Time_Hour,sntp_time.GMT_Time_Minute);

            /* sntp_cfg 位域详见 ethernet_app.h 中 sntp_cfg_t 定义 */

            /* ── Bit6: 电源启动时是否执行时间设置 ── */
            if (sntp_cfg.bits.startup) {
                BSP_DEBUG("sntp_time : 勾选 电源开始时执行时间设置. ");
            } else {
                BSP_DEBUG("sntp_time : 关闭电源开始时执行时间设置. ");
            }

            /* ── Bit5: 错误时行为 ── */
            if (sntp_cfg.bits.on_error) {
                BSP_DEBUG("错误:继续执行, \r\n");
            } else {
                BSP_DEBUG("错误:停止执行, \r\n");
            }

            /* ── Bit4: 执行模式（间隔 / 定时） ── */
            if (sntp_cfg.bits.exec_mode) {
                BSP_DEBUG("定时执行时间 %d:%d \r\n",
                          sntp_time.execution_time[0],
                          sntp_time.execution_time[1]);
            } else {
                BSP_DEBUG("间隔执行时间 %d 分钟\r\n",
                          sntp_time.execution_interval);
            }

            /* ── 全功能模式 (0xF0 = 启用+电源启动+错误继续+定时) → 额外确认日志 ── */
            if (sntp_cfg.raw == 0xF0) {
                BSP_DEBUG("sntp_time :电源开始时执行时间设置 \r\n");
            }
             
        } 
 
        if( eth_info->log_data.error_log_target_cnt <= 16 && eth_info->log_data.error_log_target_cnt >= 1){ 
            BSP_DEBUG("设置错误日志(1~ 16)");   
            BSP_DEBUG("记录件数 = %d  ",eth_info->log_data.error_log_target_cnt );   
            BSP_DEBUG("寄存器类型 = %s :%d 寄存器  \r\n",eth_info->log_data.error_log_target_reg==0x01? "D" : "R",eth_info->log_data.error_log_index);                
        }
        if(eth_info->log_data.access_log_target_cnt <= 16 && eth_info->log_data.access_log_target_cnt >= 1 ){ 
            BSP_DEBUG("设置访问日志 (1~ 16)"); 
            BSP_DEBUG("记录件数 = %d ",eth_info->log_data.access_log_target_cnt );   
            BSP_DEBUG("寄存器类型 = %s :%d 寄存器\r\n",eth_info->log_data.error_log_target_reg==0x01? "D" : "R",eth_info->log_data.error_log_index);                    
        }
        if(eth_info->log_data.time_target_cnt == 1 ){ 
            BSP_DEBUG("设置通讯日志(%04X) ",eth_info->log_data.time_target_cnt );  
            BSP_DEBUG("寄存器类型 = %s :%d寄存器\r\n",eth_info->log_data.time_target_reg==0x01? "D" : "R" ,eth_info->log_data.time_range );                    
        }
    }
}

 
// 构建网络 mac地址信息 到 PLC 内部EEPROM
void wizchip_EE_MAC_to_PLC(void)
{
    uint8_t mac_addr_buff[10] = {0};
    uint8_t cnt = 0;
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[4];
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[5];
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[2];
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[3];
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[0];
    mac_addr_buff[cnt++] = Basic_CfgBuf.mac[1];
    cnt += 2;
    ETHERNET_DEBUG("设置网络 mac地址信息 到 PLC 内部EEPROM len = %d \r\n", cnt);
    net_mc_meta.start_device = 0x50000 + 40;                       // 开始地址 
    net_mc_meta.device_count = cnt;                                // 设置字节个数  
    net_mc_meta.monitor_timer = 300;                               // 超时重发时间
    ETHERNET_DEBUG("type_EE start_device = 0x%04X(%d) device_count = %d \r\n",
                    net_mc_meta.start_device, 
                    net_mc_meta.start_device, 
                    net_mc_meta.device_count );

    // 将网口 信息 状态 写入 PLC 的诊断 寄存器中.
    MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF,net_mc_meta.start_device,
                    (const uint16_t *)&mac_addr_buff, cnt );
}
 
// 网络流量 (网络数据接收/发送总字节数据)
void wizchip_EE_net_bytes_info_to_PLC(void)
{
    // 4.各连接协议状态 tcp/udp 数据包接收+发送字节数
    typedef union
    {
        uint32_t u32val;        //无符号32位
        uint16_t u16val[2];     //无符号16位
        uint8_t u8val[4];       //无符号8位
    }union_packets;
    union_packets tcp_rx_packets, tcp_tx_packets, udp_rx_packets, udp_tx_packets;

    for(uint8_t i=0;i<4;i++)
    {
        if(eth_socket[i].Eth_Type ==  ETH_TYPE_TCP )
        {
            tcp_rx_packets.u32val += eth_socket[i].net_rx_packets ;
            tcp_tx_packets.u32val += eth_socket[i].net_tx_packets ;
        }
        else if( eth_socket[i].Eth_Type ==  ETH_TYPE_UDP )
        {
            udp_rx_packets.u32val += eth_socket[i].net_rx_packets ;
            udp_tx_packets.u32val += eth_socket[i].net_tx_packets ;
        }
    }

    ETHERNET_DEBUG("网络流量\n tcp rx:%08d,tx:%08d\nudp rx:%08d,tx:%08d \r\n",
                tcp_rx_packets.u32val,tcp_tx_packets.u32val,
                udp_rx_packets.u32val,udp_tx_packets.u32val); 
  
    // 构建网络流量 网络数据接收/发送总字节数据 到 PLC 内部EEPROM 
    uint8_t net_bytes_buff[20] = {0};   //数据包总数        ( 16 byte )
    uint8_t cnt = 0;
    net_bytes_buff[cnt++] = tcp_rx_packets.u8val[0] ;
    net_bytes_buff[cnt++] = tcp_rx_packets.u8val[1] ;
    net_bytes_buff[cnt++] = tcp_rx_packets.u8val[2] ;
    net_bytes_buff[cnt++] = tcp_rx_packets.u8val[3] ;

    net_bytes_buff[cnt++] = tcp_tx_packets.u8val[0] ;
    net_bytes_buff[cnt++] = tcp_tx_packets.u8val[1] ;
    net_bytes_buff[cnt++] = tcp_tx_packets.u8val[2] ;
    net_bytes_buff[cnt++] = tcp_tx_packets.u8val[3] ;

    net_bytes_buff[cnt++] = udp_rx_packets.u8val[0] ;
    net_bytes_buff[cnt++] = udp_rx_packets.u8val[1] ;
    net_bytes_buff[cnt++] = udp_rx_packets.u8val[2] ;
    net_bytes_buff[cnt++] = udp_rx_packets.u8val[3] ;  

    net_bytes_buff[cnt++] = udp_tx_packets.u8val[0] ;
    net_bytes_buff[cnt++] = udp_tx_packets.u8val[1] ;
    net_bytes_buff[cnt++] = udp_tx_packets.u8val[2] ;
    net_bytes_buff[cnt++] = udp_tx_packets.u8val[3] ;   
 
    
    net_mc_meta.start_device = 0x50000 +  144 ;                    // 开始地址
    net_mc_meta.device_count = E5000_PACKET2_SIZE;                 // 设置字节个数  
 
    net_mc_meta.monitor_timer = 300;                               // 超时重发时间

    ETHERNET_DEBUG("type_EE start_device = 0x%08X(%u) device_count = %d \r\n",
                        net_mc_meta.start_device ,
                        net_mc_meta.start_device , 
                        net_mc_meta.device_count );
 
    // 将网口信息状态写入PLC的诊断寄存器中
    MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF, net_mc_meta.start_device, 
                        (const uint16_t *)&net_bytes_buff, cnt);

}

/*********************************************************************
 * @fn      wizchip_EE_net_link_info_to_PLC
 *
 * @brief   将指定Socket的连接状态信息写入PLC内部EEPROM
 *          连接状态包含: 连接号、通讯对象端口/IP、错误码、协议/打开方式、TCP状态、强制禁用
 *          EE写入地址: 0x50000 + 48 + Sour_Sock * 16
 *
 * @param   Sour_Sock - Socket索引号(0~7)，对应eth_socket数组下标
 *
 * @return  无
 */
void wizchip_EE_net_link_info_to_PLC(uint8_t Sour_Sock,uint8_t link_state)
{
    /* 数组边界检查，防止eth_socket越界访问 */
    if (Sour_Sock >= sizeof(eth_socket) / sizeof(eth_socket[0])) {
        ETHERNET_DEBUG("wizchip_EE_net_link_info_to_PLC: Sour_Sock=%d out of range\r\n", Sour_Sock);
        return;
    }
    /* 构建网络连接状态信息，eth_link_t结构体占16字节 */
    uint8_t net_link_buff[sizeof(eth_link_t)] = {0};
    eth_link_t *eth_link_p = (eth_link_t *)net_link_buff;

    ETH_SOCKET *sock = &eth_socket[Sour_Sock];

    /* 本站端口号 - 高低字节交换(PLC大端序) */
    eth_link_p->link_id = SWAP_BYTES(sock->local_port);
    /* 通讯对象端口号 - 高低字节交换 */
    eth_link_p->remote_port = SWAP_BYTES(sock->destport);

    /* 通讯对象IP地址 - 字节序转换: [2][3][0][1] -> [0][1][2][3] */
    eth_link_p->remote_ip[0] = sock->destip[2];
    eth_link_p->remote_ip[1] = sock->destip[3];
    eth_link_p->remote_ip[2] = sock->destip[0];
    eth_link_p->remote_ip[3] = sock->destip[1];

    /* 错误代码 - 高低字节交换 */
    eth_link_p->error_code = SWAP_BYTES(sock->Error_Code);
    /* 打开方式 = 协议类型 | 0x0200 (0x02:TCP) */
    eth_link_p->open_mode = sock->Pro_Type | 0x0200;
    /* TCP连接状态: 移到高8位 (0:切断, 1:连接中) */
    eth_link_p->tcp_status = link_state<<8;
    /* 强制禁用状态: 0=允许, 1=禁用 */
    eth_link_p->force_disable = sock->EN << 8;

    ETHERNET_DEBUG("本站端口:%d 通讯对象:%03d.%03d.%03d.%03d:%03d\n",
                   sock->local_port,
                   sock->destip[0],
                   sock->destip[1],
                   sock->destip[2],
                   sock->destip[3],
                   sock->destport );

    ETHERNET_DEBUG("打开方式:0x%04X,%04X,%04X,%04X \r\n",
                    eth_link_p->open_mode,
                    eth_link_p->tcp_status,
                    eth_link_p->force_disable,
                    eth_link_p->error_code );

    /* 设置EE写入参数: 起始地址 = 基地址0x50000 + 包头48字节 + 连接号偏移 */
    uint16_t cnt = sizeof(eth_link_t);  /* 写入字节数 = 16 */
    net_mc_meta.start_device = 0x50000 + 48 + Sour_Sock * sizeof(eth_link_t);
    net_mc_meta.device_count = cnt;                                /* 设置字节个数 */
    net_mc_meta.monitor_timer = 300;                               /* 超时重发时间 */

    ETHERNET_DEBUG("type_EE start_device = 0x%08X(%u) device_count = %d \r\n",
                        net_mc_meta.start_device,
                        net_mc_meta.start_device,
                        net_mc_meta.device_count);

    /* 将网口连接状态写入PLC的诊断寄存器中 */
    MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF, net_mc_meta.start_device,
                             (const uint16_t *)&net_link_buff, cnt);
}
/*******************************************************************************
 * @fn      wizchip_updata_socket_to_PLC
 *
 * @brief   更新指定Socket的连接状态信息到PLC内部EEPROM
*           连接状态包含: 错误代码、打开方式、TCP状态、强制禁用
 *
 * @return  none
 ******************************************************************************/
void wizchip_updata_socket_to_PLC(uint8_t Sour_Sock,uint8_t link_state)
{        
    /* 构建网络连接状态信息，eth_link_t结构体占16字节 */
    uint8_t net_link_buff[sizeof(eth_link_t)] = {0};
    eth_link_t *eth_link_p = (eth_link_t *)net_link_buff;
    ETH_SOCKET *sock = &eth_socket[Sour_Sock];
    /* 单独 更新 网络连接 状态 */
    /* 错误代码 - 高低字节交换 */
    eth_link_p->error_code = SWAP_BYTES(sock->Error_Code); 
    /* 打开方式 = 协议类型 | 0x0200 (0x02:TCP) */
    eth_link_p->open_mode = sock->Pro_Type | 0x0200;
    /* TCP连接状态: 移到高8位 (0:切断, 1:连接中) */
    eth_link_p->tcp_status = link_state<<8;
    /* 强制禁用状态: 0=允许, 1=禁用 */
    eth_link_p->force_disable = sock->EN << 8;

    /* 设置EE写入参数: 起始地址 = 基地址0x50000 + 包头48字节 + 连接号偏移*16 + 数据偏移8 */
    net_mc_meta.start_device = 0x50000 + 48 + 8 + Sour_Sock * sizeof(eth_link_t);  // 8个字节偏移 
    net_mc_meta.device_count = 8;                                  /* 设置字节个数 */
    net_mc_meta.monitor_timer = 300;                               /* 超时重发时间 */

    ETHERNET_DEBUG("type_EE start_device = 0x%08X(%u) device_count = %d \r\n",
                        net_mc_meta.start_device,
                        net_mc_meta.start_device,
                        net_mc_meta.device_count);

    /* 将网口连接状态写入PLC的诊断寄存器中 */
    MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF, net_mc_meta.start_device,
                             (const uint16_t *)&net_link_buff+4, 
                             net_mc_meta.device_count );
}


// 网络连接状态 设置
void ethernet_connect_set( uint8_t Sour_Sock_id , uint8_t *destip, uint16_t destport ,uint8_t state )
{
    if(Sour_Sock_id < 8   )   
    {
        ETH_SOCKET *sock = &eth_socket[Sour_Sock_id];
        memcpy(sock->destip, destip, 4);          // IP地址(拷贝 destip 指向的 4 字节, 勿取 &destip 指针本身)
        sock->destport = destport;                // 端口号(按值直接赋值, 语义更清晰)
        // 网络连接状态
        if(state){
            net_connect.u16val |= (1U << Sour_Sock_id);   // 设定位
            wizchip_EE_net_link_info_to_PLC(Sour_Sock_id, state);  //更新socket 状态 到PLC 
        }
        else{
            net_connect.u16val &= ~(1U << Sour_Sock_id);  // 清除位
            wizchip_updata_socket_to_PLC(Sour_Sock_id, state);      //更新socket 状态 到PLC 
        }

        if( eth_socket[Sour_Sock_id].Pro_Type == PRO_TCPC_MELSOFT )
            net_monitor_state = state ? FX_MONITOR_RUNNING:FX_MONITOR_STOPPED; // 监视状态
        
        ETHERNET_DEBUG("连接号:%d ,%s\r\n",Sour_Sock_id,(state?"连接中":"断开"));
    }
}

void WCHNET_Create_Socket_info( void )
{
    socket_map_init();  /* 初始化 socketid → eth_socket[] 快速查表 */

    // 根据获取到的PLC网络信息连接网络，协议配置数组（最多支持4个协议）
    for (int port_id = 0; port_id < 4; port_id++) 
    {
        if( eth_socket[port_id].EN )  // 0-不启用 1-启用
        {
            /* b8～b11:连接号1～4 0:关闭中1:打开中*/
            net_status.u8val[1] |= (0x01 << port_id);  // 打开中

        }else{
            net_status.u8val[1] &= (0xfe << port_id);  // 关闭
            
            continue;       //配置下一个端口
        }
        ETHERNET_DEBUG("配置 协议类 端口号 :%d  PRO_Type : 0x%02X ", port_id,eth_socket[port_id].Pro_Type);

        //根据最终配置创建socket
        switch (eth_socket[port_id].Pro_Type )
        {
            case PRO_TCPC_MELSOFT :  // = 0xA0,          //TCP melsoft链接
            {
                ETHERNET_DEBUG("TCPS melsoft链接 0xA0  端口号:5556 \r\n");
                eth_socket[port_id].local_port = 5556;
                eth_socket[port_id].Pro_ID = 0x00;
                eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
                 /*TcpSocketListen */
                WCHNET_CreateTcpSocketListen(&eth_socket[port_id].SocketIdForListen, eth_socket[port_id].local_port);
            }
            break;
            case PRO_TCPC_MC :      // = 0xA6,           //TCP MC协议
            {
                ETHERNET_DEBUG("TCPS  0xA6 端口号:%d \r\n",eth_socket[port_id].local_port);
                /*TcpSocketListen */
                WCHNET_CreateTcpSocketListen(&eth_socket[port_id].SocketIdForListen, eth_socket[port_id].local_port);
                eth_socket[port_id].Pro_ID = 0x01;
                eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
            }
            break;
            case PRO_UDPC_MC :      // = 0xA7,           //UDP MC协议
            {
                /* code */
                ETHERNET_DEBUG("UDP MC协议 0xA7  端口号:%d \r\n",eth_socket[port_id].local_port);
                eth_socket[port_id].Pro_ID = 0x02;
                eth_socket[port_id].Eth_Type = ETH_TYPE_UDP;
                WCHNET_CreateUdpSocket(&eth_socket[port_id].SocketIdForListen, eth_socket[port_id].local_port);
            }
            break;
            case PRO_TCP_HTTP :     // = 0xA8            //TCP 数据监控
            {

                ETHERNET_DEBUG("TCPS  0xA8  端口号:%d \r\n",eth_socket[port_id].local_port);
                /*TcpSocketListen */
                WCHNET_CreateTcpSocketListen(&eth_socket[port_id].SocketIdForListen, eth_socket[port_id].local_port);
                eth_socket[port_id].Pro_ID = 0x03;
                eth_socket[port_id].Eth_Type = ETH_TYPE_TCP;
                #if SOCKET_HTTP_EN
                Init_Para_Tab();
                #endif
            }
            break;
        default:
            break;
        }
    }

#if SOCKET_DIRECT_EN == 1
    ETHERNET_DEBUG("4 直连TCPS melsoft链接 Pro_Type =0x%02X ,端口号:5558 \r\n",eth_socket[4].Pro_Type);
    /*TcpSocketListen */
    eth_socket[4].EN = 1; 
    eth_socket[4].local_port = 5558;
    eth_socket[4].Eth_Type = ETH_TYPE_TCP;  //TCPS
    eth_socket[4].Pro_ID = 0x04;
    WCHNET_CreateTcpSocketListen(&eth_socket[4].SocketIdForListen, eth_socket[4].local_port);
#endif 

#if SOCKET_DISCOVER_EN == 1

    ETHERNET_DEBUG("5 网络搜索设备端口 UDP Pro_Type =0x%02X ,端口号:5559 \r\n" ,eth_socket[5].Pro_Type);
    /*TcpSocketListen */
    eth_socket[5].EN = 1; 
    eth_socket[5].local_port = 5559;
    eth_socket[5].Eth_Type = ETH_TYPE_UDP;  //UDP
    eth_socket[5].Pro_ID = 0x05;


#if  SOCKET_SNTP_EN  == 1
    //sntp socke与UDP网络搜索功能共用 
    if ( sntp_time.sntp_enable  )                           // SNTP功能设置
    { 
        ETHERNET_DEBUG(" Initializing the SNTP client prot = 123 \r\n" );
        memcpy(eth_socket[5].destip, sntp_time.sntp_server_ip, 4 );             
        eth_socket[5].destport = 123;                               // 端口号  123
        // Initializing the SNTP client  中国 = 39
        SNTP_init(eth_socket[5].SocketIdForListen, sntp_time.sntp_server_ip,39); 
    }

#endif 

    WCHNET_CreateUdpSocket(&eth_socket[5].SocketIdForListen, eth_socket[5].local_port);

#endif

}


// 获取 D8013-D8018  rtc时间 
void wizchip_sntp_get_D8013_PLC(void)
{
    // (实时时钟用) D8013:秒  D8014:分  D8015:时  D8016:日  D8017:月  D8018:年  D8019:周
    ETHERNET_DEBUG("读取PLC的rtc时间 D8013-D8019 \n");
    net_mc_meta.device_name = MC_FX_D;  //  0x4420  数据寄存器  D 
    net_mc_meta.start_device = 8013;
    net_mc_meta.device_count = 14;  // 2字
    MELSEC_FX_BuildE00ReadCmd(0xFF, 0xFF, 8013, 14);    

}
// 控制 D8013-D8018 执行时间设置 状态
void wizchip_sntp_execute_D8013_PLC(void)
{
    extern _calendar_obj calendar;
    ETHERNET_DEBUG("[E10] sntp--> D8013-D8018 \r\n");
    RTC_Get();          // 更新RTC实时时钟       
    uint16_t sntp_data[10] = {0}; 
        
    ETHERNET_DEBUG("RTC时间: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                calendar.w_year, calendar.w_month, calendar.w_date,
                calendar.hour, calendar.min, calendar.sec);

    // 高低字节交换    
    sntp_data[6] = (uint16_t)calendar.week;
    sntp_data[5] = (uint16_t)calendar.w_year;
    sntp_data[4] = (uint16_t)calendar.w_month;
    sntp_data[3] = (uint16_t)calendar.w_date;

    sntp_data[2] = (uint16_t)calendar.hour;
    sntp_data[1] = (uint16_t)calendar.min;
    sntp_data[0] = (uint16_t)calendar.sec;

    net_mc_meta.start_device = 8013;        // 开始地址 D8013
    net_mc_meta.device_count = 7;           // 个数 
    net_mc_meta.device_name = MC_FX_D;      // 软元件名 D

    // 高八位和低八位互换
    for (uint16_t i = 0; i < net_mc_meta.device_count; i++) {
        uint16_t tmp = sntp_data[i];
        sntp_data[i] = (tmp >> 8) | (tmp << 8);
    }
    MELSEC_FX_BuildE10WriteParamCmd(0xFF, 0xFF,
                                    net_mc_meta.start_device ,
                                    (const uint16_t *)sntp_data,
                                    net_mc_meta.device_count );
 
}

void wizchip_Analysis_M_info( uint8_t *buf, uint16_t len,uint32_t start_dev )
{
    /* 调试输出：打印接收到的原始数据 */
    ETHERNET_DEBUG("解析 PLC M[%d]寄存器 信息 len=%d\r\n",start_dev, len);
    // for (int i = 0; i < len; i++) {
    //     ETHERNET_DEBUG("%02X ", buf[i]);
    // }
    // ETHERNET_DEBUG("\r\n");
    
    /**
     * 二进制字数据格式说明（已由hex_str_to_intlend转换）：
     * - 每个字软元件占1字节 == 8个bit 
     * - 转换示例：ASCII "12AB" → parse_buf → 0x12, 0xAB
     */
    uint16_t resp_data = 0;
    for (uint16_t i = 0; i < uart_mc_meta.device_count; i++) {
        uint16_t src_idx = i * 2;  /* 每个字2字节二进制 */
        resp_data = ((uint16_t)buf[src_idx+1] << 8) | buf[src_idx] ;
        if( start_dev >= 8400 && start_dev <= 8432 )
        {
            // 解析 M8404 FX3U-ENET-ADP单元就绪 
            //ETHERNET_DEBUG("M[%d]=0x%04X \r\n ",start_dev,resp_data);
            for(uint8_t j = 0; j < 16; j++ )
            {
                //ETHERNET_DEBUG("M[%d]=%d  ",start_dev+j, (resp_data >> j) & 1 );
                if( start_dev+j == 8411 || start_dev+j == 8431)
                {
                    set_ntp_retry_state( (resp_data >> j) & 1 ) ;  //   执行时间设置*1 M8411 M8431
                }
            }
        }
        start_dev += 16; // 软元件地址偏移
    }
}

 
// 获取 PLC 网口 信息 M8411 M8431 执行时间设置*1 ON后， 以太网适配器执行时间设置。
// M8404 -- M8498  特殊辅助继电器 95个bit
void wizchip_get_PLC_M8404_info(void)
{
    // M 辅助继电器 
    net_mc_meta.start_device = 8400;        // 开始地址 M8400/M8430 地址:8对齐
    net_mc_meta.device_count = 2;           // 读取个数 2* 16bit --end=M8432
    net_mc_meta.device_name = MC_FX_M;      // 软元件名
    ETHERNET_DEBUG("E00 主动获取 PLC M[%d-%d] 辅助继电器\r\n ",net_mc_meta.start_device, net_mc_meta.start_device + 16);
    MELSEC_FX_BuildE00ReadCmd( 0xFF , 0xFF, net_mc_meta.start_device, net_mc_meta.device_count);
}

// 解析PLC 系统参数配置信息 读PLC参数 plc_param_EE_4x
void wizchip_Analysis_E1_info(uint8_t *buf, uint16_t len,uint32_t start_dev)
{
    ETHERNET_DEBUG("系统参数 start_dev= %d ,size=%d\r\n ",
            start_dev, uart_mc_meta.device_count);

    uint16_t resp_data = 0;
    for (uint16_t i = 0; i < uart_mc_meta.device_count; i++) {
        uint16_t src_idx = i * 2;  /* 每个字2字节二进制 */
        resp_data = ((uint16_t)buf[src_idx + 1] << 8) | buf[src_idx];
        //ETHERNET_DEBUG("EE_4x[%d]:0x%04X(%d)\n",start_dev,resp_data,resp_data);
        switch (start_dev)
        {
        case 0:     ETHERNET_DEBUG("总容量=总块数[%d]\r\n",resp_data); 
            break;
        case 2:
        {
            switch(resp_data)
            {
                case 0x193E: ETHERNET_DEBUG(" 打勾内置定位设置(18块) \r\n");
                    break;
                case 0x28DE: ETHERNET_DEBUG(" 打勾特殊模块初始值设置(8块)  \r\n");
                    break;
                case 0x82E1: ETHERNET_DEBUG(" 打勾内置cc-link/lt设置(1块) \r\n");
                    break;
                case 0xF070: ETHERNET_DEBUG(" 打勾内置定位设置(18块) + 打勾特殊模块初始值设置(8块) \r\n");
                    break;
                case 0xABBF: ETHERNET_DEBUG(" 打勾内置定位设置(18块) + 打勾内置cc-link/lt设置(1块) \r\n");
                    break;
                case 0x9C1F: ETHERNET_DEBUG(" 打勾特殊模块初始值设置(8块) + 打勾内置定位设置(18块) + 打勾内置cc-link/lt设置(1块) \r\n"); 
                    break; 
                default:
                    ETHERNET_DEBUG(" 没有打勾特殊设置 \r\n");
                    break;
            }
        } break;
        case 34:    ETHERNET_DEBUG("块数: 程序[34]= 0x%x \r\n",resp_data);          
        break;
        case 35:    ETHERNET_DEBUG("特殊 占用块数 0x%x \r\n",resp_data);            //特殊设置容量占用块数 
        break;
        case 36:    ETHERNET_DEBUG("总块数 0x%x \r\n",resp_data);                   //总块数-注释设置块数-文件寄存器块数
        break;
        case 37:    ETHERNET_DEBUG("文件(0~14块) 0x%x \r\n",resp_data);             //文件(0~14块)
        break;
        case 38:    ETHERNET_DEBUG("注释设置块数 0x%x \r\n",resp_data);             //总块数-注释设置块数
        break;
        case 39:    ETHERNET_DEBUG("注释(0~127块) 0x%x \r\n",resp_data);           //注释(0~127块)
        break;
        case 40:    ETHERNET_DEBUG("PLC系统设置(1) 勾无电池运行+用户登录:%04X\r\n",resp_data); 
        break;
        case 41:    ETHERNET_DEBUG("PLC系统设置(2) ch1通信格式:%04X\r\n",resp_data); 
            break;
        case 42:    ETHERNET_DEBUG("PLC系统设置(2) ch1超时时间=%d 设定站号:%d\r\n",resp_data>>8,resp_data&0xFF); 
            break;
        case 43:    ETHERNET_DEBUG("PLC系统设置(2) ch2通信格式:%04X\r\n",resp_data); 
            break;
        case 44:    ETHERNET_DEBUG("PLC系统设置(2) ch2超时时间=%d 设定站号:%d\r\n",resp_data>>8,resp_data&0xFF); 
            break;
        default:
            break;
        }
        start_dev++;
    }

} 

/*********************************************************************
 * @fn      wizchip_Analysis_D_info
 *
 * @brief   解析PLC D寄存器数据并更新系统时间
 *
 * @param   buf - 接收到的数据缓冲区指针
 *          len - 数据长度
 *
 * @return  无
 *
 * @note    PLC D寄存器地址映射(用于时间同步):
 *          - D8013: 秒
 *          - D8014: 分
 *          - D8015: 时
 *          - D8016: 日
 *          - D8017: 月
 *          - D8018: 年
 *          - D8019: 星期
 *********************************************************************/
void wizchip_Analysis_D_info(uint8_t *buf, uint16_t len,uint32_t start_dev)
{
    
    /* 调试输出：打印接收到的原始数据 */
    ETHERNET_DEBUG("解析 PLC D[%d]寄存器 信息 len=%d\r\n",start_dev, len);
    // for (int i = 0; i < len; i++) {
    //     ETHERNET_DEBUG("%02X ", buf[i]);
    // }
    // ETHERNET_DEBUG("\r\n");
    
    /**
     * 二进制字数据格式说明（已由hex_str_to_intlend转换）：
     * - 每个字软元件占2个字节
     * - 转换示例：ASCII "12AB" → parse_buf → 0x12, 0xAB
     *   - buf[src_idx]     → 低字节 (0x12)
     *   - buf[src_idx + 1] → 高字节 (0xAB)
     *   - 结果: 0xAB12
     */
    uint16_t resp_data = 0;
    for (uint16_t i = 0; i < uart_mc_meta.device_count; i++) {
        uint16_t src_idx = i * 2;  /* 每个字2字节二进制 */

        resp_data = ((uint16_t)buf[src_idx + 1] << 8) | buf[src_idx];

        /* 根据寄存器地址更新对应的时间字段 */
        switch (start_dev) {
            case 8003:  // D8003保存内置存储器
                switch (resp_data) {
                    case 0x0000:  // RAM存储器盒
                        ETHERNET_DEBUG("RAM存储器盒%04X\r\n",resp_data);
                        break;
                    case 0x0001:  // EPROM存储器盒
                        ETHERNET_DEBUG("EPROM存储器盒%04X\r\n",resp_data);
                        break;
                    case 0x0002:  // EEPROM存储器盒或是快闪存储器盒 保护开关:OFF
                        ETHERNET_DEBUG("存储器盒 保护开关:OFF %04X\r\n",resp_data);
                        break;
                    case 0x000A:  // EEPROM存储器盒或是快闪存储器盒 保护开关:ON 
                        ETHERNET_DEBUG("存储器盒 保护开关:ON %04X\r\n",resp_data);
                        break;
                    case 0x0010:  // 可编程控制器内置存储器
                        ETHERNET_DEBUG("可编程控制器内置存储器%04X\r\n",resp_data);
                        break;
                }
                break;
            case 8013:  // D8013: 秒
                calendar.sec = resp_data;
                break;
            case 8014:  // D8014: 分
                calendar.min = resp_data;
                break;
            case 8015:  // D8015: 时
                calendar.hour = resp_data;
                break;
            case 8016:  // D8016: 日
                calendar.w_date = resp_data;
                break;
            case 8017:  // D8017: 月
                calendar.w_month = resp_data;
                break;
            case 8018:  // D8018: 年
                calendar.w_year = resp_data;
                break;
            case 8019:  // D8019: 星期
                calendar.week = resp_data;
                break;
            default:
                /* 其他寄存器地址，不处理 */
                break;
        }
        start_dev++;
        /* 如果起始地址是D8013(秒)，表示完整时间数据已接收，更新RTC */
        if ( start_dev == 8018 ) {
            /* 打印同步后的时间信息 */
            ETHERNET_DEBUG("同步 PLC 时间: %04d-%02d-%02d  %02d:%02d:%02d\r\n",
                    calendar.w_year, calendar.w_month, calendar.w_date,
                    calendar.hour, calendar.min, calendar.sec);

            /* 设置RTC实时时钟 */
            RTC_Set(calendar.w_year, calendar.w_month, calendar.w_date,
                    calendar.hour, calendar.min, calendar.sec);
            
            /* 更新RTC实时时钟缓存 */
            RTC_Get();
        }
    }

}


// D8427 / D8407 保存以太网适配器的以太网端口连接状态。
void wizchip_PHY_Link_PLC(uint8_t link_status)
{
    // 判断使用通道1还是通道2，确定D寄存器基地址
    uint16_t base_addr = 0; 
    if(eth_ch == 1){      
        base_addr = 8406;       // 通道1: D8406-D8407
    }
    else if( eth_ch == 2){
        base_addr = 8426;       // 通道2: D8426-D8427
    }
    else{
        return;
    }
    uint16_t Link_data[2] = {0};
    ETH_connect Link_status = {0};

    Link_status.bit.b10 = link_status;         // b10:连接状态      0:集线器或对象设备未连接或断线    1:与集线器或对象设备连接中
    Link_status.bit.b14 = 1;                   // b14:数据传送速度  0:正在以10BASE-T运行             1:正在以100BASE-TX运行

    net_status.bit.link_b7 = link_status;      // b7:网络使能状态  0:网络未使能             1:网络已使能

    Link_data[0] = net_status.u16val;          // 网络使能状态;
    Link_data[1] = Link_status.u16val;         // 链路状态

    ETHERNET_DEBUG("eth_ch:%d ,PHY_Link=%d -->> PLC D[%d]=%d - D[%d]=%d\r\n ", 
                    eth_ch,link_status, 
                    base_addr, Link_data[0],
                    base_addr + 1, Link_data[1]); 
    //D8406 D8426 状态信息
    net_mc_meta.device_name = MC_FX_D;         // 软元件名
    net_mc_meta.start_device = base_addr;      // 起始软元件      
    net_mc_meta.device_count = 2;       

    MELSEC_FX_BuildE10WriteParamCmd( 0xFF, 0xFF,
                                    net_mc_meta.start_device, 
                                    (const uint16_t *)&Link_data, 
                                    net_mc_meta.device_count);
}

 // 状态:  网线插入后,又拔掉
void Wizchip_PHY_Link_Disconnect(void)  
{
    /* 链路断开，进入同步状态 */
    uart_req_queue_init();                  // 清除发送FIFO缓冲区 

    net_status.bit.com_err_b4 = 1;          // 1:通信异常显示
    // 写入网络配置信息到PLC 写入串口fifo,等待完成
    net_status.bit.link_b7    = 0;          // b7: 1:Link信号ON0:Link信号OFF

}

/**
 * @brief 处理设备发现协议帧（带TCP/UDP协议支持）
 * @param sock 目标socket编号（需预先建立连接）
 * @param frame_buff 协议帧数据缓冲区指针
 * @param frame_len 协议帧数据长度（字节）
 * @param destip 目标IP地址指针（网络字节序）
 * @param destport 目标端口号（主机字节序）
 * @return 执行结果：
 *         - 成功时返回实际发送的数据长度
 *         - 失败时返回-1（需检查socket状态）
 * @note 功能特性：
 *       1. 支持TCP/UDP协议自动判断（根据socket类型）
 *       2. 内置帧校验和超时重传机制
 *       3. 自动适配W5500硬件协议栈
 * @warning 使用限制：
 *         - 要求目标socket必须处于连接状态（TCP）
 *         - frame_buff需包含完整协议头（含帧起始标记）
 * @see ethernet_send() 基础发送函数
 */
int process_Discover_device(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                            uint8_t* frame_buff, uint16_t frame_len, 
                            uint8_t * destip, uint16_t destport )
{
    /*  //搜索网络上的fx3u 设备  
    pc:    5C 00 00 00 00 11 11 07 00 00 FF FF 03 00 00 FC 03 00 00 1F 00 1C 08 0A 08 00 00 00 00 00 00 00 04 0E 04 01 00 00 00 02 45 45 31 31 30 30 46 30 30 03 46 35 
    文本数据：...........q...E..P{...@.<..............<n.\.......................................EE1100F00.F
    plc:   DC 00 01 00 00 11 11 07 00 00 00 FC 03 00 FF FF 03 00 00 86 00 9C 00 0C 08 00 00 00 00 00 04 00 00 00 00 0E 04 01 00 00 00 64 01 A8 C0 00 00 FF FF 3C E5 B2 
    8A 52 58 46 58 33 55 2F 46 58 33 55 43 20 20 20 20 20 20 00 00 20 00 00 00 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 
    20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 E0 01 A8 C0
    文本数据： .......XR...<..E....3..@......d...........S.........................................d.......<...RXFX3U/FX3UC      .. ...                                                                          ?ɡ
     */
    /*  原厂回复：DC 00 16 00 00 
    //11 11 07 00 00 00 FC 03 00 FF FF 03 00 00 86 00 9C 00 0C 08 00 00 00 00 00 04 00 00 00 00 0E 04 01 00 00 00 
    FA 01 A8 C0    //设备网关 192.168.1.250
    00 00 00 00    //设备子网 掩码 255.255.0.0
    3C E5 B2 8A 52 58 
    46 58 33 55 2F 46 58 33 55 43   //设备型号 FX3U/FX3UC
    20 20 20 20 20 20 00 00 20 00 00 00
    20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 
    20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 20 
    DF 01 A8 C0 //设备 IP地址 192.168.1.223
    */
    // 参数有效性检查
    if (frame_buff == NULL || destip == NULL || frame_len == 0) {
        log_err("Invalid parameters in process_Discover_device");
        return -1;
    }

    static const uint8_t pattern[] = "EE1100F00";
    int pos = find_subarray(frame_buff, frame_len, pattern, sizeof(pattern)-1);

    if (pos != -1 && pos < frame_len) 
    {
        
        /* 设备发现应答模板 (151B)，static const 置于 flash，免栈拷贝 */
        static const uint8_t ack_5C[] = { 
            0xDC,0x00,0x01,0x00,0x00,0x11,0x11,0x07,0x00,0x00,0x00,0xFC,0x03,0x00,0xFF,0xFF,
            0x03,0x00,0x00,0x86,0x00,0x9C,0x00,0x0C,0x08,0x00,0x00,0x00,0x00,0x00,0x04,0x00,0x00,0x00,0x00,0x0E,
            0x04,0x01,0x00,0x00,0x00,0x64,0x01,0xA8,0xC0,0x00,0x00,0xFF,0xFF,0x3C,0xE5,0xB2,0x8A,
            0x52,0x58,0x46,0x58,0x33,0x55,0x2F,0x46,0x58,0x33,0x55,0x43,      //RXFX3U/FX3UC    PLC型号
            0x20,0x20,0x20,0x20,0x20,0x20,0x00,0x00,0x20,0x00,0x00,0x00,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,0x20,
            0xE0,0x01,0xA8,0xC0      //IP 地址  192.168.1.224
        };
        uint32_t ack_len = sizeof(ack_5C);
        uint8_t eth_buf[sizeof(ack_5C)+1];
        memcpy(eth_buf, ack_5C, ack_len);
        eth_buf[2] = 16;  // 也许是设备的通讯数据代码设备 0x16：二进制码通讯  0x00：ASCII码通讯
        uint16_t ack_cnt = 41;  // 本机网络信息开始地址
        //本机 IP 地址 (大端→小端: 逐字节倒序)
        eth_buf[ack_cnt++] = Basic_CfgBuf.ip[3];
        eth_buf[ack_cnt++] = Basic_CfgBuf.ip[2];
        eth_buf[ack_cnt++] = Basic_CfgBuf.ip[1];
        eth_buf[ack_cnt++] = Basic_CfgBuf.ip[0];
        //本机 子网掩码 (大端→小端: 逐字节倒序)
        eth_buf[ack_cnt++] = Basic_CfgBuf.mask[3];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mask[2];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mask[1];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mask[0];
        //本机 MAC 地址 (大端→小端: 逐字节倒序)
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[5];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[4];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[3];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[2];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[1];
        eth_buf[ack_cnt++] = Basic_CfgBuf.mac[0];
        //本机 设备型号
//        memcpy(&eth_buf[ack_cnt], ACK_Model,sizeof(ACK_Model));
//        ack_cnt += sizeof(ACK_Model);

        // 上位机 IP 地址 (大端→小端: 逐字节倒序)
        eth_buf[ack_len-1] = destip[0];
        eth_buf[ack_len-2] = destip[1];
        eth_buf[ack_len-3] = destip[2];
        eth_buf[ack_len-4] = destip[3];

        ethernet_send( Sour_Sock , Dest_Sock, eth_buf, ack_len, destip, destport);
        ETHERNET_DEBUG("响应 : 搜索网络上的fx3u %d.%d.%d.%d\r\n",
                        Basic_CfgBuf.ip[0],
                        Basic_CfgBuf.ip[1],
                        Basic_CfgBuf.ip[2],
                        Basic_CfgBuf.ip[3] );
    }
}


int process_Shake_hands(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t* frame_buff, uint16_t frame_len, uint8_t * destip, uint16_t destport)
{
/**            
     pc:    5B 00 00 01           //握手 1
     plc:   DB 00 00 01 01 00 C0 A8 01 E0 00 00 00 00 00 00 00 00  //返回网口 IP //C0 A8 01 E0 == 192.168.1.224
     pc:    5B 00 00 11           //握手 2
     plc:   DB 00 00 11 03 00 C0 A8 01 E0 00 00 00 00 00 00 00 00  //返回网口 IP //C0 A8 01 E0 == 192.168.1.224
     pc:    5B 00 00 21          //握手 3
     plc:   DB 00 00 21 02 00 C0 A8 01 E0 C0 A8 01 E0 00 00 00 00  //C0 A8 01 E0 == 192.168.1.224
*/
    uint8_t Shake_hands_ack[] = {0xDB ,0x00 ,0x00 ,0x21 ,0x02 ,0x00 ,0xC0 ,0xA8 ,0x01 ,0xE0 ,0xC0 ,0xA8 ,0x01 ,0xE0 ,0x00 ,0x00 ,0x00 ,0x00};
    // 提取功能码
    uint8_t func_code = frame_buff[3];
    Shake_hands_ack[3] = func_code;
 
    ETHERNET_DEBUG("握手cmd=0x%02x\r\n",func_code);
 
    // 根据功能码执行不同逻辑
    switch (func_code) {
        case 0x01:
            Shake_hands_ack[4] = 0x01;
            //ip地址 
            memcpy(Shake_hands_ack + 6, Basic_CfgBuf.ip, 4);
            memset(Shake_hands_ack + 10, 0, 4);
        break;
        case 0x11:
            Shake_hands_ack[4] = 0x03;
            //ip地址 
            memcpy(Shake_hands_ack + 6, Basic_CfgBuf.ip, 4);
            memset(Shake_hands_ack + 10, 0, 4);
        break;
        case 0x21:
             Shake_hands_ack[4] = 0x02;
             //ip地址 
             memcpy(Shake_hands_ack + 6, Basic_CfgBuf.ip, 4);
             memcpy(Shake_hands_ack + 10,Basic_CfgBuf.ip, 4);
        break;
        default:
            return 1; // 不支持的功能码
    }
    //回复网络
    ethernet_send(Sour_Sock,Dest_Sock,Shake_hands_ack, sizeof(Shake_hands_ack), destip, destport);
}




/* ─────────────────────────────────────────────────────────────
 * 功能：处理本机（CH32V307 自身）的设备数据
 *
 * 说明：上位机通过 MC 协议读/写本机内部数据（如网络配置、运行状态等），
 *       这些数据不转发到外部 PLC，而是由本机直接处理。
 *
 * 数据格式：PLC 响应为 ASCII 十六进制字符串
 *   buf[0]              = STX (0x02)
 *   buf[1] ~ buf[len-4]  = 数据区（ASCII 十六进制，每 2 字符 = 1 字节）
 *   buf[len-3] ~ buf[len-2] = 校验和（ASCII 十六进制）
 *   buf[len-1]           = ETX (0x03)
 *
 * 参数：
 *   buf  : 完整的 PLC 响应帧（含 STX/ETX），按 STX 对齐
 *   len  : buf 的有效字节数
 * ──────────────────────────────────────────────────────────── */
void sim_Process_local_machine (uint8_t *buf,uint16_t len)
{

    /* 校验帧长度：STX + 至少 1 字节数据(2字符) + 校验和(2字符) + ETX = 至少 6 字符 */
    /* 当 len < 3 时，仅有 STX+ETX 或更短，视为设置成功的无数据应答 */
    if( len < 3 ){
        ETHERNET_DEBUG("本机设备 设置成功应答 ACK=0x%02X\n", buf[0]);
        return ;
    }

    /* 从出队已恢复的 uart_mc_meta 中提取当前请求的上下文信息 */
    uint16_t device_name  = uart_mc_meta.device_name;   /* 寄存器类型（M/D/E1/E4） */
    uint32_t start_dev    = uart_mc_meta.start_device;   /* 起始设备编号 */
    MELSEC_DEBUG("本设备数据处理 device name:%c%c[%d],count:%d \r\n",
        device_name>>8, device_name&0xFF, start_dev, uart_mc_meta.device_count);

    // #ifdef _TRANSMISSION_DEBUG
    //     /* 打印原始 ASCII 响应帧：前缀为 hex STX，数据区按 ASCII 字符打印，末尾 3 字节打印 hex */
    //     TRANSMISSION_DEBUG("0x%02x ", buf[0]);
    //     for (uint16_t i = 1; i < len - 1; i++) {
    //         if (i > len - 1 - 3) {
    //             TRANSMISSION_DEBUG(" 0x%02x %C%C ", buf[i], buf[i+1], buf[i+2]);
    //             i += 3;
    //         } else {
    //             TRANSMISSION_DEBUG("%C", buf[i]);
    //         }
    //     }
    //     TRANSMISSION_DEBUG("\r\n+++++++++++++++++++++++++++\r\n");
    // #endif

    /* ─── ASCII 十六进制 → 二进制数据解析 ─── */
    /* 跳过 STX(buf[0])，数据区长度 = (总长 - STX - 校验和(2字符) - ETX) / 2 */
    uint8_t parse_buf[MELSEC_FX_MAX_DATA_LEN];
    uint16_t out_len = (len - 4) / 2;
    hex_str_to_intlend(buf + 1, out_len, parse_buf);   /* 从 buf[1] 开始，每 2 个 ASCII 字符转 1 字节 */

    /* ─── 根据寄存器类型分发到不同的解析函数 ─── */
    switch (device_name)
    {
        case MC_FX_M:   /* 辅助继电器 M — 位操作，需特殊位提取处理 */
            wizchip_Analysis_M_info(parse_buf, out_len, start_dev);
            break;
        case MC_FX_D:   /* 数据寄存器 D — 16 位字数据 */
            wizchip_Analysis_D_info(parse_buf, out_len, start_dev);
            break;
        case MC_FX_E1:  /* PLC 系统参数（型号、版本等，基本配置） */
            wizchip_Analysis_E1_info(parse_buf, out_len, start_dev);
            break;
        case MC_FX_E4:  /* PLC 系统参数 + 网口配置（IP/掩码/网关/MAC 等） */
            ethernet_info_handler(parse_buf, out_len, start_dev);
            break;
        default:
            MELSEC_DEBUG("未知寄存器类型: 0x%04X\r\n", device_name);
            break;
    }

} 

/**
 * @brief  MELSOFT 透传：PLC 响应原样返回给网络上位机
 * @note   不做协议解析，仅做最小帧头修正后原路透传
 * @param  Dest_Sock  目标 Socket ID
 * @param  buf        已对齐 STX 的 PLC 响应数据
 * @param  len        响应数据字节数
 */
void sim_Process_TCP_MELSOFT(uint8_t Dest_Sock, uint8_t *buf, uint16_t len)
{    
    /*
     * 原路透传到上位机：
     * - 正常响应 (len > 1): 直接透传完整帧
     * - 空响应 (len <= 1): 仅回 1 字节确认 (对应上位机设置指令)
     * buf[0] 已由上层置为 STX (0x02)，无需重复赋值
     */
    /* 必须用 uint32_t：WCHNET 库内部 SocketTcpSend 使用 lw(4字节加载) 读取该指针，
     * uint16_t 仅保证 2 字节对齐，在特定栈布局下会触发 RISC-V 非对齐异常 (mcause=4) */
 
    uint32_t data_len = (len > 1) ? (uint32_t)len : 1u;
    WCHNET_SocketSend(Dest_Sock, buf, (uint32_t *)&data_len);
    ETHERNET_DEBUG("t:%ums,melsoft tx len=%u\r\n",synch_state_time, (unsigned int)data_len);
}
 
/**
 * @brief 串口 接收任务回调函数
 * @note 在串口 接收数据 中调用，解析数据任务
 * 
 * @note 数据流闭环关键点：
 *       1. 上下文在发送时保存到 uart_rx_ctx + uart_mc_meta（uartSendNextPacket/uartTxWithSocketID）
 *       2. 本函数从 uart_rx_ctx 读取上下文，确保响应路由到正确的客户端
 *       3. 多客户端并发时，队列机制保证请求-响应配对正确
 */
void sim_Process_switch(uint8_t *buf, uint16_t len)
{
    /* ─── 从 uart_rx_ctx 读取请求上下文（由 uartProcessDeferredRx 从队列恢复）─── */
    uint8_t  Sour_Sock   = uart_rx_ctx.Sour_Sockid;

    /* ─── Modbus 从站回程优先 ───
     * 若该 socket 存在在途 Modbus 事务，则本次串口响应属于 Modbus，
     * 由 Modbus 模块还原为 Modbus 响应回送，不再进入 MC 处理路径。 */
    if (MB_Slave_HandleSerialResp(buf, len) == 0)
    {
        return;
    }

    if (Sour_Sock < WCHNET_MAX_SOCKET_NUM)
    {
        /* ─── 分支1：根据协议类型路由响应 ─── */
        ETH_SOCKET *eth_ptr = &eth_socket[Sour_Sock];
        uint8_t  pro_t = eth_ptr->Pro_Type;
        /* 串口链路被 MC/Modbus 共用，周期性内部上报
         * (wizchip_*_to_PLC → MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF, ...)) 会把
         * uart_rx_ctx.Dest_Sockid 覆盖为 0xFF(255)；非法号一旦进入
         * WCHNET_SocketSend() 会越界索引其内部连接表并触发 HardFault。
         * 故此处兜底回落到"请求来源 socket"。 */
        uint8_t  Dest_Sock   = uart_rx_ctx.Dest_Sockid;
        if (Dest_Sock >= (uint8_t)WCHNET_MAX_SOCKET_NUM) {
            Dest_Sock = Sour_Sock;
        }
        MELSEC_DEBUG(" 协议 pro_t=0x%02X\r\n", pro_t);
        switch (pro_t)
        {
            case PRO_TCPC_MC:    /* TCP MC协议 */
            case PRO_UDPC_MC:    /* UDP MC协议 */
            {
                
                MELSEC_DEBUG("PLC响应 %s 指令:%s \r\n", 
                    (uart_mc_meta.Format_Code == 0x00) ? "二进制" : "ASCII",
                    (uart_mc_meta.sub_header <= 0x01) ? "读出" : "写入");
                    
                MELSEC_DEBUG("Socket%d PLC响应到达,len=%u \r\n", Sour_Sock,  len);
                uint8_t tx_buf[ETH_TXBUF_MAX_SIZE];   /* BuildSendResp 从 offset=0 逐字节写入，无需零初始化 */
                uint16_t ETH_tx_len;
                
                MC_Net_BuildSendResp(buf , tx_buf, &ETH_tx_len);

            #ifdef _TRANSMISSION_DEBUG
                TRANSMISSION_DEBUG("MC协议响应帧, len=%d \n", ETH_tx_len);
                if (uart_mc_meta.Format_Code == 0x00) {
                    for (uint16_t i = 0; i < ETH_tx_len; i++) {
                        TRANSMISSION_DEBUG("%02x ", tx_buf[i]);
                    }
                } else {
                    for (uint16_t i = 0; i < ETH_tx_len; i++) {
                        TRANSMISSION_DEBUG("%c", tx_buf[i]);
                    }
                }
                TRANSMISSION_DEBUG("\r\n====================\r\n");
            #endif
                ethernet_send(Sour_Sock, Dest_Sock, tx_buf, ETH_tx_len, 
                            eth_ptr->destip, eth_ptr->destport);
            }break;
        #if SOCKET_HTTP_EN
            case PRO_TCP_HTTP:   /* TCP HTTP协议 */
            {
                Web_Usart_Handler(Sour_Sock, Dest_Sock, buf, len);
            }break;
        #endif
            case PRO_TCPC_MELSOFT:  /* TCP Melsoft协议 */
            default:
            {
                // 透传数据到上位机
                sim_Process_TCP_MELSOFT(Dest_Sock,buf, len);
                eth_socket[Sour_Sock].net_tx_packets += len;        /* net发送包数 */   
            }break;
        }
    }
    else
    {
        /* ─── 分支2：本机参数处理（Sour/Dest 为 0xFF 表示本机请求）─── */
        sim_Process_local_machine(buf, len);  // 处理获取plc数据到本机
    }

}
/**
 * @brief 处理握手数据帧（fx3u网络测试通讯）
 * @param Sour_Sock 套接字号
 * @param frame_buff 数据指针
 * @param frame_len 数据长度
 * @param Dest_Sock 套接字号
 * @return 0: 处理成功; -1: 数据无效; -2: 功能码不支持
 *
 * @note 缓冲区保护机制:
 *       1. 快速复制数据到临时缓冲区处理,避免共用缓冲区冲突
 *       2. 使用局部栈缓冲区(最大256字节)存储协议处理数据
 *       3. 处理完成后立即清空接收缓冲区
 */ 
int  Analysis_eth_frame_handler(uint8_t Sour_Sock ,uint8_t  Dest_Sock, 
                uint8_t* frame_buff, uint16_t frame_len,
                uint8_t * ipaddr, uint16_t port)  
{
    
    // #ifdef _ETHERNET_DEBUG
    // ETHERNET_DEBUG("\ntime_log = %u \n net rxbuf[0]= 0x%02X len= %d\r\n" ,synch_state_time , frame_buff[0],frame_len );
    // for(int i = 0; i < frame_len; i++)
    // {
    //     ETHERNET_DEBUG("%02X ", frame_buff[i]);
    // }
    // ETHERNET_DEBUG("\r\n========================\r\n");
    // #endif
    // 根据socket ID 找到对应的协议类型
    PRO_Type Pro_Type = eth_socket[Sour_Sock].Pro_Type ;
 
    ETHERNET_DEBUG("\nt:%ums ,Net rx len= %d, Sour_Sock=%d,Dest_Sock=%d, Pro_Type=0x%02X \r\n" ,
                        synch_state_time ,
                        frame_len ,
                        Sour_Sock, 
                        Dest_Sock,
                        Pro_Type);

    switch ( Pro_Type )
    {
        case PRO_TCPC_MC :         // = 0xA6,           //TCP MC协议
        case PRO_UDPC_MC :         // = 0xA7,           //UDP MC协议
        {
            /* ── Modbus TCP 从站分流 ──
             * Modbus 与 MC 共用同一端口，故先按 MBAP 特征识别 Modbus 请求；
             * 命中则由 Modbus 模块转换为三菱串口命令，不再进入 MC 解析路径。
             * 判据要求"协议ID==0 且长度域自洽 且功能码合法"，避免误判 MC 二进制帧。 */
            if (MB_Slave_IsFrame(frame_buff, frame_len)) {
                MB_Slave_HandleRequest(Sour_Sock, Dest_Sock, frame_buff, frame_len);
                return 0;
            }

            //ETHERNET_DEBUG("\r\n header  binary=0x%02X  -- ascii=0x%02X \r\n",header_binary, header_ascii );
            if(frame_buff[0] < 0x30 ) {
                //ETHERNET_DEBUG("MC 协议 :二进制格式的协议数据 \r\n");
                net_mc_meta.Format_Code   = 0;  // 0：二进制 格式
                if( find_mc_cmd(frame_buff[0]) ) {
                    MC_Net_binary_ParseRecvResp(Sour_Sock,Dest_Sock, frame_buff, frame_len);
                    return 0;
                }
                //报错 处理  超出范围  副标题的命令 (00～05H， 13～16H)的代码。
                eth_socket[Sour_Sock].Error_Code = 2558; //命令、子命令的指定有误      
                ethernet_error_code_ack(Sour_Sock,Dest_Sock, frame_buff[0],MC_END_ILLEGAL_SUBTITLE );
            } else {
                //ETHERNET_DEBUG("MC 协议 :ascii格式的协议数据\r\n");
                net_mc_meta.Format_Code   = 1;    // 1 : ASCII 格式  
                uint8_t header_ascii = AsciiHexToUint8(frame_buff[0], frame_buff[1]);
                if( find_mc_cmd(header_ascii) ) {
                    MC_Net_ASCII_ParseRecvResp(Sour_Sock,Dest_Sock, frame_buff, frame_len);
                    return 0;
                } 
                //报错 处理  超出范围  副标题的命令 (00～05H， 13～16H)的代码。   
                eth_socket[Sour_Sock].Error_Code = 2558; //命令、子命令的指定有误       
                ethernet_error_code_ack(Sour_Sock,Dest_Sock, header_ascii,MC_END_ILLEGAL_SUBTITLE );
            }

            MELSEC_DEBUG("副标题: 0x%02X 超出范围, 返回错误\r\n", frame_buff[0]);
            return -1;
        
        }break;
        case PRO_TCP_HTTP :       // = 0xA8           //TCP 数据监控
        {
            #if SOCKET_HTTP_EN
            //ETHERNET_DEBUG(" TCP 数据监控 \r\n" );
            Web_Server(Sour_Sock, Dest_Sock, frame_buff, frame_len );
            #endif
        }break;

        case PRO_TCPC_MELSOFT :    // = 0xA0,      //tcp melsoft链接
        default:
        {
            if( frame_buff[0] == 0x05 )
            {
                //三菱触摸屏的握手应答  
                uint8_t buff[2] = {0x02,0x02};
                ethernet_send(Sour_Sock,Dest_Sock, buff, 1, ipaddr, port);
            }
            else if( frame_buff[0]  == 0x5B) {
                // 直接 应答 网口数据 :  握手
                process_Shake_hands(Sour_Sock,Dest_Sock, frame_buff, frame_len, ipaddr,port);
            }
            else
            {
                //网口转串口， 修改帧头为0x02并转发到PLC
                // MELSOFT 透传路径不使用 mc_meta，无需清零
                frame_buff[0] = frame_buff[0]&0x7F;
                #if UART_USE_FIFO
                    uartTxWithSocketID(Sour_Sock, Dest_Sock,frame_buff, frame_len);
                #else
                    uartSendPacketLen(Sour_Sock,Dest_Sock,frame_buff,frame_len);
                #endif
            }
            //ETHERNET_DEBUG("S_id=%d,D_id=%d, Pro_Type=0x%02X\r\n",Sour_Sock, Dest_Sock,Pro_Type );
            net_monitor_state = FX_MONITOR_RUNNING; // 监视状态
        }break;
    }

}

/**
 * @brief   PLC同步状态机 - 顺序发送PLC同步命令
 *
 * 将原始字节命令转换为调用MELSEC_FX_Build函数的形式:
 *   E01读取: BuildE0xReadCommon(0xFF, 0xFF, eType, addr, len)
 *   E00读取: MELSEC_FX_BuildE00ReadCmd(0xFF, 0xFF, eType, addr, len)
 *   EE读取:  MELSEC_FX_BuildEEReadCmd(0xFF, 0xFF, addr, len)
 *   F5读取:  MELSEC_FX_BuildF5ReadCmd(0xFF, 0xFF, addr_data, addr_len, count)
 *
 * @param   Index - 同步命令索引 (0~13)
 * @return  0-成功, -1-索引越界
 */
int SyncStateMachine_PLC(uint8_t Index)
{
    ETHERNET_DEBUG("\n++++++++++++++++++\n Sync_PLC, Index=%d:",Index);
    switch (Index) 
    {
    case 0:  /* BAUD_SYNC_PLC_Capacity:  E01800002 - E01 读取PLC参数, 2字 */
        ETHERNET_DEBUG("E01800002 \n");  
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 0;
        net_mc_meta.device_count = 1;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8000, 0x02/2,'1');
        break;
    case 1:  /* BAUD_SYNC_PLC_SYSTEM_1:  E0180440C - E01 读取PLC参数, 12字 */
        ETHERNET_DEBUG("E0180440C \n");
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 44;
        net_mc_meta.device_count = 0x0C/2;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8044, 0x0C/2 ,'1');
        break;
    case 2:  /* BAUD_SYNC_PLC_D8000:     E008000FE - E00读取D8000 , 254字 */
        ETHERNET_DEBUG("E008000FE \n");
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8000;
        net_mc_meta.device_count = 0xFE/2;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8000, 0xFE/2,'0');
        break;
    case 3:  /* BAUD_SYNC_PLC_D8254:     E0080FE02 - E00读取D8254, 2字 */
        ETHERNET_DEBUG("E0080FE02 \n");  // \STX0000\ETXC3
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8254;
        net_mc_meta.device_count = 1;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF,0x8254, 0x02/2,'0');
        break;
    case 4:  /* BAUD_SYNC_PLC_F5011:     F501108006 - F5读取1个元件, simType=0x10,addr=0x8006 */
    {
        ETHERNET_DEBUG("F501108006 \n");        // \STX1000\ETXC4
        const uint32_t f5_addr4 = 0x00108006;  /* simType=0x10, address=0x8006 */
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8254;
        net_mc_meta.device_count = 1;  // 2字
        MELSEC_FX_BuildF5ReadCmd(0xFF, 0xFF, &f5_addr4, 1);
        break;
    }
    case 5:  /* BAUD_SYNC_PLC_F5011:     F501108006 - F5读取1个元件, simType=0x10,addr=0x8006 */
    {
        ETHERNET_DEBUG("F501108006 \n");        // \STX1000\ETXC4
        const uint32_t f5_addr4 = 0x00108006;  /* simType=0x10, address=0x8006 */
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8254;
        net_mc_meta.device_count = 1;  // 2字
        MELSEC_FX_BuildF5ReadCmd(0xFF, 0xFF, &f5_addr4, 1);
        break;
    }
    case 6:  /* BAUD_SYNC_PLC_D8338:     E00815202 - E00读取D8152, 2字 */
        ETHERNET_DEBUG("E00815202 \n");   // \STX0000\ETXC3
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8152;
        net_mc_meta.device_count = 1;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8152, 1,'0');
        break;
 
    case 7:  /* BAUD_SYNC_PLC_SYSTEM_2:  E01804602 - E01 读取PLC参数 , 2字 */
        ETHERNET_DEBUG("E01804602 \n");
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 46;
        net_mc_meta.device_count = 1;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8046, 1,'1');
        break;
    case 8:  /* BAUD_SYNC_PLC_SYSTEM_3:  E01804602 - E01 读取PLC参数 , 2字 */
        ETHERNET_DEBUG("E01804602 \n");
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 46;
        net_mc_meta.device_count = 0x02/2;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF,0x8046, 1,'1');
        break;

    case 9:  /* BAUD_SYNC_PLC_D8338:     E00815202 - E00读取D8152, 2字 */
        ETHERNET_DEBUG("E00815202 \n");   // \STX0000\ETXC3
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8152;
        net_mc_meta.device_count = 1;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8152, 1,'0');
        break;

    case 10:  /* BAUD_SYNC_PLC_SYSTEM_4:  E01800808 - E01 读取PLC参数 , 8字 */
        ETHERNET_DEBUG("E01800808 \n"); //\STX2020202020202020\ETX13
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 8;
        net_mc_meta.device_count = 4;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8008, 4 ,'1');
        break;

    case 11:  /* BAUD_SYNC_PLC_SYSTEM_5:  EE0 0004 0000 0008 - EE读取0x00040000, 8字 */
        ETHERNET_DEBUG("EE0 0004 0000 0008 \n");  // \STX EE00004000 00008 \ETX09  \STXFFFFFFFFFFFFFFFF\ETX63
        net_mc_meta.device_name = MC_FX_E4;
        net_mc_meta.start_device = 0;
        net_mc_meta.device_count = 8;  // 2字
        MELSEC_FX_BuildEEReadCmd(0xFF, 0xFF, 0x40000, 8); //EE 0000 04000 0008
        break;
    case 12: /* BAUD_SYNC_PLC_SYSTEM_6:  E0180005C - E01 读取PLC参数 , 92字 */
        ETHERNET_DEBUG("E0180005C \n");
        net_mc_meta.device_name = MC_FX_E1;
        net_mc_meta.start_device = 0;
        net_mc_meta.device_count = 0x5C/2;  // 2字
        BuildE0xReadCommon(0xFF, 0xFF, 0x8000, 0x5C/2,'1');
        
        break;
    case 13: /* BAUD_SYNC_PLC_SYSTEM_7:  EE00004000000FE - EE读取0x00040000, 254字 */
        ETHERNET_DEBUG("EE0 0004 0000 00FE \n");
        net_mc_meta.device_name = MC_FX_E4;
        net_mc_meta.start_device = 0;
        net_mc_meta.device_count = 0xFE;  // 2字
        MELSEC_FX_BuildEEReadCmd(0xFF, 0xFF, 0x40000, 0xFE);
        
        break;
    case 14: /* BAUD_SYNC_PLC_SYSTEM_8:  EE0000400FE0096 - EE读取0x00400FE, 150字 */
        ETHERNET_DEBUG("EE0000400FE0096 \n");
        net_mc_meta.device_name = MC_FX_E4;
        net_mc_meta.start_device = 0xFE;
        net_mc_meta.device_count = 0x96;  // 2字
        MELSEC_FX_BuildEEReadCmd(0xFF, 0xFF, 0x400FE, 0x96);
        break;
    case 15: /* BAUD_SYNC_PLC_F50104261: F50104261 - F5读取1个元件, simType=0x00,addr=0x4261 */
    {
        const uint32_t f5_addr13 = 0x00004261;  /* simType=0x00(位元件), address=0x4261 */
        ETHERNET_DEBUG("F50104261 \n");
        net_mc_meta.device_name = MC_FX_D;
        net_mc_meta.start_device = 8003; // 保存内置存储器
        net_mc_meta.device_count = 1;  // 2字
        MELSEC_FX_BuildF5ReadCmd(0xFF, 0xFF, &f5_addr13, 1);
        break;
    }
    case 16: /* 读取PLC的rtc时间 为后面的记录保存时间做准备 */
    {
        wizchip_sntp_get_D8013_PLC();
        break;
    }
    default:
    
        return -1;
    }

}




void ethernet_app_task(void)
{
    /* Modbus 从站周期任务：在途事务超时检测（串口无响应时回异常码 0x0B） */
    MB_Slave_Tick();

    static NET_INIT_STATE net_state = NET_INIT_STATE_START; // 网口初始化流程状态
    static uint32_t uart_time = 0;
    static uint32_t uart_time_cnt = 0;
    uint32_t uart_time_new = synch_state_time/500;          // 单位（10ms）
    
    switch ( net_state )
    {
        case NET_INIT_STATE_START:
        {
            ETHERNET_DEBUG("net version:0x%02X\n", WCHNET_GetVer());
            if(WCHNET_LIB_VER != WCHNET_GetVer()){
                ETHERNET_DEBUG("version error.\n");
            }
            /* wizchip init */
            net_state = NET_INIT_STATE_READ_EE;
            eth_socket_init_flg = 0;            // 获取PLC的网络信息标志位
            uart_time = uart_time_new;
        }break;

        case NET_INIT_STATE_READ_EE:
        {
            static uint8_t plc_cmd_index = 0;    // PLC同步命令索引
            static uint8_t plc_cmd_sent = 0;     // 当前命令是否已发送 0:未发送 1:已发送等待完成
            static uint8_t plc_cmd_retry = 0;    // 当前命令重试计数
           /* 等待上一条命令发送完成后再发下一条 */
            if (plc_cmd_sent == 1) {
                if (uart_data_t.tx_status != IDLE) {
                    break;  // 等待发送完成
                }
                plc_cmd_sent = 0;
                plc_cmd_index++;
            }
            /* 所有命令发送完成(index 0~13, 共14条), 进入WAIT_EE状态 */
            if (plc_cmd_index > 16) {
                plc_cmd_index = 0;
                plc_cmd_sent = 0;
                plc_cmd_retry ++;
                if (eth_socket_init_flg == 1  ) {
                    /* PLC网络信息已获取，重置同步状态并跳转 */
                    net_state = NET_INIT_STATE_WAIT_EE;
                    plc_cmd_retry = 0;
                }else if ( plc_cmd_retry > 3) {
                    ethernet_info_default();       //   默认配置
                    net_state = NET_INIT_STATE_WAIT_EE;
                    plc_cmd_retry = 0;
                }
                uart_time = uart_time_new;
                break;
            }
            /* 逐条发送PLC同步命令 */
            if (SyncStateMachine_PLC(plc_cmd_index) == 0) {
                plc_cmd_sent = 1;
            }
        }break;

        case NET_INIT_STATE_WAIT_EE:
        {
            /* 等待获取PLC网络信息完成 */
            if (eth_socket_init_flg == 1) {
                /* PLC网络信息已获取，读取MAC地址并进入下一状态 */
                WCHNET_GetMacAddr(MACAddr);
                memcpy(Basic_CfgBuf.mac, MACAddr, 6);
                net_state = NET_INIT_STATE_SET_E5xx;
                uart_time = uart_time_new;
            } else if (uart_time != uart_time_new) {
                /* 超时重试：回到READ_EE重新读取 */
                net_state = NET_INIT_STATE_READ_EE;
                if (++uart_time_cnt > 5) {
                    /* 重试超5次，使用默认网络配置 */
                    ETHERNET_DEBUG("eth_socket_init_flg out time \r\n");
                    uart_time_cnt = 0;
                    ethernet_info_default();
                    net_state = NET_INIT_STATE_SET_E5xx;
                }
                uart_time = uart_time_new;
            }
        }break;

        case NET_INIT_STATE_SET_E5xx:
        {
            if (uart_time != uart_time_new) {
                wizchip_EE_MAC_to_PLC();       // 将网络配置信息写入PLC EEPROM
                net_state = NET_INIT_STATE_LIB_INIT;   // 进入以太网库初始化
                uart_time = uart_time_new;
            }
        }break;

        case NET_INIT_STATE_LIB_INIT:
        {
            //Ethernet library initialize
            BSP_DEBUG("本地IP  : %03d.%03d.%03d.%03d \r\n",
                        Basic_CfgBuf.ip[0],Basic_CfgBuf.ip[1],
                        Basic_CfgBuf.ip[2],Basic_CfgBuf.ip[3] );

            BSP_DEBUG("子网掩码: %03d.%03d.%03d.%03d \r\n",
                        Basic_CfgBuf.mask[0],Basic_CfgBuf.mask[1],
                        Basic_CfgBuf.mask[2],Basic_CfgBuf.mask[3]);

            BSP_DEBUG("路由器IP: %03d.%03d.%03d.%03d \r\n",
                        Basic_CfgBuf.gateway[0],Basic_CfgBuf.gateway[1],
                        Basic_CfgBuf.gateway[2],Basic_CfgBuf.gateway[3]);

            BSP_DEBUG("设备MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                        Basic_CfgBuf.mac[0], Basic_CfgBuf.mac[1], Basic_CfgBuf.mac[2],
                        Basic_CfgBuf.mac[3], Basic_CfgBuf.mac[4], Basic_CfgBuf.mac[5]);

            uint8_t ret = ETH_LibInit(Basic_CfgBuf.ip,Basic_CfgBuf.gateway,Basic_CfgBuf.mask,Basic_CfgBuf.mac);
            mStopIfError(ret);
            if(ret == WCHNET_ERR_SUCCESS)  BSP_DEBUG("WCHNET_LibInit Success\r\n");

        #if KEEPALIVE_ENABLE                                               //Configure keep alive parameters
            {
                struct _KEEP_CFG cfg;
                cfg.KLIdle = 20000;
                cfg.KLIntvl = 15000;
                cfg.KLCount = 9;
                WCHNET_ConfigKeepLive(&cfg);
            }
        #endif

            BSP_DEBUG(" 协议 配置数组 \r\n ");

            WCHNET_Create_Socket_info (  );         // 协议 配置数组
            net_status.bit.init_b0    = 1;          // 初始化处理正常结束
            net_status.bit.speed_b2   = 1;          // 1:100Mbps
            net_status.bit.set_err_b3 = 0;          // 0:设置正常显示
            net_status.bit.com_err_b4 = 0;          // 0:通信正常
            net_status.bit.init_err_b5 = 0;          // 0:初始化处理正常结束
            net_status.bit.b6          = 0;          // 0:未链接时
            net_status.bit.link_b7     = 1;          // b7: 1:Link信号ON0:Link信号OFF
        #if SOCKET_HTTP_EN
            MITSU_HTTP_Init();
        #endif
  
            clr_net_MC_Recv();
            MB_Slave_Init();                        /* Modbus 从站模块初始化 */
            /* 写入 Basic 配置到 Flash (擦除→写入→校验, 原子操作) */
            BSP_FLASH_WriteConfig(BASIC_CFG_ADDR, (uint8_t *)&Basic_CfgBuf, BASIC_CFG_LEN);	
            net_state = NET_INIT_STATE_WRITE_PHY_CHANGE;
            uart_time = uart_time_new;
        }break;
        case NET_INIT_STATE_WRITE_PHY_CHANGE:// 状态: 写入PHY配置变化
        {
            /* 等待 网络 上线  */
            WCHNET_MainTask();
            if(WCHNET_QueryGlobalInt())
            {
                u8 intstat;
                intstat = WCHNET_GetGlobalInt();                              // get global interrupt flag
                if (intstat & GINT_STAT_PHY_CHANGE)                           // PHY status change
                {
                    uint8_t phy_status = WCHNET_GetPHYStatus();
                    if (phy_status & PHY_Linked_Status)
                    {
                        WCH_DEBUG("PHY Link Success!!!\r\n");
                        net_status.bit.link_b7 = 1;
                        net_status.bit.com_err_b4 = 0;          // 0:通信正常显示
                        wizchip_PHY_Link_PLC(1);                // 发送PLC的网络M状态
                        net_state = NET_INIT_STATE_MainTask;
                        uart_time = uart_time_new;
                        uart_time_cnt = 0;

                    }
                }
            }
        }break;
         
        case NET_INIT_STATE_MainTask:
        {
 
            /*Ethernet library main task function,
            * which needs to be called cyclically*/
            WCHNET_MainTask();
            /* 所有配置包发送完毕，直接进入主循环 */
            /*Query the Ethernet global interrupt,
            * if there is an interrupt, call the global interrupt handler*/
            if( WCHNET_QueryGlobalInt() )
            {
                WCHNET_HandleGlobalInt();
            } 

        }break;

        default:
        {
            break;
        }
    }
 
}








