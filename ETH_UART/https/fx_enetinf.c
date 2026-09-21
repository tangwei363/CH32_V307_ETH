/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_enetinf.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP信息页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_enetinf.h"
#include "fifo_queue.h"
#include "HTTPS.h"
#include "html_components.h"
#include "index.h"
#include "ethernet_app.h"
#include "bsp_uart.h"
/* 全局变量定义 */
static fx_enetinf_adapter_t g_adapter_info;
 
fx_enetinf_error_log_t g_error_logs[FX_ENETINF_MAX_ERRORS];
fifo_queue_t g_error_fifo = {0};

/* 静态函数声明 */
static char* FX_ENETINF_GenerateErrorTable(void);

/*********************************************************************
 * @fn      FX_ENETINF_Init
 *
 * @brief   初始化FX3U-ENET-ADP信息模块
 *
 * @param   无
 *
 * @return  无
 */
void FX_ENETINF_Init(void)
{
    /* 初始化适配器信息 - 示例值 */
    g_adapter_info.version = 0x0122;  /* 版本1.22 */
    /* 设置LED状态 */
    g_adapter_info.led_power = FX_ENET_LED_GREEN;  /* POWER灯常亮 */
    g_adapter_info.led_100m = FX_ENET_LED_GREEN;   /* 100M灯常亮 */
    g_adapter_info.led_err = FX_ENET_LED_OFF;      /* ERR灯关闭 */
    g_adapter_info.led_open = FX_ENET_LED_GREEN;   /* OPEN灯常亮 */
    
    /* 初始化错误履历队列 */
    FIFO_Init(&g_error_fifo, g_error_logs, sizeof(fx_enetinf_error_log_t), log_data.error_log_target_cnt);

    printf("FX3U-ENET-ADP信息模块初始化完成\r\n");
}
 
/*********************************************************************
 * @fn      FX_ENETINF_GetRecord
 *
 * @brief   获取指定错误履历
 *
 * @param   index - 错误履历索引(0-15)
 *          record - 错误履历结构体指针
 *
 * @return  无
 */
void FX_ENETINF_GetRecord(uint8_t index, fx_enetinf_error_log_t *record)
{
    if (index < FX_ENETINF_MAX_ERRORS && record != NULL) {
        memcpy(record, &g_error_logs[index], sizeof(fx_enetinf_error_log_t));   
    }
}

/*********************************************************************
 * @fn      FX_ENETINFACCLOG_AddRecord
 *
 * @brief   添加错误履历到队列
 *
 * @param   conn_id;         连接号 
 * @param   protocol;        协议类型 
 * @param   open_type;       开放方式 
 * @param   local_port;      本站端口号 
 * @param   error_code;      错误代码 
 * @param   remote_ip[4];    通信对象IP地址 
 * @param   remote_port;     通信对象端口号 
 * @param   cmd_code;        指令代码 
 * @return  无
 */
void FX_ENETINF_ACCLOG_AddRecord(uint16_t conn_id, uint8_t protocol, uint8_t open_type, 
                                uint16_t local_port, uint16_t error_code, uint8_t remote_ip[4],
                                uint16_t remote_port, uint8_t cmd_code)
{
    if ( remote_ip == NULL) {
        return;
    }
    fx_enetinf_error_log_t record;
    // 更新RTC实时时钟       
    RTC_Get();
    record.log_time.year = calendar.w_year;
    record.log_time.month = calendar.w_month;
    record.log_time.day = calendar.w_date;
    record.log_time.hour = calendar.hour;
    record.log_time.minute = calendar.min;
    record.log_time.second = calendar.sec;
    record.conn_id = conn_id;  // 连接号 
    record.protocol = protocol; // 协议类型 TCP / UDP
    record.open_type = 0x02FF & open_type; // 开放方式 
    record.local_port = local_port; // 本站端口号 
    record.error_code = error_code; // 错误代码 
    record.remote_port = remote_port; // 通信对象端口号 
    record.cmd_code = cmd_code; // 指令代码 
    memcpy(record.remote_ip, remote_ip, 4);
    
    /* 添加新记录到队列 */
    FIFO_AddRecord(&g_error_fifo, &record);
}

/*********************************************************************
 * @fn      FX_ENETINF_GetErrorLog
 *
 * @brief   获取指定错误履历
 *
 * @param   index - 错误履历索引(0-15)
 *          log - 错误履历结构体指针
 *
 * @return  无
 */
