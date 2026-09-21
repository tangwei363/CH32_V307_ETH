/********************************** (C) COPYRIGHT *******************************
 * File Name          : index.c
 * Author             : AI Assistant
 * Version            : V2.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP HTTP服务器实现 - 调用HTTPS.c现有函数
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "index.h"

/* 全局变量定义 */
/* 注意: HtmlBuffer和http_request在HTTPS.c中定义，这里只声明共用缓冲区 */
char mitsu_table_buffer[MITSU_TABLE_BUFFER_SIZE];

/* 表格流式发送状态：已累积长度与目标 socket（0xFF 表示未绑定） */
static uint32_t mitsu_table_len  = 0;
static uint8_t  mitsu_table_sock = 0xFF;

/* 累积阈值：留 32 字节余量，确保追加时不会越界 */
#define MITSU_TABLE_EMIT_LIMIT    (MITSU_TABLE_BUFFER_SIZE - 32)

/* 主页内容：品牌 / 型号 / 用途，三段式排版（样式见共享 CSS 的 .hero* 规则）
 * 说明：标题层级用 h1，桌面 40px、窄屏 26px（见 CSS 媒体查询），与页脚风格统一。
 *       段落间距由 CSS 的 .hero* margin 控制（想再大/再小改那几处即可）。
 *       "小崎科技"是超链接：指向公司主页 gdxq.cn，新窗口打开，
 *       避免点击后离开本机的监视页面。 */
const char Html_Index_Content[] =
    "<div class=\"hero\">\r\n"
    "<div class=\"hero-brand\"><a href=\"http://gdxq.cn\" target=\"_blank\" title=\"小崎科技 公司主页\">小崎科技</a></div>\r\n"
    "<h1 class=\"hero-title\">MELSEC-F FX3U-ENET-ADP</h1>\r\n"
    "<div class=\"hero-sub\">数据监控</div>\r\n"
    "<div class=\"hero-line\"></div>\r\n"
    "</div>\r\n";

/*********************************************************************
 * @fn      MITSU_HTTP_GetTableBuffer
 *
 * @brief   获取表格缓冲区指针
 *
 * @param   无
 *
 * @return  表格缓冲区指针
 */
char* MITSU_HTTP_GetTableBuffer(void)
{
    return mitsu_table_buffer;
}

/*********************************************************************
 * @fn      MITSU_HTTP_BeginTable
 *
 * @brief   开始一张表格的流式发送，绑定目标 socket
 *
 * @param   sock - 目标 Socket ID
 *
 * @return  无
 */
void MITSU_HTTP_BeginTable(uint8_t sock)
{
    mitsu_table_sock      = sock;
    mitsu_table_len       = 0;
    mitsu_table_buffer[0] = '\0';
}

/*********************************************************************
 * @fn      MITSU_HTTP_FlushTable
 *
 * @brief   冲刷已累积的表格内容（若已绑定 socket 则发送出去）
 *
 * @param   无
 *
 * @return  无
 */
void MITSU_HTTP_FlushTable(void)
{
    if (mitsu_table_len == 0) {
        return;
    }

    if (mitsu_table_sock != 0xFF) {
        Data_Send(mitsu_table_sock, (uint8_t *)mitsu_table_buffer, mitsu_table_len);
    }

    mitsu_table_len       = 0;
    mitsu_table_buffer[0] = '\0';
}

/*********************************************************************
 * @fn      MITSU_HTTP_Emit
 *
 * @brief   追加一段表格 HTML；累积到阈值即自动发送，缓冲永不越界
 *
 * @param   src - 以 '\0' 结尾的 HTML 片段
 *
 * @return  无
 *
 * @note    替代原先无边界检查的 strcat(table_buffer, src)，
 *          单个片段超长时会分片发送，因此可支持任意长度的表格。
 */
void MITSU_HTTP_Emit(const char *src)
{
    size_t n;

    if (src == NULL) {
        return;
    }
    n = strlen(src);
    if (n == 0) {
        return;
    }

    /* 单段内容本身就接近或超过累积上限：直接冲刷并分片发出 */
    if (n >= MITSU_TABLE_EMIT_LIMIT) {
        MITSU_HTTP_FlushTable();
        if (mitsu_table_sock != 0xFF) {
            Data_Send(mitsu_table_sock, (uint8_t *)src, (uint32_t)n);
        }
        return;
    }

    /* 累积到阈值先冲刷，保证 mitsu_table_buffer 不越界 */
    if (mitsu_table_len + (uint32_t)n >= MITSU_TABLE_EMIT_LIMIT) {
        MITSU_HTTP_FlushTable();
    }

    memcpy(mitsu_table_buffer + mitsu_table_len, src, n);
    mitsu_table_len += (uint32_t)n;
    mitsu_table_buffer[mitsu_table_len] = '\0';
}

/*********************************************************************
 * @fn      MITSU_HTTP_ClearTableBuffer
 *
 * @brief   清空表格累积内容（保留已绑定的 socket）
 *
 * @param   无
 *
 * @return  无
 */
void MITSU_HTTP_ClearTableBuffer(void)
{
    /* 只重置累积状态：目标 socket 由 MITSU_HTTP_BeginTable 设定并保留 */
    mitsu_table_len       = 0;
    mitsu_table_buffer[0] = '\0';
}

/*********************************************************************
 * @fn      MITSU_HTTP_Init
 *
 * @brief   初始化三菱HTTP服务器
 *
 * @param   无
 *
 * @return  无
 */
void MITSU_HTTP_Init(void)
{
    /* 清空HTML缓冲区 - 使用HTTPS.c中的HtmlBuffer */
    ClearHtmlBuffer();

    FX_ACCLOG_Init();      // 初始化访问日志队列
    FX_ENETINF_Init();     // 初始化错误日志队列
    /* 初始化所有三菱HTTP页面模块 */
    FX_PLCINF_Init();
    FX_DEVMON_Init();

    printf("三菱HTTP服务器初始化完成\r\n");
}
 
/*********************************************************************
 * @fn      FX_index_SendWebPage
 *
 * @brief   流式发送主页HTML内容 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串(未使用)
 *
 * @return  无
 */
void FX_index_SendWebPage(uint8_t Dest_Sock, char *url)
{
    char *temp_buffer = HtmlBuffer;  /* 使用HtmlBuffer作为发送缓冲区 */
    uint32_t offset;

    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);

    /* 第一次打包: HTML头部到</head> (使用共享组件) */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "主页");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

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

    /* 第三次打包: 内容区域和页脚 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "%s", Html_Index_Content);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 页脚(含 SX 自检脚本)直发：组件 >1.2KB，远超 HtmlBuffer 容量(1024B)。
     * 原来与收尾内容挤在同一包 -> 越界写穿 http_request / g_access_fifo -> HardFault。 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_FOOTER_NEW),
              strlen(HTML_GetComponent(HTML_COMP_FOOTER_NEW)));

}
