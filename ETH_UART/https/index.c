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

/* 主页HTML内容 - 使用共享组件精简版 */
const char Html_Index_Content[] = "<div class=\"x\"><div class=\"c1\">小崎科技</div><div class=\"p\">MELSEC-F FX3U-ENET-ADP</div><div class=\"t\">数据监控</div></div>";

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
    offset += sprintf(temp_buffer + offset, HTML_GetComponent(HTML_COMP_HEADER), "主页");
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_CSS_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第二次打包: body开始到导航栏结束 (使用共享组件) */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_NAV_BAR_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第三次打包: 内容区域和页脚 */
    offset = 0;
    offset += sprintf(temp_buffer + offset, "%s", Html_Index_Content);
    offset += sprintf(temp_buffer + offset, "%s", HTML_GetComponent(HTML_COMP_FOOTER_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

}