void FX_ENETINF_GetErrorLog(uint8_t index, fx_enetinf_error_log_t *log)
{
    if (index < FX_ENETINF_MAX_ERRORS && log != NULL) {
        if (FIFO_GetCount(&g_error_fifo) > 0) {
            /* 重置读取位置到最新记录 */
            FIFO_ResetReadPos(&g_error_fifo);
            
            /* 遍历队列，找到指定索引的记录 */
            fx_enetinf_error_log_t temp_log;
            uint8_t i = 0;
            
            while (i <= index && FIFO_GetNextRecord(&g_error_fifo, &temp_log)) {
                if (i == index) {
                    memcpy(log, &temp_log, sizeof(fx_enetinf_error_log_t));
                    break;
                }
                i++;
            }
        } else {
            /* 队列为空，返回空记录 */
            memset(log, 0, sizeof(fx_enetinf_error_log_t));
        }
    }
}

/*********************************************************************
 * @fn      FX_ErrorLogs_GetNextRecord
 *
 * @brief   按顺序获取下一条错误履历（从头到尾）
 *
 * @param   record - 错误履历结构体指针
 *
 * @return  1 - 成功获取记录，0 - 没有更多记录
 */
uint8_t FX_ErrorLogs_GetNextRecord(fx_enetinf_error_log_t *record)
{
    return FIFO_GetNextRecord(&g_error_fifo, record);
}
/*********************************************************************
 * @fn      FX_ErrorLogs_ResetReadPos
 *
 * @brief   重置读取位置到最新记录
 *
 * @param   无
 *
 * @return  无
 */
void FX_ErrorLogs_ResetReadPos(void)
{
    FIFO_ResetReadPos(&g_error_fifo);
}
/*********************************************************************
 * @fn      FX_ENETINF_ClearErrorLogs
 *
 * @brief   清空错误履历队列
 *
 * @param   无
 *
 * @return  无
 */
void FX_ENETINF_ClearErrorLogs(void)
{
    FIFO_Clear(&g_error_fifo);
}


/*********************************************************************
 * @fn      FX_ErrorLogs_SendAccess_PLC
 *
 * @brief   分批发送访问履历PLC寄存器中 (避免内存越界)
 *
 *
 * @return  无
 */
void FX_ErrorLogs_SendAccess_PLC(void)
{
    /* 设置时间设置结果的存储目标类型 */
    ETHERNET_DEBUG(" 同步 PLC 设置时间设置结果的存储目标类型 \r\n ");
    // 软元件范围（800:0x0320，高低互换）
    net_mc_meta.start_device = log_data.error_log_target_cnt;  
    //（记录件数）(1~ 16)
    net_mc_meta.device_count = sizeof(fx_enetinf_error_log_t);
    fx_enetinf_error_log_t record;
    // 遍历队列中的记录
    while (FX_ErrorLogs_GetNextRecord(&record)) {
        // 处理记录
        // 时间设置结果存储目标寄存器类型（01:D寄存器，02:R寄存器）
        if (log_data.error_log_target_reg == 0x02) {
            MELSEC_FX_Build_E16_write_R_Cmd(0xFF , 0xFF, net_mc_meta.start_device, 
                        (uint16_t*)&record, net_mc_meta.device_count);
        } else if (log_data.error_log_target_reg == 0x01){ 
            MELSEC_FX_BuildE10WriteParamCmd(0xFF , 0xFF, net_mc_meta.start_device,
                        (uint16_t*)&record, net_mc_meta.device_count);
        }
        net_mc_meta.start_device += net_mc_meta.device_count;
        Delay_Ms(100);//等待发送完成
    }
    // 重置读取位置到最新记录
    FX_ErrorLogs_ResetReadPos();

}
 
/*********************************************************************
 * @fn      FX_ENETINF_GenerateErrorTable
 *
 * @brief   生成错误履历表格HTML
 *
 * @param   无
 *
 * @return  表格HTML字符串
 */
