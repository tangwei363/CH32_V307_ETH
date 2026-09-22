/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_status.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP 通信状态页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_status.h"
#include "HTTPS.h"
#include "html_components.h"
#include "index.h"
#include "ethernet_app.h"

/* 语言选择字符串 - 中文 */
const char* Lang_Select_ZS = "ZS";

/*********************************************************************
 * @fn      FX_STATUS_GenerateConnectionTable
 *
 * @brief   生成连接状态表格HTML
 *
 * @param   无
 *
 * @return  生成的HTML字符串
 */
char* FX_STATUS_GenerateConnectionTable(void)
{
    char *table_buffer = MITSU_HTTP_GetTableBuffer();
    char *temp_buffer = HtmlBuffer;
 
    /* 打开方式文本：下标即 Pro_ID（0x00 TCP MELSOFT / 0x01 TCP MC / 0x02 UDP MC
     * 0x03 TCP 数据监视 / 0x04 直连 TCPS MELSOFT(5558) / 0x05 网络搜索 UDP(5559)）
     * 原表仅 4 项，Pro_ID=0x04/0x05 会越界读到数组外 → 页面显示 "(null)"，故补齐 */
    const char* open_type_str[] = {"MELSOFT连接", "MC协议", "MC协议", "数据监视", "MELSOFT连接", "网络搜索"};
    /* TCP状态文本：顺序必须与 net_status_t 枚举严格一致
     * （0 NONE / 1 LISTEN / 2 ESTABLISHED / 3 CLOSED / 4 ERROR）
     * 原数组与枚举错位（LISTEN 被显示成"已建立"等），一并纠正 */
    const char* tcp_state_str[] = {"----", "监听", "已建立", "切断", "错误"};
    uint8_t i;
    
    MITSU_HTTP_ClearTableBuffer();
    
    /* 表格头部 */
    MITSU_HTTP_Emit(
           "<table border=\"1\" cellspacing=\"1\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td>\r\n"
           "<table border=\"1\" cellspacing=\"0\" bgcolor=\"#ffffff\" style=\"text-align:center;font-size:14px\">\r\n"
           "<tbody>\r\n"
           "<tr bgcolor=\"#cccccc\">\r\n"
           "<td width=\"110\">连接号/功能</td>\r\n"
           "<td width=\"90\">本站端口号</td>\r\n"
           "<td width=\"110\">通信对象<br>IP地址</td>\r\n"
           "<td width=\"80\">通信对象<br>端口号</td>\r\n"
           "<td width=\"90\">最新<br>错误代码</td>\r\n"
           "<td width=\"70\">协议</td>\r\n"
           "<td width=\"160\">开放方式</td>\r\n"
           "<td width=\"90\">TCP状态</td>\r\n"
           "<td width=\"90\">强制禁用<br>状态</td>\r\n"
           "</tr>\r\n");

    /* 生成连接表格 */
    for (i = 0; i < ETH_MAX_CONNECTIONS; i++) {
        char port_str[16];
        char remote_port_str[16];
        char error_code_str[16];

        ETH_SOCKET *eth_socket_p = &eth_socket[i];
 
        if (eth_socket_p->Pro_Type == PRO_TCPC_MELSOFT) {
            /* 直接连接MELSOFT */
            sprintf(temp_buffer,
                    "<tr><td height=\"24\" style=\"text-align:center;font-size:10px;background-color:#cccccc\">直接连接MELSOFT</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>0.0.0.0</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>----</td>\r\n"
                    "<td>否</td>\r\n"
                    "</tr>\r\n");
        } else {
            /* 文本下标先做边界检查：eth_socket[4]/[5] 的 Pro_ID=0x04/0x05
             * 超出原数组范围，直接索引会读数组外内存 → 页面出现 "(null)" */
            const char *open_str;
            const char *state_str;

            open_str  = (eth_socket_p->Pro_ID < (uint8_t)(sizeof(open_type_str) / sizeof(open_type_str[0])))
                        ? open_type_str[eth_socket_p->Pro_ID] : "----";
            state_str = ((unsigned)eth_socket_p->net_stat < (unsigned)(sizeof(tcp_state_str) / sizeof(tcp_state_str[0])))
                        ? tcp_state_str[eth_socket_p->net_stat] : "----";

            /* 格式化端口为字符串 */
            sprintf(port_str, "%d", eth_socket_p->local_port);
            sprintf(remote_port_str, "%d", eth_socket_p->destport);
            sprintf(error_code_str, "%d", eth_socket_p->Error_Code);
            
            /* 普通连接 */
            sprintf(temp_buffer,
                    "<tr><td height=\"24\" class=\"ct\">%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "</tr>\r\n",
                    eth_socket_p->SOCKET_ID,
                    eth_socket_p->local_port == 0 ? "----" : port_str,        // 本站端口号
                    eth_socket_p->destip[0], eth_socket_p->destip[1],         // 目标 IP地址 
                    eth_socket_p->destip[2], eth_socket_p->destip[3],         // 目标 IP地址
                    eth_socket_p->destport == 0 ? "----" : remote_port_str,   // 目标端口号
                    eth_socket_p->Error_Code == 0 ? "----" : error_code_str,  // 错误代码
                    eth_socket_p->Pro_Type == PRO_UDPC_MC ? "UDP" : "TCP",    // 协议 tcp/Udp
                    open_str,                      // 开放方式 显示文本
                    state_str,                    // TCP状态 显示文本
                    eth_socket_p->EN ==0 ? "不可" : "可");                    // 强制切断状态
        }
        MITSU_HTTP_Emit( temp_buffer);
    }
    
    /* 表格底部 */
    MITSU_HTTP_Emit(
           "</tbody>\r\n"
           "</table>\r\n"
           "</td>\r\n"
           "</tr>\r\n"
           "</tbody>\r\n"
           "</table>\r\n");
    
    /* 表格已按阈值分批发出，冲刷尾部残留 */
    MITSU_HTTP_Emit("<br>\r\n");
    MITSU_HTTP_FlushTable();

    return table_buffer;
}


