/********************************** (C) COPYRIGHT *******************************
 * File Name          : index.h
 * Author             : AI Assistant
 * Version            : V2.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP HTTP服务器头文件 - 复用HTTPS.c现有函数
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __INDEX_H__
#define __INDEX_H__

#include "debug.h"
#include "wchnet.h"
#include "HTTPS.h"
#include "html_components.h"
#include "fx_status.h"
#include "fx_plcinf.h"
#include "fx_enetinf.h"
#include "fx_acclog.h"
#include "fx_devmon.h"
#include "ethernet_app.h"
/* HTTP服务器端口号 */
#define MITSUBISHI_HTTP_PORT      80

/* 网页缓冲区大小 - 直接使用HTTPS.c中的HtmlBuffer */
 
/*
 * 表格累积缓冲区大小。
 * 改为"边生成边发送"后，该缓冲只作为发送前的暂存区，无需容纳整张表格。
 * MITSU_HTTP_Emit 对超长单段会自动直发，因此该值仅影响发送粒度、不会越界。
 * 取 512 字节：与 SX_CHUNK_MAX(900) 配合，兼顾 SRAM 与发送效率。
 */
#define MITSU_TABLE_BUFFER_SIZE   512

/* 外部变量声明 - 使用HTTPS.c中的结构体 */
extern st_http_request http_request;

extern char mitsu_table_buffer[MITSU_TABLE_BUFFER_SIZE];
 
/* 表格缓冲区管理函数 - 所有页面共用 */
char* MITSU_HTTP_GetTableBuffer(void);
void MITSU_HTTP_ClearTableBuffer(void);

/*
 * 表格流式发送接口
 *   1) MITSU_HTTP_BeginTable(sock) 绑定本次表格的目标 socket；
 *   2) 用 MITSU_HTTP_Emit() 逐段追加 HTML（累积到阈值即自动发出）；
 *   3) MITSU_HTTP_FlushTable() 冲刷尾部残留。
 * 如此可支持任意长度表格，且与 HTTP chunked 分包天然配合。
 */
void MITSU_HTTP_BeginTable(uint8_t sock);
void MITSU_HTTP_Emit(const char *src);
void MITSU_HTTP_FlushTable(void);

/* 主页HTML内容 - 外部声明 */
extern const char Html_Index[];

/* 函数声明 */

/**
 * @brief  初始化三菱HTTP服务器
 * @param  无
 * @retval 无
 * @note   初始化HTTP请求结构体和缓冲区
 */
void MITSU_HTTP_Init(void);

 
/**
 * @brief  解析HTTP请求
 * @param  request: HTTP请求结构体指针(st_http_request类型,来自HTTPS.h)
 * @param  buffer: HTTP数据缓冲区
 * @retval 无
 * @note   内部调用HTTPS.c的ParseHttpRequest函数
 */
void MITSU_HTTP_ParseRequest(st_http_request *request, char *buffer);

/**
 * @brief  解析URL类型
 * @param  type: 类型指针(来自HTTPS.h: PTYPE_HTML/PTYPE_PNG等)
 * @param  url: URL字符串
 * @retval 无
 * @note   内部调用HTTPS.c的ParseURLType函数
 */
void MITSU_HTTP_ParseURLType(char *type, char *url);

/**
 * @brief  生成HTTP响应头
 * @param  buffer: 响应缓冲区
 * @param  type: 资源类型(来自HTTPS.h: PTYPE_HTML/PTYPE_PNG等)
 * @param  content_len: 内容长度
 * @retval 响应头长度
 * @note   内部调用HTTPS.c的MakeHttpResponse函数
 */
uint32_t MITSU_HTTP_MakeResponseHeader(char *buffer, char type, uint32_t content_len);

/**
 * @brief  流式发送主页HTML内容
 * @param  Dest_Sock: Socket ID
 * @param  url: URL字符串
 * @retval 无
 * @note   流式发送三菱FX3U-ENET-ADP主页HTML内容,避免在RAM中存储完整页面
 */
void FX_index_SendWebPage(uint8_t Dest_Sock, char *url);

#endif /* __INDEX_H__ */