static char* FX_ENETINF_GenerateErrorTable(void)
{
    char *table_buffer = MITSU_HTTP_GetTableBuffer();
    char *temp_buffer = HtmlBuffer;
    uint8_t i;
    const char* proto_str[] = {"TCP", "UDP"};
    const char* open_str[] = {"MELSOFT", "MC", "数据监视", "未知"};
    
    MITSU_HTTP_ClearTableBuffer();
    
    /* 表格头部 */
    MITSU_HTTP_Emit(
           "<table border=\"1\" cellspacing=\"1\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td>\r\n"
           "<table border=\"1\" cellspacing=\"0\" style=\"text-align:center;font-size:14px\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td width=\"50\">&nbsp;</td>\r\n"
           "<td width=\"110\">连接号</td>\r\n"
           "<td width=\"70\">协议</td>\r\n"
           "<td width=\"90\">开放方式</td>\r\n"
           "<td width=\"100\">本站端口号</td>\r\n"
           "<td width=\"50\">错误<br>代码</td>\r\n"
           "<td width=\"90\">通信对象<br>IP地址</td>\r\n"
           "<td width=\"100\">通信对象<br>端口号</td>\r\n"
           "<td width=\"60\">指令<br>代码</td>\r\n"
           "<td width=\"110\">年月日</td>\r\n"
           "<td width=\"70\">时间</td>\r\n"
           "</tr>\r\n");
    
    /* 生成错误履历行 */
    if (FIFO_GetCount(&g_error_fifo) > 0) {
        /* 重置读取位置到最新记录 */
        FIFO_ResetReadPos(&g_error_fifo);
        
        /* 显示最新记录 */
        fx_enetinf_error_log_t latest_log;
        if (FIFO_GetNextRecord(&g_error_fifo, &latest_log)) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>最新</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "</tr>\r\n",
                    latest_log.conn_id,
                    proto_str[latest_log.protocol],
                    open_str[latest_log.open_type],
                    latest_log.local_port,
                    latest_log.error_code,
                    latest_log.remote_ip[0], latest_log.remote_ip[1],
                    latest_log.remote_ip[2], latest_log.remote_ip[3],
                    latest_log.remote_port,
                    latest_log.cmd_code,
                    latest_log.log_time.year, latest_log.log_time.month, latest_log.log_time.day,
                    latest_log.log_time.hour, latest_log.log_time.minute, latest_log.log_time.second);
            MITSU_HTTP_Emit( temp_buffer);
        }
        
        /* 显示剩余记录 */
        fx_enetinf_error_log_t log;
        uint8_t log_index = 2;
        while (FIFO_GetNextRecord(&g_error_fifo, &log) && log_index <= FX_ENETINF_MAX_ERRORS) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "</tr>\r\n",
                    log_index,
                    log.conn_id,
                    proto_str[log.protocol],
                    open_str[log.open_type],
                    log.local_port,
                    log.error_code,
                    log.remote_ip[0], log.remote_ip[1],
                    log.remote_ip[2], log.remote_ip[3],
                    log.remote_port,
                    log.cmd_code,
                    log.log_time.year, log.log_time.month, log.log_time.day,
                    log.log_time.hour, log.log_time.minute, log.log_time.second);
            MITSU_HTTP_Emit( temp_buffer);
            log_index++;
        }
        
        /* 显示空行 */
        for (; log_index <= FX_ENETINF_MAX_ERRORS; log_index++) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "</tr>\r\n",
                    log_index);
            MITSU_HTTP_Emit( temp_buffer);
        }
    } else {
        /* 所有行都是空行 */
        for (i = 0; i < FX_ENETINF_MAX_ERRORS; i++) {
            if (i == 0) {
                /* 最新行 - 无错误 */
                MITSU_HTTP_Emit(
                       "<tr>\r\n"
                       "<td>最新</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "</tr>\r\n");
            } else {
                /* 空行 */
                sprintf(temp_buffer,
                        "<tr>\r\n"
                        "<td>%d</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "</tr>\r\n",
                        i + 1);
                MITSU_HTTP_Emit( temp_buffer);
            }
        }
    }
    
    /* 表格尾部 */
    MITSU_HTTP_Emit(
           "</tbody>\r\n"
           "</table>\r\n"
           "</td>\r\n"
           "</tr>\r\n"
           "</tbody>\r\n"
           "</table>\r\n");
    
    /* 表格已按阈值分批发出，冲刷尾部残留 */
    MITSU_HTTP_FlushTable();

    return table_buffer;
}



/*********************************************************************
 * @fn      FX_ENETINF_SendWebPage
 *
 * @brief   流式发送FX3U-ENET-ADP信息页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */
