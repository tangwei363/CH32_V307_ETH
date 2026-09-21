/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_plcinf.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP PLC信息页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_plcinf.h"
#include "HTTPS.h"
#include "html_components.h"
#include "index.h"

#include "bsp_rtc.h"
#include "ethernet_app.h"
/* 全局变量定义 */
#define FX_PLCINF_TABLE_ROWS  11
static fx_plcinf_info_t g_plc_info;
static fx_plcinf_error_t g_errors[FX_PLCINF_MAX_ERRORS];
 
/* 静态函数声明 */
static char* FX_PLCINF_GetCPUTypeString(fx_plcinf_cpu_type_t cpu_type);
static char* FX_PLCINF_GetMemTypeString(fx_plcinf_mem_type_t mem_type);
static char* FX_PLCINF_GenerateErrorTable(void);

/*********************************************************************
 * @fn      FX_PLCINF_Init
 *
 * @brief   初始化PLC信息模块
 *
 * @param   无
 *
 * @return  无
 */
void FX_PLCINF_Init(void)
{
    uint8_t i;
    
    /* 初始化PLC信息 - 示例值 */
    g_plc_info.cpu_type = FX_PLC_CPU_FX3U;
    g_plc_info.cpu_version = 0x0315;  /* 版本3.15 */
    g_plc_info.mem_type = FX_PLC_MEM_RAM;
    g_plc_info.battery_mode = 0;
    
    /* 设置LED状态 */
    g_plc_info.led_power = FX_PLC_LED_GREEN;   /* POWER灯常亮(绿色) */
    g_plc_info.led_run = FX_PLC_LED_OFF;       /* RUN灯关闭 */
    g_plc_info.led_batt = FX_PLC_LED_OFF;      /* BATT灯关闭 */
    g_plc_info.led_error = FX_PLC_LED_OFF;     /* ERROR灯关闭 */
    
    /* 初始化错误信息 */
    memset(g_errors, 0, sizeof(g_errors));
    for (i = 0; i < FX_PLCINF_MAX_ERRORS; i++) {
        g_errors[i].error_no = i + 1;
        g_errors[i].error_step = 0;
        g_errors[i].error_msg[0] = '\0';
    }
    
    /* 初始化HTML缓冲区 */
    
    printf("PLC信息模块初始化完成\r\n");
}

/*********************************************************************
 * @fn      FX_PLCINF_GetCPUTypeString
 *
 * @brief   获取CPU类型字符串
 *
 * @param   cpu_type - CPU类型
 *
 * @return  CPU类型字符串
 */
static char* FX_PLCINF_GetCPUTypeString(fx_plcinf_cpu_type_t cpu_type)
{
    switch (cpu_type) {
        case FX_PLC_CPU_FX3U:
            return "FX3U/FX3UC";
        case FX_PLC_CPU_FX3UC:
            return "FX3U/FX3UC";
        default:
            return "UNKNOWN";
    }
}

/*********************************************************************
 * @fn      FX_PLCINF_GetMemTypeString
 *
 * @brief   获取存储器类型字符串
 *
 * @param   mem_type - 存储器类型
 *
 * @return  存储器类型字符串
 */
static char* FX_PLCINF_GetMemTypeString(fx_plcinf_mem_type_t mem_type)
{
    switch (mem_type) {
        case FX_PLC_MEM_RAM:
            return "RAM";
        case FX_PLC_MEM_EEPROM:
            return "EEPROM";
        case FX_PLC_MEM_FLASH:
            return "FLASH";
        default:
            return "UNKNOWN";
    }
}

/*********************************************************************
 * @fn      FX_PLCINF_GenerateErrorTable
 *
 * @brief   生成错误信息表格HTML
 *
 * @param   无
 *
 * @return  表格HTML字符串
 */
