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
#include "melsec_fx_tables.h"   /* MC_FX_M：M 软元件编码 */
#include "melsec_fx_core.h"     /* MELSEC_FX_BuildE00ReadCmd：字单位成批读出 */
#include "melsec_fx_net.h"      /* net_mc_meta：MC 协议请求上下文 */
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

/* ══════════════════════════════════════════════════════════════════
 * PLC 状态位(M8000~M8015 一个字)驱动 LED
 *
 * 位序（三菱约定，与 ethernet_app.c 的 wizchip_Analysis_M_info 一致）：
 *   状态字的 bit j = M(8000 + j)
 *     M8000 -> RUN   灯：1=点亮(绿)，0=熄灭
 *     M8004 -> ERROR 灯：1=点亮(红，错误发生)
 *     M8005 -> BATT  灯：1=点亮(红，电池电压低)
 *     POWER 灯固定常亮(绿)，不依赖 PLC 状态
 *
 * 取数方式：M 是位软元件，按"字单位"读 1 点 = 16 位(正好覆盖 M8000~M8015)，
 *           与软元件监视页(fx_devmon)读 M 的方式完全一致。
 * 刷新时机：页面渲染时(监视执行中)发起一次读；UART 回帧到达后由
 *           HTTPS.c 的 Web_Usart_Handler(HTML_PAGE_PLCINF 分支)调用
 *           FX_PLCINF_OnStatusReply() 更新 LED，浏览器每 5s 元刷新即看到新状态。
 * ══════════════════════════════════════════════════════════════════ */
#define FX_PLCINF_STATUS_ADDR     8000u   /* M8000 */
#define FX_PLCINF_STATUS_POINTS   1u      /* 1 个字 = M8000~M8015 */
#define FX_PLCINF_BIT_RUN         0u      /* M8000 */
#define FX_PLCINF_BIT_ERROR       4u      /* M8004 */
#define FX_PLCINF_BIT_BATT        5u      /* M8005 */

static uint16_t g_plc_status       = 0;   /* 最近一次读到的状态字 */
static uint8_t  g_plc_status_valid = 0;   /* 是否成功读到过 */
static uint8_t  g_plc_req_busy     = 0;   /* 有一次读在途：避免请求堆积 */

/*********************************************************************
 * @fn      FX_PLCINF_RequestStatus
 *
 * @brief   发起一次 M8000~M8015 读取（仅"监视执行中"；每次页面渲染调一次）
 *
 * @param   Sour_Sock - 请求来源 socket
 * @param   Dest_Sock - 目的 socket（回帧时按此 socket 的页面路由回来）
 *
 * @return  none
 */
void FX_PLCINF_RequestStatus(uint8_t Sour_Sock, uint8_t Dest_Sock)
{
    if (net_monitor_state != FX_MONITOR_RUNNING) {
        g_plc_req_busy = 0;                    /* 监视停止：解除在途标志 */
        return;
    }
    if (g_plc_req_busy) {
        return;                                /* 上一帧还没回来，先不重复发 */
    }

    net_mc_meta.device_name  = MC_FX_M;
    net_mc_meta.start_device = FX_PLCINF_STATUS_ADDR;
    net_mc_meta.device_count = FX_PLCINF_STATUS_POINTS;

    g_plc_req_busy = 1;
    MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                              FX_PLCINF_STATUS_ADDR, FX_PLCINF_STATUS_POINTS);
    printf("PLC信息页: 读取 M8000~M8015(1 字)\r\n");
}

/*********************************************************************
 * @fn      FX_PLCINF_OnStatusReply
 *
 * @brief   处理 M8000~M8015 回帧，刷新 LED（POWER 常亮，RUN/BATT/ERROR 由状态位驱动）
 *
 * @param   data - 已由 ASCII 十六进制解码为二进制的数据区(低位字在前)
 *          len  - 数据字节数
 *
 * @return  none
 */