void FX_ENETINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url)
{
    char *temp_buffer = HtmlBuffer;
    char *error_table;
    const char *monitor_status;
 
    uint32_t offset;
    /* 更新监视状态 */
    FX_ENETINF_UpdateMonitor();
 
    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);
    /* 绑定表格流式发送的目标 socket */
    MITSU_HTTP_BeginTable(Dest_Sock);
    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);
    /* 第一次打包: HTML头部和CSS样式 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, HTML_GetComponent(HTML_COMP_HEADER), "FX3U-ENET-ADP信息");
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_CSS_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第二次打包: body开始到导航栏结束 (使用共享组件) */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_NAV_BAR_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第四次打包: 表单开始和适配器信息标题 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    offset += sprintf(temp_buffer + offset, "<div style=\"text-align:center;\">\r\n");
    offset += sprintf(temp_buffer + offset, "<form action=\"fx_enetinf.html\" method=\"post\" style=\"display:inline-block; text-align:left;\">\r\n");
    offset += sprintf(temp_buffer + offset, "<font style=\"font-size=16px\"><b>FX3U-ENET-ADP信息</b></font>\r\n");
    offset += sprintf(temp_buffer + offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n<tbody>\r\n<tr>\r\n<td width=\"20\"></td>\r\n<td width=\"50\"></td>\r\n<td width=\"10\"></td>\r\n<td width=\"50\"></td>\r\n<td width=\"10\"></td>\r\n<td width=\"90\"></td>\r\n<td width=\"200\"></td>\r\n<td width=\"170\"></td>\r\n<td width=\"80\"></td>\r\n<td width=\"80\"></td>\r\n</tr>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第五次打包: 状态行和版本行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"7\">以太网适配器信息</td>\r\n<td colspan=\"1\" align=\"right\">状态 :&nbsp;</td>\r\n<td colspan=\"2\">%s</td>\r\n</tr>\r\n", monitor_status);
    offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">FX3U-ENET-ADP 版本</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%02d</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td>\r\n</tr>\r\n", (g_adapter_info.version >> 8) & 0xFF, g_adapter_info.version & 0xFF);
    offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"8\"></td>\r\n<td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td>\r\n</tr>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: 以太网适配器设置行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"10\">以太网适配器设置</td>\r\n</tr>\r\n<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">IP地址</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n", Basic_CfgBuf.ip[0], Basic_CfgBuf.ip[1], Basic_CfgBuf.ip[2], Basic_CfgBuf.ip[3]);
    offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">子网掩码类型</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n", Basic_CfgBuf.mask[0], Basic_CfgBuf.mask[1], Basic_CfgBuf.mask[2], Basic_CfgBuf.mask[3]);

    // /* 第八次打包: LED状态行 */
    // offset = 0;
    // offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">POWER</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", HTML_GetLEDClass_Old(g_adapter_info.led_power == FX_ENET_LED_GREEN ? 1 : 0));
    // offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">100M</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", HTML_GetLEDClass_Old(g_adapter_info.led_100m == FX_ENET_LED_GREEN ? 1 : 0));
    // offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">ERR.</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", HTML_GetLEDClass_Old(g_adapter_info.led_err == FX_ENET_LED_ON ? 1 : 0));
    // offset += sprintf(temp_buffer + offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">OPEN</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", HTML_GetLEDClass_Old(g_adapter_info.led_open == FX_ENET_LED_GREEN ? 1 : 0));
    // offset += sprintf(temp_buffer + offset, "</tbody>\r\n</table>\r\n<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n</form>\r\n");
    // Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第九次打包: 错误履历表格标题 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\">\r\n<tbody>\r\n<tr>\r\n<td colspan=\"5\" style=\"font-size:14px\">错误履历</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n</tbody>\r\n</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十次打包: 错误履历表格 */
    error_table = FX_ENETINF_GenerateErrorTable();
    Data_Send(Dest_Sock, (uint8_t*)error_table, strlen(error_table));

 
    /* 打包: 页面尾部 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "</div>\r\n");
    offset += sprintf(temp_buffer + offset, "</div>\r\n");
    offset += sprintf(temp_buffer + offset, "</body>\r\n</html>\r\n");
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_FOOTER_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    printf("FX3U-ENET-ADP信息页面流式发送完成\r\n");
}