/*********************************************************************
 * @fn      FX_STATUS_SendWebPage
 *
 * @brief   流式发送通信状态页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */
void FX_STATUS_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock, char *url)
{
    char *temp_buffer = HtmlBuffer;
    char *monitor_status;
    char *conn_table;
    uint32_t offset;

    net_packet_t g_protocol_stats = {0};

    for(uint8_t i=0;i<4;i++)
    {
        if(eth_socket[i].Eth_Type ==  ETH_TYPE_TCP )
        {
            g_protocol_stats.tcp_rx_packets += eth_socket[i].net_rx_packets ;
            g_protocol_stats.tcp_tx_packets += eth_socket[i].net_tx_packets ;
        }
        else if( eth_socket[i].Eth_Type ==  ETH_TYPE_UDP )
        {
            g_protocol_stats.udp_rx_packets += eth_socket[i].net_rx_packets ;
            g_protocol_stats.udp_tx_packets += eth_socket[i].net_tx_packets ;
        }
    }

    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);

    /* 绑定表格流式发送的目标 socket */
    MITSU_HTTP_BeginTable(Dest_Sock);

    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);

    /* 第一次打包: HTML头部(约0.3KB) —— CSS 改为直发，见下 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "通信状态");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* CSS 样式表：组件本身 >1.2KB，而 HtmlBuffer 仅 HTML_LEN(768) 字节，
     * 用 sprintf 拷进 HtmlBuffer 会越界写穿 BSS(相邻全局变量) → 死机。
     * 无 %s 占位的静态组件一律绕过缓冲区直接发送。 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_CSS_NEW),
              strlen(HTML_GetComponent(HTML_COMP_CSS_NEW)));

    /* 第二次打包: body开始标签 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 导航栏：组件约 0.9KB，同样超过 HtmlBuffer 容量，改为直发 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_NAV_BAR_NEW),
              strlen(HTML_GetComponent(HTML_COMP_NAV_BAR_NEW)));

    /* 第四次打包: 内容区域开始和表单开始 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<div class=\"content\">\r\n<br>\r\n");
    /* 只有"监视执行中"才主动刷新；停止时不注入 meta refresh，改为被动 GET 刷新 */
    if (net_monitor_state == FX_MONITOR_RUNNING) {
        offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    }
    offset += HTML_PACK(temp_buffer, offset, "<form action=\"fx_status.html\" method=\"post\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<font style=\"font-size=16px\"><b>通信状态</b></font>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第五次打包: 适配器信息开始和状态行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td width=\"20\"></td><td width=\"50\"></td><td width=\"20\"></td><td width=\"140\"></td><td width=\"200\"></td><td width=\"170\"></td><td width=\"80\"></td><td width=\"80\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"5\">以太网适配器信息</td><td colspan=\"1\" align=\"right\">状态 :&nbsp;</td><td colspan=\"2\">%s</td></tr>\r\n", monitor_status);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第六次打包: IP地址行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"3\">IP地址</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td></tr>\r\n",
             Basic_CfgBuf.ip[0], Basic_CfgBuf.ip[1], Basic_CfgBuf.ip[2], Basic_CfgBuf.ip[3]);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第七次打包: 子网掩码行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"3\">子网掩码类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td></tr>\r\n",
             Basic_CfgBuf.mask[0], Basic_CfgBuf.mask[1], Basic_CfgBuf.mask[2], Basic_CfgBuf.mask[3]);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第八次打包: 网关和MAC地址行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"3\">默认路由器IP地址</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td><td colspan=\"3\"></td></tr>\r\n",
             Basic_CfgBuf.gateway[0], Basic_CfgBuf.gateway[1], Basic_CfgBuf.gateway[2], Basic_CfgBuf.gateway[3]);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"3\">以太网地址</td><td colspan=\"1\" class=\"inf\" align=\"right\">%02X%02X.%02X%02X.%02X%02X</td><td colspan=\"3\"></td></tr>\r\n",
             Basic_CfgBuf.mac[0], Basic_CfgBuf.mac[1], Basic_CfgBuf.mac[2], Basic_CfgBuf.mac[3], Basic_CfgBuf.mac[4], Basic_CfgBuf.mac[5]);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第九次打包: 表格结束和表单结束 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十次打包: 连接状态表格标题 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr><td colspan=\"5\" style=\"font-size:14px\">各连接状态</td><td colspan=\"3\"></td></tr></tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十一次打包: 生成并发送连接表格 */
    conn_table = FX_STATUS_GenerateConnectionTable();
    Data_Send(Dest_Sock, (uint8_t*)conn_table, strlen(conn_table));

    /* 第十二次打包: 协议统计表格 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr><td colspan=\"5\" style=\"font-size:14px\">各协议状态</td><td colspan=\"3\"></td></tr></tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"1\" cellspacing=\"1\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr><td><table border=\"1\" cellspacing=\"0\" bgcolor=\"#ffffff\" style=\"text-align:center;font-size:14px\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr bgcolor=\"#cccccc\"><td width=\"330\">&nbsp;</td><td width=\"230\">TCP数据包</td><td width=\"230\">UDP数据包</td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td height=\"24\" class=\"ct\">接收总数</td><td>%u</td><td>%u</td></tr>\r\n", g_protocol_stats.tcp_rx_packets, g_protocol_stats.udp_rx_packets);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td height=\"24\" class=\"ct\">发送总数</td><td>%u</td><td>%u</td></tr>\r\n", g_protocol_stats.tcp_tx_packets, g_protocol_stats.udp_tx_packets);
    offset += HTML_PACK(temp_buffer, offset, "</tbody></table></td></tr></tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 打包: 页面尾部 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</div>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</div>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</body>\r\n</html>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 页脚(含 SX 自检脚本)直发：组件 >1.2KB，远超 HtmlBuffer 容量。
     * 原来与收尾标签挤在同一包(约 1.27KB) -> 越界写穿 http_request / g_access_fifo
     * (其 records 指针被 HTML 文本覆盖) -> 下一次 FX_ACCLOG_AddRecord() 取 conn_id
     * 即 HardFault(mcause=4 未对齐取数, mtval 为文本字节)。 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_FOOTER_NEW),
              strlen(HTML_GetComponent(HTML_COMP_FOOTER_NEW)));

    HTTPS_DEBUG("通信状态页面流式发送完成\r\n");
}

  