void FX_PLCINF_OnStatusReply(const uint8_t *data, uint16_t len)
{
    uint16_t word;

    g_plc_req_busy = 0;                        /* 无论成败都解除在途标志 */
    if (data == NULL || len < 2u) {
        printf("PLC信息页: 状态回帧过短(len=%u)\r\n", len);
        return;
    }

    word = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    g_plc_status       = word;
    g_plc_status_valid = 1;

    /* POWER 固定常亮；RUN 绿；ERROR / BATT 红 */
    g_plc_info.led_power = FX_PLC_LED_GREEN;
    g_plc_info.led_run   = (word & (1u << FX_PLCINF_BIT_RUN))   ? FX_PLC_LED_GREEN : FX_PLC_LED_OFF;
    g_plc_info.led_error = (word & (1u << FX_PLCINF_BIT_ERROR)) ? FX_PLC_LED_RED   : FX_PLC_LED_OFF;
    g_plc_info.led_batt  = (word & (1u << FX_PLCINF_BIT_BATT))  ? FX_PLC_LED_RED   : FX_PLC_LED_OFF;

    printf("PLC信息页: 状态字=0x%04X  RUN=%d ERROR=%d BATT=%d\r\n", (unsigned)word,
           (g_plc_info.led_run   != FX_PLC_LED_OFF),
           (g_plc_info.led_error != FX_PLC_LED_OFF),
           (g_plc_info.led_batt  != FX_PLC_LED_OFF));
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

    /* ★ 每次渲染前：
     *   ① RTC_Get() 取板载 RTC 的当前时间(年月日/时间实时刷新；秒中断里也在更新，
     *      这里再取一次保证渲染瞬间是新鲜的)；
     *   ② 监视执行中则发起一次 M8000~M8015 读取，回帧异步刷新 LED
     *      (本页 LED 用"最近一次读到的状态"渲染，下一轮刷新即体现新值)。 */
    RTC_Get();
    FX_PLCINF_RequestStatus(Sour_Sock, Dest_Sock);

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
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "PLC信息");
    /* CSS 样式表直发：组件 >1.2KB，HtmlBuffer 装不下(越界会写穿 BSS 导致死机) */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_CSS_NEW),
              strlen(HTML_GetComponent(HTML_COMP_CSS_NEW)));
    /* 第二次打包: body开始到导航栏结束 (使用共享组件) */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 导航栏直发：组件约 0.9KB，同样超过 HtmlBuffer 容量 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_NAV_BAR_NEW),
              strlen(HTML_GetComponent(HTML_COMP_NAV_BAR_NEW)));
    /* 第四次打包: 内容区域开始和表单开始 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<div class=\"content\">\r\n");
    /* 只有"监视执行中"才主动刷新；停止时不注入 meta refresh，改为被动 GET 刷新 */
    if (net_monitor_state == FX_MONITOR_RUNNING) {
        offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    }
    offset += HTML_PACK(temp_buffer, offset, "<form action=\"fx_plcinf.html\" method=\"post\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<div style=\"text-align:center\"><font style=\"font-size=16px\"><b>PLC信息</b></font></div>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第五次打包: PLC信息表格开始和标题行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td width=\"20\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"90\"></td><td width=\"200\"></td><td width=\"170\"></td><td width=\"80\"></td><td width=\"80\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"7\">PLC信息</td><td colspan=\"1\" align=\"right\">状态 :&nbsp;</td><td colspan=\"2\">%s</td></tr>\r\n", monitor_status);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: CPU类型和CPU版本行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", cpu_type_str);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU版本</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%02d</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", (g_plc_info.cpu_version >> 8) & 0xFF, g_plc_info.cpu_version & 0xFF);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第七次打包: 存储器类型和电池模式行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">存储器类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", mem_type_str);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">无电池模式</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", battery_mode_str);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第八次打包: 日期和时间行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">年月日</td><td colspan=\"1\" class=\"inf\" align=\"right\">%04d-%02d-%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.w_year, calendar.w_month, calendar.w_date);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">时间</td><td colspan=\"1\" class=\"inf\" align=\"right\">%02d:%02d:%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.hour, calendar.min, calendar.sec);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    // /* 第九次打包: LED状态行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"10\">&nbsp;</td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"10\">LED状态</td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">POWER</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_power));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">RUN</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_run));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">BATT</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_batt));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">ERROR</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_error));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第十次打包: 表格结束和隐藏字段 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十一次打包: 错误信息表格标题 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" bgcolor=\"#ffffff\" style=\"margin:0 auto;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr><td bgcolor=\"#cccccc\">错误信息</td></tr></tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十二次打包: 生成并发送错误表格 */
    error_table = FX_PLCINF_GenerateErrorTable();
    Data_Send(Dest_Sock, (uint8_t*)error_table, strlen(error_table));

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

    printf("PLC信息页面流式发送完成\r\n");
}