static char* FX_PLCINF_GenerateErrorTable(void)
{
    char *table_buffer = MITSU_HTTP_GetTableBuffer();
    char *temp_buffer = HtmlBuffer;
    uint8_t i;
    uint8_t has_error = 0;
    
    MITSU_HTTP_ClearTableBuffer();
    
    /* 表格头部 */
    MITSU_HTTP_Emit(
           "<table border=\"1\" cellspacing=\"1\" style=\"margin:0 auto;\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td>\r\n"
           "<table border=\"1\" cellspacing=\"0\" bgcolor=\"#ffffff\" style=\"table-layout:fixed;text-align:center;font-size:14px\">\r\n"
           "<tbody>\r\n"
           "<tr bgcolor=\"#cccccc\">\r\n"
           "<td width=\"90\">No.</td>\r\n"
           "<td width=\"180\">错误步</td>\r\n"
           "<td width=\"630\">当前错误</td>\r\n"
           "</tr>\r\n");
    
    /* 生成错误行 */
    for (i = 0; i < FX_PLCINF_MAX_ERRORS; i++) {
        if (strlen(g_errors[i].error_msg) > 0) {
            has_error = 1;
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%u</td>\r\n"
                    "<td>%s</td>\r\n"
                    "</tr>\r\n",
                    g_errors[i].error_no,
                    g_errors[i].error_step,
                    g_errors[i].error_msg);
            MITSU_HTTP_Emit( temp_buffer);
        }
    }
    
    /* 如果没有错误,显示无错误 */
    if (!has_error) {
        MITSU_HTTP_Emit(
               "<tr>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>无错误</td>\r\n"
               "</tr>\r\n");
    }
    
    /* 填充剩余空行 */
    for (i = 1; i < FX_PLCINF_TABLE_ROWS; i++) {
        MITSU_HTTP_Emit(
               "<tr>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "</tr>\r\n");
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
 * @fn      FX_PLCINF_GetError
 *
 * @brief   获取指定错误信息
 *
 * @param   index - 错误索引(0-9)
 *          error - 错误信息结构体指针
 *
 * @return  无
 */
void FX_PLCINF_GetError(uint8_t index, fx_plcinf_error_t *error)
{
    if (index < FX_PLCINF_MAX_ERRORS && error != NULL) {
        memcpy(error, &g_errors[index], sizeof(fx_plcinf_error_t));
    }
}

 /* LED 状态 -> CSS 类名（原厂配色: 绿=ledg, 红=ledr, 灭=off） */
static const char* FX_PLCINF_LEDClass(fx_plcinf_led_state_t st)
{
    if (st == FX_PLC_LED_GREEN) { return "ledg"; }
    if (st == FX_PLC_LED_RED)   { return "ledr"; }
    return "off";
}

 
/*********************************************************************
 * @fn      FX_PLCINF_SendWebPage
 *
 * @brief   流式发送PLC信息页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */

void FX_PLCINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url)
{
    char *temp_buffer = HtmlBuffer;
    char *error_table;
    const char *monitor_status;
    char *cpu_type_str;
    char *mem_type_str;
    char *battery_mode_str;
 
    uint32_t offset;
    /* 获取CPU类型字符串 */
    cpu_type_str = FX_PLCINF_GetCPUTypeString(g_plc_info.cpu_type);
    /* 获取存储器类型字符串 */
    mem_type_str = FX_PLCINF_GetMemTypeString(g_plc_info.mem_type);
    /* 获取电池模式字符串 */
    battery_mode_str = g_plc_info.battery_mode ? "有效" : "无效";
 
    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);
    /* 绑定表格流式发送的目标 socket */
    MITSU_HTTP_BeginTable(Dest_Sock);
    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);
     /* 第一次打包: HTML头部到</head> (使用共享组件) */
    offset = 0;
    offset += sprintf(temp_buffer + offset, HTML_GetComponent(HTML_COMP_HEADER), "PLC信息");
    /* CSS 样式表直发：组件 >1.2KB，HtmlBuffer 装不下(越界会写穿 BSS 导致死机) */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_CSS_NEW),
              strlen(HTML_GetComponent(HTML_COMP_CSS_NEW)));
    /* 第二次打包: body开始到导航栏结束 (使用共享组件) */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 导航栏直发：组件约 0.9KB，同样超过 HtmlBuffer 容量 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_NAV_BAR_NEW),
              strlen(HTML_GetComponent(HTML_COMP_NAV_BAR_NEW)));
    /* 第四次打包: 内容区域开始和表单开始 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<div class=\"content\">\r\n");
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    offset += sprintf(temp_buffer + offset, "<form action=\"fx_plcinf.html\" method=\"post\">\r\n");
    offset += sprintf(temp_buffer + offset, "<div style=\"text-align:center\"><font style=\"font-size=16px\"><b>PLC信息</b></font></div>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第五次打包: PLC信息表格开始和标题行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n");
    offset += sprintf(temp_buffer + offset, "<tbody>\r\n");
    offset += sprintf(temp_buffer + offset, "<tr><td width=\"20\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"90\"></td><td width=\"200\"></td><td width=\"170\"></td><td width=\"80\"></td><td width=\"80\"></td></tr>\r\n");
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"7\">PLC信息</td><td colspan=\"1\" align=\"right\">状态 :&nbsp;</td><td colspan=\"2\">%s</td></tr>\r\n", monitor_status);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: CPU类型和CPU版本行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", cpu_type_str);
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU版本</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%02d</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", (g_plc_info.cpu_version >> 8) & 0xFF, g_plc_info.cpu_version & 0xFF);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第七次打包: 存储器类型和电池模式行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">存储器类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", mem_type_str);
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">无电池模式</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", battery_mode_str);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第八次打包: 日期和时间行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">年月日</td><td colspan=\"1\" class=\"inf\" align=\"right\">%04d-%02d-%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.w_year, calendar.w_month, calendar.w_date);
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">时间</td><td colspan=\"1\" class=\"inf\" align=\"right\">%02d:%02d:%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.hour, calendar.min, calendar.sec);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    // /* 第九次打包: LED状态行 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"10\">&nbsp;</td></tr>\r\n");
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"10\">LED状态</td></tr>\r\n");
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">POWER</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_power));
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">RUN</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_run));
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">BATT</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_batt));
    offset += sprintf(temp_buffer + offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">ERROR</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_error));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第十次打包: 表格结束和隐藏字段 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "</tbody>\r\n");
    offset += sprintf(temp_buffer + offset, "</table>\r\n");
    offset += sprintf(temp_buffer + offset, "<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n");
    offset += sprintf(temp_buffer + offset, "</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十一次打包: 错误信息表格标题 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" bgcolor=\"#ffffff\" style=\"margin:0 auto;\">\r\n");
    offset += sprintf(temp_buffer + offset, "<tbody><tr><td bgcolor=\"#cccccc\">错误信息</td></tr></tbody>\r\n");
    offset += sprintf(temp_buffer + offset, "</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十二次打包: 生成并发送错误表格 */
    error_table = FX_PLCINF_GenerateErrorTable();
    Data_Send(Dest_Sock, (uint8_t*)error_table, strlen(error_table));

    /* 打包: 页面尾部 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "</div>\r\n");
    offset += sprintf(temp_buffer + offset, "</div>\r\n");
    offset += sprintf(temp_buffer + offset, "</body>\r\n</html>\r\n");
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_FOOTER_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    printf("PLC信息页面流式发送完成\r\n");
}
