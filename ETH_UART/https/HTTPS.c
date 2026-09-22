/********************************** (C) COPYRIGHT *******************************
 * File Name          : HTTPS.c
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/05/31
 * Description        : HTTPS related functions.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>    
#include "HTTPS.h"
#include "sx_stream.h"

#include <ctype.h>
#include <stdarg.h>   /* HTML_PACK: vsnprintf/va_list */

#include "bsp_uart.h"
#include "bsp_wch_net.h"

#include "index.h"
#include "fx_status.h"
#include "fx_plcinf.h"
#include "fx_enetinf.h"
#include "fx_acclog.h"
#include "fx_devmon.h"
#include "ethernet_app.h"      /* FX_ENETINF_SetMonitorState / FX_MONITOR_RUNNING */
#include "melsec_fx_core.h"    /* hex_str_to_intlend：把 PLC 回帧的 ASCII 十六进制数据解码为二进制 */


st_http_request http_request;

u8 *name;                                               //The name of the web page requested by HTTP

char HtmlBuffer[HTML_LEN];

/* ★ 本次响应是否已出现发送失败(对端断开)：由 SX_RawSend 置位，
 *   SX_Send 的分包循环据此提前放弃剩余分包，避免整页几十包都空转重试。 */
u8 g_sx_tx_failed = 0;                              //Web page send buffer

/* 编译期确认宏里的容量常量与缓冲区实际尺寸一致（防止将来只改一处） */
typedef char HTML_Assert_Buffer_Size_Matches_HTML_LEN[
    (sizeof(HtmlBuffer) == (unsigned)HTML_LEN) ? 1 : -1];

/**
 * @brief 向缓冲区追加格式化文本（带容量校验），供 HTML_PACK 宏调用
 * @param buf 目标缓冲区（通常是 HtmlBuffer）
 * @param off 当前写入偏移
 * @param cap 缓冲区总容量
 * @param fmt 格式化串，其余参数同 printf
 * @return 实际写入的字节数（0 表示空间不足/格式化失败；offset 不会超过 cap）
 *
 * @note 与 sprintf 的差别：空间不足时只写入能容纳的部分并打印告警，
 *       不做越界写入 —— 保证不会破坏 BSS 中紧邻 HtmlBuffer 的全局变量。
 */
uint32_t Html_PackChk(char *buf, uint32_t off, uint32_t cap, const char *fmt, ...)
{
    va_list ap;
    int need;
    uint32_t space;

    if (buf == NULL || fmt == NULL || cap == 0u) {
        return 0;
    }
    if (off >= cap) {
        printf("HTML_PACK 越界: off=%u cap=%u, 该包剩余内容已丢弃\r\n",
               (unsigned)off, (unsigned)cap);
        return 0;
    }

    space = cap - off;
    va_start(ap, fmt);
    need = vsnprintf(buf + off, space, fmt, ap);
    va_end(ap);

    if (need < 0) {
        buf[off] = '\0';
        printf("HTML_PACK 格式化失败: off=%u\r\n", (unsigned)off);
        return 0;
    }
    if ((uint32_t)need >= space) {
        /* 被截断：保留 space-1 字节 + 结尾 0 */
        printf("HTML_PACK 截断: 需要 %u 字节, 仅剩 %u (off=%u cap=%u)\r\n",
               (unsigned)need, (unsigned)space, (unsigned)off, (unsigned)cap);
        return space - 1u;
    }
    return (uint32_t)need;
}

/*********************************************************************
 * @fn      GetHtmlBuffer
 *
 * @brief   获取HTML缓冲区指针
 *
 * @param   无
 *
 * @return  HTML缓冲区指针
 */
char* GetHtmlBuffer(void)
{
    return HtmlBuffer;
}

/*********************************************************************
 * @fn       ClearHtmlBuffer
 *
 * @brief   清空HTML缓冲区
 *
 * @param   无
 *
 * @return  无
 */
void  ClearHtmlBuffer(void)
{
    memset(HtmlBuffer, 0, sizeof(HtmlBuffer));
}

volatile uint32_t g_Html_time_tick = 0;  // HTML时间计数器，用于记录系统运行时间（毫秒级）

/*********************************************************************
 * @fn      Html_time_handler
 *
 * @brief   HTML时间计数器处理函数，每毫秒调用一次，用于更新全局时间计数
 *
 * @param   无
 *
 * @return  无
 *
 * @note    该函数通常由定时器中断调用，实现简单的系统时间基准
 */
void Html_time_handler(void)
{
    g_Html_time_tick++;  // 时间计数器递增，每调用一次增加1
}

/*********************************************************************
 * @fn      Html_time_get
 *
 * @brief   获取当前HTML时间计数器的值
 *
 * @param   无
 *
 * @return  当前时间计数器的值（uint32_t类型）
 *
 * @note    可用于计算时间间隔或超时判断，注意计数器溢出处理
 */
uint32_t Html_time_get(void)
{
    return g_Html_time_tick;  // 返回当前时间计数值
}

/*********************************************************************
 * @fn      ParseHttpRequest
 *
 * @brief   解析HTTP请求报文，提取请求方法（GET/POST）和URL
 *
 * @param   request - HTTP请求结构体指针，用于保存解析结果
 *          buf - HTTP请求报文字符串（如："GET /index.html HTTP/1.1\r\n..."）
 *
 * @return  无
 *
 * @note    HTTP请求格式: <方法> <URL> <HTTP版本>\r\n<头部>\r\n<内容>
 *          示例: "GET /index.html?LANG=ZS HTTP/1.1\r\nHost: 192.168.1.1\r\n\r\n"
 */
void ParseHttpRequest(st_http_request *request, char *buf)
{
    char *strptr = buf;              // 指向请求报文的指针
    char *url_start = NULL;         // URL起始位置
    char *url_end = NULL;          // URL结束位置
    u32 url_len = 0;              // URL长度

    /* 跳过HTTP方法前的空格（处理格式不规范的情况） */
    while (*strptr == ' ') strptr++;

    /* 判断HTTP请求类型 */
    if (strstr(strptr, "GET") == strptr || strstr(strptr, "get") == strptr) {       /* GET请求 */
        request->METHOD = METHOD_GET;       // 设置请求方法为GET
        strptr += strlen("GET");            // 指针跳过"GET"
        while (*strptr == ' ') strptr++;    // 跳过GET和URL之间的空格
    }
    else if (strstr(strptr, "POST") == strptr || strstr(strptr, "post") == strptr) { /* POST请求 */
        request->METHOD = METHOD_POST;      // 设置请求方法为POST
        strptr += strlen("POST");          // 指针跳过"POST"
        while (*strptr == ' ') strptr++;    // 跳过POST和URL之间的空格
    }
    else {
        request->METHOD = METHOD_ERR;      // 未知的请求方法
        return;                            // 直接返回
    }

    /* 记录URL起始位置 */
    url_start = strptr;

    /* 查找URL结束符（\r或空格） */
    url_end = strchr(strptr, '\r');                 // 查找回车符（HTTP报文行结束）
    if (url_end == NULL) {
        url_end = strchr(strptr, ' ');             // 如果没找到\r，查找空格
    }

    /* 计算URL长度并检查是否超标 */
    if (url_end != NULL) {
        url_len = url_end - url_start;             // 计算URL长度
    } else {
        url_len = strlen(strptr);                  // 如果没找到结束符，取剩余长度
    }

    /* 检查URL长度是否超过最大限制 */
    if (url_len >= MAX_URL_SIZE) {
        url_len = MAX_URL_SIZE - 1;              // 截断超长URL
    }

    /* 直接将解析出的URL复制到请求结构体（不使用临时缓冲区） */
    strncpy((char*)request->URL, url_start, url_len);  // 复制URL到结构体
    request->URL[url_len] = '\0';                      // 确保字符串以\0结尾
    HTTPS_DEBUG("url_len=%d, URL:%s \r\n", url_len, request->URL);
}

/*********************************************************************
 * @fn      ParseURLType
 *
 * @brief   Parse URL type
 *
 * @param   type - type.
 *          buf - data buff
 *
 * @return  URL type
 */
void ParseURLType(char *type, char * buf)
{
    if (strstr(buf, ".html") || strstr(name, "HTTP")) /* html type */
        *type = PTYPE_HTML;
    else if (strstr(buf, ".png"))                    /* png type */
        *type = PTYPE_PNG;
    else if (strstr(buf, ".css"))                    /* css type */
        *type = PTYPE_CSS;
    else if (strstr(buf, ".gif"))                    /* gif type */
        *type = PTYPE_GIF;
    else
        *type = PTYPE_ERR;
}

/*********************************************************************
 * @fn      SendHttpResponse
 *
 * @brief   Directly send HTTP response header and content with packet fragmentation
 *
 * @param   socket_id - Socket ID
 *          type - content type (PTYPE_HTML, PTYPE_PNG, etc.)
 *          content - content pointer (可以是Flash或HtmlBuffer)
 *          len - content length
 *
 * @return  none
 *
 * @note    如果内容长度超过单次发送限制，会自动分包发送
 */
static void SendHttpResponse(u8 socket_id, char type, const char *content, u32 len)
{
    const char *head;
    char len_str[16];
    u32 head_len;
    u32 sent_len = 0;
    u32 chunk_len;
    u8 *data_ptr;

    /* 参数有效性检查 */
    if (content == NULL || len == 0) {
        HTTPS_DEBUG("error: content=%p, len=%d \r\n", content, len);
        return;
    }

    /* Select response header based on content type */
    if (type == PTYPE_HTML)
        head = RES_HTMLHEAD_OK;
    else if (type == PTYPE_PNG)
        head = RES_PNGHEAD_OK;
    else if (type == PTYPE_CSS)
        head = RES_CSSHEAD_OK;
    else if (type == PTYPE_GIF)
        head = RES_GIFHEAD_OK;
    else{
        HTTPS_DEBUG("SendHttpResponse error: type =%d \r\n",type );
        return;
    }

    /* Send response header */
    head_len = strlen(head);
    Data_Send(socket_id, (u8*)head, head_len);

    /* Send content length */
    snprintf(len_str, sizeof(len_str), "%d", len);
    Data_Send(socket_id, (u8*)len_str, strlen(len_str));

    /* Send header terminator */
    Data_Send(socket_id, (u8*)RES_END, strlen(RES_END));

    /* Send content data with packet fragmentation */
    data_ptr = (u8*)content;

    while (sent_len < len)
    {
        /* 计算本次发送的数据块大小（最大HTML_LEN字节） */
        chunk_len = len - sent_len;
        if (chunk_len > HTML_LEN) {
            chunk_len = HTML_LEN;
        }

        /* 发送数据块 */
        Data_Send(socket_id, data_ptr + sent_len, chunk_len);
        sent_len += chunk_len;

        /* 如果还有剩余数据，稍作延时避免发送过快导致缓冲区溢出 */
        if (sent_len < len) {
            Delay_Ms(5);  // 短暂延时5ms
        }
    }

    HTTPS_DEBUG("SendHttpResponse: type=%d, total_len=%d, chunks=%d\r\n", type, len, (len + HTML_LEN) / HTML_LEN );
}

/*********************************************************************
 * @fn      SendHttpHeader
 *
 * @brief   Send HTTP response header without Content-Length (流式发送模式)
 *
 * @param   socket_id - Socket ID
 *          type - content type (PTYPE_HTML, PTYPE_PNG, etc.)
 *
 * @return  none
 *
 * @note    使用Transfer-Encoding: chunked或直接流式发送，不需要预先知道总长度
 */
void SendHttpHeader(u8 socket_id, char type)
{
    /*
     * 统一走 chunked 分包模式。
     * 页面是流式拼装发送的，事先无法得知总长度，而 HTTP/1.1 下不带
     * Content-Length 的裸流式响应会让浏览器无法判断响应结束（一直转圈）。
     * 页面发送完毕后，由 Web_Server / Web_Usart_Handler 调用 SX_End() 收尾。
     */
    g_sx_tx_failed = 0;   /* 新响应开始，清除上一轮的发送失败标志 */
    SX_Begin(socket_id, type, SX_PAGE_UNKNOWN);
}

/*********************************************************************
 * @fn      DataLocate
 *
 * @brief   Locate the location of "name"
 *
 * @param   buf - data buff
 *          name - name strings
 *
 * @return  pointer to the last digit of name
 */
char * DataLocate(char *buf, char *name)
{
    char *p;
    p = strstr(buf, name);
    if (p != NULL)
        p += strlen(name);
    return p;
}

/*********************************************************************
 * @fn      atoh
 *
 * @brief   Converting character to hexadecimal number
 *
 * @param   src - character
 *
 * @return  hexadecimal number
 */
uint8_t atoh(uint8_t *src)
{
    uint8_t desc=0;

    if((*src >= '0') && (*src <= '9'))
    desc = *src - 0x30;
    else if((*src >= 'a') && (*src <= 'f'))
    desc = *src - 0x57;
    else if((*src >= 'A') && (*src <= 'F'))
    desc = *src - 0x37;

    return desc;
}
/*********************************************************************
 * @fn      Refresh_Basic
 *
 * @brief   Parse the basic interface configuration parameters from
 *          the post request and store the parsed parameters in the
 *          flash in the form of a structure
 *
 * @param   buf - data buff
 *
 * @return  none
 */
void Refresh_Basic(u8 *buf)
{
    char *p, *q;
    char temp[30];                               //Save the value of each configuration in the form of a string
    Basic_Cfg_t BasicCfg;
    u8 i;

    memset((uint8_t *)(&BasicCfg), 0, BASIC_CFG_LEN);
    BasicCfg.flag[0] = 0x57;
    BasicCfg.flag[1] = 0xAB;

    p = DataLocate(buf, "__PMAC=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        p = strtok(temp, ".");
        (strlen(p) == 1) ? (BasicCfg.mac[0] = atoh(p)) : (BasicCfg.mac[0] = atoh(p) << 4 | atoh(p + 1));
        for (i = 1; i < 6; i++) {
            p = strtok(NULL, ".");
            (strlen(p) == 1) ? (BasicCfg.mac[i] = atoh(p)) : (BasicCfg.mac[i] = atoh(p) << 4 | atoh(p + 1));
        }
    }
    else return;

    p = DataLocate(q, "__PSIP=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        p = strtok(temp, ".");
        BasicCfg.ip[0] = atoi(p);
        for (i = 1; i < 4; i++) {
            p = strtok(NULL, ".");
            BasicCfg.ip[i] = atoi(p);
        }
    }
    else return;

    p = DataLocate(q, "__PMSK=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        p = strtok(temp, ".");
        BasicCfg.mask[0] = atoi(p);
        for (i = 1; i < 4; i++) {
            p = strtok(NULL, ".");
            BasicCfg.mask[i] = atoi(p);
        }
    }
    else return;

    p = DataLocate(q, "__PGAT=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "\r\n");
        if(q)
            memcpy(temp, p, (q - p));
        else
            memcpy(temp, p, strlen(p));
        p = strtok(temp, ".");
        BasicCfg.gateway[0] = atoi(p);
        for (i = 1; i < 4; i++) {
            p = strtok(NULL, ".");
            BasicCfg.gateway[i] = atoi(p);
        }
    }
    else return;

    BSP_FLASH_ERASE( BASIC_CFG_ADDR, FLASH_PAGE_SIZE);
    BSP_FLASH_WRITE_Word( BASIC_CFG_ADDR, (uint8_t *)(&BasicCfg), BASIC_CFG_LEN);
    
    HTTPS_DEBUG("flag:%x %x\r\n", BasicCfg.flag[0], BasicCfg.flag[1]);
    HTTPS_DEBUG("mac:%x %x %x %x %x %x\r\n", BasicCfg.mac[0], BasicCfg.mac[1], \
                                        BasicCfg.mac[2], BasicCfg.mac[3], \
                                        BasicCfg.mac[4], BasicCfg.mac[5]);
    HTTPS_DEBUG("ip:%d %d %d %d\r\n", BasicCfg.ip[0], BasicCfg.ip[1],\
                                    BasicCfg.ip[2], BasicCfg.ip[3]);
    HTTPS_DEBUG("mask:%d %d %d %d\r\n", BasicCfg.mask[0], BasicCfg.mask[1],\
                                        BasicCfg.mask[2], BasicCfg.mask[3]);
    HTTPS_DEBUG("gateway:%d %d %d %d\r\n", BasicCfg.gateway[0], BasicCfg.gateway[1],\
                                        BasicCfg.gateway[2], BasicCfg.gateway[3]);
}

/*********************************************************************
 * @fn      Refresh_Port
 *
 * @brief   Parse the Port parameter from the post request
 *          and store the parsed parameter in flash
 *
 * @param   buf - data buff
 *
 * @return  none
 */
void Refresh_Port(char *buf)
{
    u8 i;
    char *p, *q;
    char temp[30];
    Port_Cfg_t portCfg;

    memset((uint8_t *)(&portCfg), 0, PORT_CFG_LEN);
    portCfg.flag[0] = 0X57;
    portCfg.flag[1] = 0XAB;

    p = DataLocate(buf, "__PMOD=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        if (strcmp(temp, "0") == 0)
        {
            portCfg.mode = MODE_TCPSERVER;
        }
        if (strcmp(temp, "1") == 0) {
            portCfg.mode = MODE_TCPCLIENT;
        }
    }
    else return;

    p = DataLocate(q, "__PSPT=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        portCfg.src_port[0] = atoi(temp) / 256;
        portCfg.src_port[1] = atoi(temp) % 256;
    }
    else return;

    p = DataLocate(q, "__PDIP=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "&");
        if(q == NULL) return;
        memcpy(temp, p, (q - p));
        p = strtok(temp, ".");
        portCfg.des_ip[0] = atoi(p);
        for (i = 1; i < 4; i++) {
            p = strtok(NULL, ".");
            portCfg.des_ip[i] = atoi(p);
        }
    }
    else return;

    p = DataLocate(q, "__PDPT=");
    if (p != NULL) {
        memset(temp, 0, 30);
        q = strstr(p, "\r\n");
        if(q)
            memcpy(temp, p, (q - p));
        else
            memcpy(temp, p, strlen(p));
        portCfg.des_port[0] = atoi(temp) / 256;
        portCfg.des_port[1] = atoi(temp) % 256;
    }
    else return;

    BSP_FLASH_ERASE( PORT_CFG_ADDR, FLASH_PAGE_SIZE);
    BSP_FLASH_WRITE_Word( PORT_CFG_ADDR, (uint8_t *)(&portCfg), PORT_CFG_LEN);

    HTTPS_DEBUG("mode:%x\r\n",portCfg.mode);
    HTTPS_DEBUG("src_port:%d\r\n", portCfg.src_port[0]*256 + portCfg.src_port[1]);
    HTTPS_DEBUG("des_ip:%d %d %d %d\r\n", portCfg.des_ip[0], portCfg.des_ip[1], portCfg.des_ip[2], portCfg.des_ip[3]);
    HTTPS_DEBUG("des_port:%d\r\n", portCfg.des_port[0]*256 + portCfg.des_port[1]);
}

/*********************************************************************
 * @fn      ASCToDec
 *
 * @brief   Convert ASC code to Decimal number
 *
 * @param   ASCPtr - Pointer to the character to be converted
 *
 * @return  status
 */
uint8_t ASCToDec(uint8_t *ASCPtr)
{
    uint8_t i;
    for(i = 0; i < 2; i++)
    {
        if( (*ASCPtr >= '0') && (*ASCPtr <= '9'))
        {
            *ASCPtr = *ASCPtr - '0';
        }
        else if( (*ASCPtr >= 'a') && (*ASCPtr <= 'f'))
        {
            *ASCPtr = *ASCPtr - 'a' + 10;
        }
        else if( ( *ASCPtr >= 'A') && (*ASCPtr <= 'F'))
        {
            *ASCPtr = *ASCPtr - 'A' + 10;
        }
        else return NoREADY;
        ASCPtr++;
    }
    return READY;
}

/*********************************************************************
 * @fn      URLDecode
 *
 * @brief   Decoding special characters for URL transmission
 *
 * @param   srcptr - source buff
 *          desptr - destination buff
 *          bufflen - length of source buff
 *
 * @return  status or Decoded string length
 */
uint8_t URLDecode(char *srcptr, char *desptr, uint8_t bufflen)
{
    uint8_t i = 0, ret, datalen = 0;
    char tempbuf[2];
    while(i < bufflen)
    {
        if(srcptr[i] == '%')
        {
            tempbuf[0] = srcptr[++i];
            tempbuf[1] = srcptr[++i];
            ret = ASCToDec(tempbuf);
            if(ret == NoREADY) return ret;
            tempbuf[0] = tempbuf[0] * 16 + tempbuf[1];
            *desptr++ = tempbuf[0];
            datalen++;
            i++;
        }
        else {
            *desptr++ = srcptr[i++];
            datalen++;
        }
        if(datalen > sizeof(Login_CfgBuf.pass)) return NoREADY;
    }
    return datalen;
}

/* URL 解码(%XX -> 字节)，返回解码后字节数(含结尾 0)。不复用上面的 URLDecode():
 * 它有 Login_CfgBuf.pass 长度上限，这里需要按 CMD= 定长截取。 */
static uint8_t HTTPS_DecodeUrlBytes(const char *src, char *dst, uint8_t dst_len)
{
    uint8_t n = 0;

    if (src == NULL || dst == NULL || dst_len == 0u) {
        return 0;
    }
    while (*src != 0 && n < (uint8_t)(dst_len - 1u)) {
        if (src[0] == 0x25 && src[1] != 0 && src[2] != 0) {
            char hex[3];
            hex[0] = src[1];
            hex[1] = src[2];
            hex[2] = 0;
            dst[n++] = (char)strtol(hex, NULL, 16);
            src += 3;
        } else {
            dst[n++] = *src++;
        }
    }
    dst[n] = 0;
    return n;
}

/* 解析 POST 请求里的 CMD 参数(监视开始/监视停止)，命中即切换监视运行状态。
 * 这两个按钮在 通信状态页(fx_status) 与 FX3U-ENET-ADP信息页(fx_enetinf) 上都有，
 * 但这两个 POST 分支此前只重绘页面、不解析 CMD(全工程只有 fx_devmon 页解析)，
 * 所以按钮点了没反应。POST 值是 GBK 的 URL 编码(监视开始=%BC%E0%CA%D3%BF%AA%CA%BC)，
 * 解码后与十六进制转义的 GBK 字节比较，不受源文件编码影响。 */
static void HTTPS_HandleMonitorCmd(const char *request)
{
    char enc[32];
    char dec[32];
    const char *p;
    uint8_t i = 0;

    if (request == NULL) {
        return;
    }
    p = strstr(request, "CMD=");
    if (p == NULL) {
        return;
    }
    p += 4;
    while (p[i] != 0 && p[i] != 0x26 && i < (uint8_t)(sizeof(enc) - 1u)) {
        enc[i] = p[i];
        i++;
    }
    enc[i] = 0;

    if (HTTPS_DecodeUrlBytes(enc, dec, (uint8_t)sizeof(dec)) == 0u) {
        return;
    }

    if (strcmp(dec, "\xBC\xE0\xCA\xD3\xBF\xAA\xCA\xBC") == 0) {           /* 监视开始 */
        FX_ENETINF_SetMonitorState(FX_MONITOR_RUNNING);
    } else if (strcmp(dec, "\xBC\xE0\xCA\xD3\xCD\xA3\xD6\xB9") == 0) {    /* 监视停止 */
        FX_ENETINF_SetMonitorState(FX_MONITOR_STOPPED);
    }
}

/*********************************************************************
 * @fn      Refresh_Login
 *
 * @brief   Parse the login parameter from the post request
 *          and store the parsed parameter in flash
 *
 * @param   buf - data buff
 *
 * @return  none
 */
void Refresh_Login(char *buf)
{
    char *p, *q = NULL;
    u8 len;
    uint8_t tempbuff[30] = {0x00};
    Login_Cfg_t LoginInf;


    memset((uint8_t *)(&LoginInf), 0, LOGIN_CFG_LEN);
    LoginInf.flag[0] = 0X57;
    LoginInf.flag[1] = 0XAB;

    p = DataLocate(buf, "__PUSE=");
    if (p != NULL) {
        q = strstr(p, "&");
        if(q == NULL) return;
        len = q - p;
        if(len > sizeof(tempbuff)) return;
        len = URLDecode(p, tempbuff, len);
        if(len == 0) return;
        memcpy(LoginInf.user, tempbuff, len);
    }
    else return;

    p = DataLocate(q, "__PPAS=");
    if (p != NULL) {
        q = strstr(p, "\r\n");
        if(q)
            len = q - p;
        else
            len = strlen(p);
        if(len > sizeof(tempbuff)) return;
        len = URLDecode(p, tempbuff, len);
        if(len == 0) return;
        memcpy(LoginInf.pass, tempbuff, len);
    }
    else return;

    BSP_FLASH_ERASE( LOGIN_CFG_ADDR, FLASH_PAGE_SIZE);
    BSP_FLASH_WRITE_Word( LOGIN_CFG_ADDR, (uint8_t *)(&LoginInf), LOGIN_CFG_LEN);
    HTTPS_DEBUG("user:%s\r\n",LoginInf.user);
    HTTPS_DEBUG("pass:%s\r\n",LoginInf.pass);
}

/*********************************************************************
 * @fn      Refresh_Html
 *
 * @brief   选择Flash中的HTML文件，用参数表中的值替换其中的占位符，
 *          并将结果保存到HtmlBuffer[]中。
 *
 * @param   html - HTML数组常量（源HTML模板）
 *          buf - 参数结构体数组（包含占位符和对应的替换值）
 *          paranum - 参数数量
 *
 * @return  处理后的数据长度，失败返回0
 */
uint16_t Refresh_Html(const char *html, Parameter *buf, u8 paranum)
{
    const char *lastptr = html;  // 上一次处理的指针位置
    char *currptr;               // 当前找到的占位符指针
    const char *keyword = "__A"; // 占位符前缀
    uint8_t i;
    uint16_t datalen = 0, copylen = 0;

    // 参数有效性检查
    if (html == NULL || buf == NULL || paranum == 0)
    {
        HTTPS_DEBUG("Refresh_Html: 参数错误\n");
        return 0;
    }

    // 遍历HTML内容，查找并替换所有占位符
    while(1)
    {
        currptr = strstr(lastptr, keyword);  // 查找占位符前缀
        if(currptr == NULL) break;          // 未找到占位符，退出循环

        copylen = currptr - lastptr;        // 计算占位符前的文本长度

        // 检查缓冲区边界，防止溢出
        if ((datalen + copylen) >= HTML_LEN)
        {
            HTTPS_DEBUG("Refresh_Html: 缓冲区溢出风险\n");
            return 0;
        }

        memcpy(&HtmlBuffer[datalen], lastptr, copylen);  // 复制占位符前的文本
        datalen += copylen;

        // 在参数表中查找匹配的占位符
        for(i = 0; i < paranum; i++)
        {
            if (buf[i].para == NULL)
                continue;  // 跳过无效参数

            if(memcmp(currptr, buf[i].para, strlen(buf[i].para)) == 0)
            {
                HTTPS_DEBUG("keyword ok!!\n");
                break;
            }
        }

        // 未找到匹配的占位符
        if(i == paranum)
        {
            HTTPS_DEBUG("keyword failed!!\n");
            return 0;
        }

        // 二次验证value指针有效性（防止野指针）
        if (buf[i].value == NULL)
        {
            HTTPS_DEBUG("Refresh_Html: value指针为NULL\n");
            return 0;
        }

        // 检查替换值长度边界
        uint16_t valuelen = strlen(buf[i].value);
        if ((datalen + valuelen) >= HTML_LEN)
        {
            HTTPS_DEBUG("Refresh_Html: 缓冲区溢出风险\n");
            return 0;
        }

        memcpy(&HtmlBuffer[datalen], buf[i].value, valuelen);  // 复制替换值
        datalen += valuelen;
        lastptr = currptr + strlen(buf[i].para);  // 更新处理位置到占位符之后
    }

    // 复制剩余的HTML内容（最后一个占位符之后的所有内容）
    if(strlen(lastptr) != 0)
    {
        if ((datalen + strlen(lastptr)) >= HTML_LEN)
        {
            HTTPS_DEBUG("Refresh_Html: 缓冲区溢出风险\n");
            return 0;
        }
        memcpy(&HtmlBuffer[datalen], lastptr, strlen(lastptr));
        datalen += strlen(lastptr);
    }

    // 确保字符串以'\0'结尾
    if (datalen < HTML_LEN)
    {
        HtmlBuffer[datalen] = '\0';
    }

    return datalen;
}

/*********************************************************************
 * @fn      copy_flash
 *
 * @brief   Select the html file in the flash and copy it directly
 *          to the HtmlBuffer (only for some web pages without variables)
 *
 * @param   html - HTML array constants
 *          len - data length
 *
 * @return  none
 */
void copy_flash(const char *html, u32 len)
{
    u32 copy_len;

    // 参数有效性检查
    if (html == NULL || len == 0) {
        return;
    }

    // 限制拷贝长度，防止缓冲区溢出
    copy_len = (len > HTML_LEN) ? HTML_LEN : len;

    // 清空缓冲区后拷贝数据
    memset(HtmlBuffer, 0, HTML_LEN);
    memcpy(HtmlBuffer, html, copy_len);
}



/*********************************************************************
 * @fn      Init_Para_Tab
 *
 * @brief   Initialization parameter table.
 *
 * @return  none
 */
void Init_Para_Tab(void)
{
    u8 s[30];

    Para_Basic[0].para = "__AMAC";
    memset(s, 0, 30);
    snprintf(s, 30, "%x.%x.%x.%x.%x.%x", Basic_CfgBuf.mac[0], Basic_CfgBuf.mac[1],
            Basic_CfgBuf.mac[2], Basic_CfgBuf.mac[3], Basic_CfgBuf.mac[4],
            Basic_CfgBuf.mac[5]);
    strcpy(Para_Basic[0].value, s);
 

    Para_Basic[1].para = "__ASIP";
    memset(s, 0, 30);
    snprintf(s, 30, "%d.%d.%d.%d", Basic_CfgBuf.ip[0], Basic_CfgBuf.ip[1],
            Basic_CfgBuf.ip[2], Basic_CfgBuf.ip[3]);
    strcpy(Para_Basic[1].value, s);
 

    Para_Basic[2].para = "__AMSK";
    memset(s, 0, 30);
    snprintf(s, 30, "%d.%d.%d.%d", Basic_CfgBuf.mask[0], Basic_CfgBuf.mask[1],
            Basic_CfgBuf.mask[2], Basic_CfgBuf.mask[3]);
    strcpy(Para_Basic[2].value, s);
 

    Para_Basic[3].para = "__AGAT";
    memset(s, 0, 30);
    snprintf(s, 30, "%d.%d.%d.%d", Basic_CfgBuf.gateway[0],
            Basic_CfgBuf.gateway[1], Basic_CfgBuf.gateway[2],
            Basic_CfgBuf.gateway[3]);
    strcpy(Para_Basic[3].value, s);
 

    Para_Port[0].para = "__AMOD";
    memset(s, 0, 30);
    snprintf(s, 30, "%d", Port_CfgBuf.mode);
    strcpy(Para_Port[0].value, s);
 

    Para_Port[1].para = "__ASPT";
    memset(s, 0, 30);
    snprintf(s, 30, "%d", Port_CfgBuf.src_port[0] * 256 + Port_CfgBuf.src_port[1]);
    strcpy(Para_Port[1].value, s);
 

    Para_Port[2].para = "__ADIP";
    memset(s, 0, 30);
    snprintf(s, 30, "%d.%d.%d.%d", Port_CfgBuf.des_ip[0], Port_CfgBuf.des_ip[1],
            Port_CfgBuf.des_ip[2], Port_CfgBuf.des_ip[3]);
    strcpy(Para_Port[2].value, s);
 

    Para_Port[3].para = "__ADPT";
    memset(s, 0, 30);
    snprintf(s, 30, "%d", Port_CfgBuf.des_port[0] * 256 + Port_CfgBuf.des_port[1]);
    strcpy(Para_Port[3].value, s);
 

    Para_Login[0].para = "__AUSE";
    strcpy(Para_Login[0].value, Login_CfgBuf.user);
 

    Para_Login[1].para = "__APAS";
    strcpy(Para_Login[1].value, Login_CfgBuf.pass);
 
}

/*********************************************************************
 * @fn      Data_Send
 *
 * @brief   Socket sends data with retry mechanism
 *
 * @param   id - Socket ID
 *          dataptr - pointer to data buffer
 *          datalen - total data length to send
 * @return  none
 */
void SX_RawSend(u8 id, const uint8_t *dataptr, uint32_t datalen)
{
    u32 len, totallen;
    const u8 *p;
    u8 timeout;
    u32 sent;  // 实际发送的字节数

    // 参数有效性检查
    if (dataptr == NULL || datalen == 0) {
        return;
    }

    if (id < WCHNET_MAX_SOCKET_NUM) {
        ETH_S(id).net_tx_packets += datalen;        /* net发送包数 */
    }

    p = dataptr;
    totallen = datalen;
    timeout = 50;  // 最大重试次数
    #if NET_LED_ENABLE == 1
    NEN_TX_LED_Trigger();  // 触发发送LED闪烁
    #endif
    while (totallen > 0)
    {
        len = totallen;
        // 发送数据
        sent = len;
        WCHNET_SocketSend(id, (u8 *)p, &sent);
        // 更新剩余数据量
        if (sent > 0) {
            // 防止sent超过剩余数据量导致totallen溢出
            if (sent > totallen) {
                sent = totallen;
            }
            totallen -= sent;
            p += sent;
            timeout = 50;  // 重置超时计数器
        } else {
            // 发送失败或缓冲区满，减少超时计数
            if(--timeout == 0) {
                //HTTPS_DEBUG("SX_RawSend timeout, remaining=%d\n", totallen);
                g_sx_tx_failed = 1;   /* 标记：对端已发不出去，后续分包不必再试 */
                break;
            }
            Delay_Ms(1);  // 等待1ms后重试
            IWDG_ReloadCounter();   /* 发送重试期间喂狗：大页面(几十个分包)每个分包最多重试
                                     * 50 x 1ms，累计会超过 IWDG(3.2s) -> 复位。重试次数
                                     * 有上限(50)，因此不会掩盖真正的死循环。 */
        }
    }
}

/*********************************************************************
 * @fn      Data_Send
 *
 * @brief   统一发送入口：处于分包流模式时自动进行 chunk 包装与节流
 *
 * @param   id - Socket ID
 *          dataptr - pointer to data buffer
 *          datalen - total data length to send
 *
 * @return  none
 *
 * @note    所有页面的 Data_Send 调用无需修改，即可自动获得
 *          chunked 分包、CRC 累进与发送节流能力。
 */
void Data_Send(u8 id, uint8_t *dataptr, uint32_t datalen)
{
    if (dataptr == NULL || datalen == 0) {
        return;
    }

    if (SX_Active(id)) {
        /* 分包流模式：chunked 包装 + 切分 + 节流 + 整页 CRC 累进 */
        SX_Send(id, (const u8 *)dataptr, datalen);
    } else {
        /* 普通模式：直接发送（如 SendHttpResponse 的 Content-Length 路径） */
        SX_RawSend(id, (const u8 *)dataptr, datalen);
    }
}


// 从原始字节流中查找并提取HTTP请求体
char* extract_body_from_raw_data(const unsigned char* raw_data, int data_len) {
    if (raw_data == NULL || data_len <= 0) {
        return NULL;
    }
    
    // 将字节流视为字符串处理
    const char* data = (const char*)raw_data;
    
    // 查找HTTP请求开始（POST或GET等）
    const char* http_start = strstr(data, "POST ");
    if (http_start == NULL) {
        http_start = strstr(data, "GET ");
        if (http_start == NULL) {
            http_start = strstr(data, "HTTP/");
            if (http_start == NULL) {
                return NULL;  // 不是HTTP请求
            }
        }
    }
    
    // 查找请求体开始位置
    const char* body_marker = strstr(http_start, "\r\n\r\n");
    if (body_marker != NULL) {
        body_marker += 4;  // 跳过\r\n\r\n
        
        // 计算请求体长度
        int body_len = data_len - (body_marker - data);
        if (body_len <= 0) {
            return NULL;
        }
        
        // 复制请求体
        char* body = (char*)malloc(body_len + 1);
        if (body == NULL) {
            perror("内存分配失败");
            return NULL;
        }
        
        strncpy(body, body_marker, body_len);
        body[body_len] = '\0';
        
        return body;
    }
    
    return NULL;
}

// 处理URL编码的请求体
void decode_url_encoded_body(const char* encoded_body) {
    if (encoded_body == NULL) {
        return;
    }
    
    HTTPS_DEBUG("URL编码的请求体: %s\n", encoded_body);
    HTTPS_DEBUG("解码后的关键参数:\n");
    
    // 简单解析参数（实际使用时需要更完善的解析）
    const char* token = strtok((char*)encoded_body, "&");
    while (token != NULL) {
        HTTPS_DEBUG("  %s\n", token);
        token = strtok(NULL, "&");
    }
}

// 实用函数：查找特定参数
char* find_parameter_value(const char* body, const char* param_name) {
    if (body == NULL || param_name == NULL) {
        return NULL;
    }
    
    char search_str[256];
    snprintf(search_str, sizeof(search_str), "%s=", param_name);
    
    const char* param_start = strstr(body, search_str);
    if (param_start == NULL) {
        return NULL;
    }
    
    param_start += strlen(search_str);
    
    const char* param_end = strchr(param_start, '&');
    if (param_end == NULL) {
        param_end = param_start + strlen(param_start);
    }
    
    int value_len = param_end - param_start;
    char* value = (char*)malloc(value_len + 1);
    if (value == NULL) {
        return NULL;
    }
    
    strncpy(value, param_start, value_len);
    value[value_len] = '\0';
    
    return value;
}

// int main() {
//     // 示例：从原始数据中提取
//     unsigned char raw_packet[] = 
//         "POST /fx_devmon.html HTTP/1.1\r\n"
//         "Host: 192.168.1.250\r\n"
//         "Content-Length: 123\r\n"
//         "\r\n"
//         "MONT=D&DEVT=D&DEVN=0&CMD=%BC%E0%CA%D3%BF%AA%CA%BC&MDL=0&BFMN=0&BFMV=10%BD%F8%D6%C6&INT=5&DISP=16&VAL=D&FORM=WD&BITO=F";
    
//     HTTPS_DEBUG("=== 从原始数据包提取请求体 ===\n");
//     char* body = extract_body_from_raw_data(raw_packet, sizeof(raw_packet));
    
//     if (body != NULL) {
//         HTTPS_DEBUG("提取的请求体: %s\n\n", body);
        
//         // 查找特定参数
//         char* cmd_value = find_parameter_value(body, "CMD");
//         if (cmd_value != NULL) {
//             HTTPS_DEBUG("CMD参数值: %s\n", cmd_value);
//             // 注意：这里的值是URL编码的，需要进一步解码
//             free(cmd_value);
//         }
        
//         char* bfmv_value = find_parameter_value(body, "BFMV");
//         if (bfmv_value != NULL) {
//             HTTPS_DEBUG("BFMV参数值: %s\n", bfmv_value);
//             free(bfmv_value);
//         }
        
//         free(body);
//     }
    
//     return 0;
// }
 
// 从HTTP请求中提取请求体
char* extract_http_body(const char* http_request) {
    if (http_request == NULL) {
        return NULL;
    }
    
    // 查找请求头和请求体之间的分隔符: \r\n\r\n
    const char* body_start = strstr(http_request, "\r\n\r\n");
    
    if (body_start == NULL) {
        // 有些请求可能使用\n\n作为分隔符
        body_start = strstr(http_request, "\n\n");
        if (body_start != NULL) {
            body_start += 2;  // 跳过\n\n
        } else {
            return NULL;  // 找不到请求体分隔符
        }
    } else {
        body_start += 4;  // 跳过\r\n\r\n
    }
    
    // 创建请求体的副本
    char* body = strdup(body_start);
    if (body == NULL) {
        perror("内存分配失败");
        return NULL;
    }
    
    return body;
}

// 解析Content-Length头部以确定请求体长度
char* extract_http_body_with_content_length(const char* http_request) {
    if (http_request == NULL) {
        return NULL;
    }
    
    // 查找Content-Length头部
    const char* content_length_ptr = strstr(http_request, "Content-Length:");
    int body_length = 0;
    
    if (content_length_ptr != NULL) {
        sscanf(content_length_ptr, "Content-Length: %d", &body_length);
    }
    
    // 查找请求体开始位置
    const char* body_start = strstr(http_request, "\r\n\r\n");
    if (body_start == NULL) {
        body_start = strstr(http_request, "\n\n");
        if (body_start != NULL) {
            body_start += 2;
        } else {
            return NULL;
        }
    } else {
        body_start += 4;
    }
    
    // 如果没有Content-Length，就取到字符串结尾
    if (body_length <= 0) {
        return strdup(body_start);
    }
    
    // 根据Content-Length提取准确长度的请求体
    char* body = (char*)malloc(body_length + 1);
    if (body == NULL) {
        HTTPS_DEBUG("内存分配失败\r\n");
        return NULL;
    }
    
    strncpy(body, body_start, body_length);
    body[body_length] = '\0';
    //HTTPS_DEBUG("body_start:body_length=%d,\r\n%s\r\n",body_length,body);
    return body;
}

// 完整解析HTTP请求，包括提取方法、URL、头部和请求体
typedef struct {
    char method[16];
    char url[256];
    char headers[1024];
    char* body;
} HttpRequest;

HttpRequest* parse_http_request(const char* request_data) {
    if (request_data == NULL || strlen(request_data) == 0) {
        return NULL;
    }
    
    HttpRequest* req = (HttpRequest*)malloc(sizeof(HttpRequest));
    if (req == NULL) {
        perror("内存分配失败");
        return NULL;
    }
    
    // 初始化
    memset(req, 0, sizeof(HttpRequest));
    req->body = NULL;
    
    // 解析请求行（第一行）
    char first_line[512];
    sscanf(request_data, "%511[^\r\n]", first_line);
    sscanf(first_line, "%15s %255s", req->method, req->url);
    
    // 查找头部开始位置（跳过第一行）
    const char* headers_start = strstr(request_data, "\r\n");
    if (headers_start != NULL) {
        headers_start += 2;  // 跳过\r\n
        
        // 查找头部结束位置
        const char* headers_end = strstr(headers_start, "\r\n\r\n");
        if (headers_end != NULL) {
            // 复制头部
            int headers_len = headers_end - headers_start;
            if (headers_len < sizeof(req->headers)) {
                strncpy(req->headers, headers_start, headers_len);
                req->headers[headers_len] = '\0';
            }
            
            // 提取请求体
            const char* body_start = headers_end + 4;  // 跳过\r\n\r\n
            if (*body_start != '\0') {
                req->body = strdup(body_start);
            }
        }
    }
    
    return req;
}

// 释放HttpRequest结构体
void free_http_request(HttpRequest* req) {
    if (req != NULL) {
        if (req->body != NULL) {
            free(req->body);
        }
        free(req);
    }
}

// // 主函数测试
// int main() {
//     // 示例HTTP POST请求（简化版）
//     const char* http_post_request = 
//         "POST /api/data HTTP/1.1\r\n"
//         "Host: example.com\r\n"
//         "Content-Type: application/json\r\n"
//         "Content-Length: 28\r\n"
//         "\r\n"
//         "{\"key\": \"value\", \"id\": 123}";
    
//     HTTPS_DEBUG("=== 方法1: 简单提取请求体 ===\n");
//     char* body1 = extract_http_body(http_post_request);
//     if (body1 != NULL) {
//         HTTPS_DEBUG("请求体: %s\n\n", body1);
//         free(body1);
//     }
    
//     HTTPS_DEBUG("=== 方法2: 使用Content-Length精确提取 ===\n");
//     char* body2 = extract_http_body_with_content_length(http_post_request);
//     if (body2 != NULL) {
//         HTTPS_DEBUG("请求体: %s\n\n", body2);
//         free(body2);
//     }
    
//     HTTPS_DEBUG("=== 方法3: 完整解析HTTP请求 ===\n");
//     HttpRequest* req = parse_http_request(http_post_request);
//     if (req != NULL) {
//         HTTPS_DEBUG("方法: %s\n", req->method);
//         HTTPS_DEBUG("URL: %s\n", req->url);
//         HTTPS_DEBUG("头部:\n%s\n", req->headers);
//         HTTPS_DEBUG("请求体: %s\n", req->body ? req->body : "(空)");
//         free_http_request(req);
//     }
    
//     // 测试你提供的原始数据（十六进制转换后的字符串）
//     HTTPS_DEBUG("\n=== 处理你的数据（需要先转换为字符串） ===\n");
//     // 注意：你需要先将十六进制数据转换为字符串
//     // 这里假设你已经有了完整的HTTP请求字符串
    
//     return 0;
// }


/*********************************************************************
 * @fn      strFind
 *
 * @brief   query for a specific string.
 *
 * @param   str  - source string.
 *          substr - String to be queried.
 *
 * @return  The number of data segments contained in the received data
 */
int strFind( char str[], char substr[] )
{
    int i, j, check ,count = 0;
    int len = strlen( str );
    int sublen = strlen( substr );
    for( i = 0; i < len; i++ )
    {
        check = 1;
        for( j = 0; j + i < len && j < sublen; j++ )
        {
            if( str[i + j] != substr[j] )
            {
                check = 0;
                break;
            }
        }
        if( check == 1 )
        {
            count++;
            i = i + sublen;
        }
    }
    return count;
}

/*********************************************************************
 * @fn      Web_Server
 *
 * @brief   web process function.
 *
 * @return  none
 */
void Web_Server(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *socket_buffer,uint32_t lend )
{

    uint8_t reqnum = 0;
    u8 current_page = 0xFF; // 当前页面id

    // 参数验证
    if (socket_buffer == NULL) {
        return ;
    }

    // for(int i=0;i<lend;i++)
    // {
    //      HTTPS_DEBUG("%C",socket_buffer[i]);
    // }
    // HTTPS_DEBUG("\r\n" );

    // 更新网页状态
    Web_Page_State[Sour_Sock].connected = 1;
    Web_Page_State[Sour_Sock].Sour_Sock = Sour_Sock;
    Web_Page_State[Sour_Sock].Dest_Sock = Dest_Sock;
    Web_Page_State[Sour_Sock].tx_busy = 0;
    Web_Page_State[Sour_Sock].rx_complete = 0;
    Web_Page_State[Sour_Sock].timestamp = Html_time_get();   // 获取当前HTML时间计数器值

    reqnum = strFind(socket_buffer,"GET") + strFind(socket_buffer,"get") + \
             strFind(socket_buffer,"POST") + strFind(socket_buffer,"post");
    //HTTPS_DEBUG("Web_ServerPOST : reqnum = %d \r\n",reqnum);
    while(reqnum)
    {
        reqnum--;
        // 解析HTTP请求报文，提取请求方法（GET/POST）和URL
        ParseHttpRequest(&http_request, socket_buffer);
        HTTPS_DEBUG(" TYPE = %d ,METHOD= %d \r\n",http_request.TYPE,http_request.METHOD);
        switch (http_request.METHOD)
        {
            case METHOD_ERR:
                HTTPS_DEBUG("METHOD_ERR\r\n");
                break;

            case METHOD_POST:                                       //'post' request
            {
                name = http_request.URL;
                ParseURLType(&http_request.TYPE, name);
                HTTPS_DEBUG("POST \r\n");
                /* 三菱FX3U-ENET-ADP HTTP页面 - POST请求处理 */
                if(strstr(name, "fx_status.html") != NULL || strstr(name, "fx_status") != NULL) {
                    HTTPS_HandleMonitorCmd((char*)socket_buffer);
                    FX_STATUS_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    current_page = HTML_PAGE_STATUS ;  /* 标记页面已处理 */
                }
                else if(strstr(name, "fx_plcinf.html") != NULL || strstr(name, "fx_plcinf") != NULL) {
                    HTTPS_HandleMonitorCmd((char*)socket_buffer);   /* 监视开始/停止按钮 -> 切换监视状态 */
                    FX_PLCINF_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    current_page = HTML_PAGE_PLCINF;  /* 标记页面已处理 */
                }
                else if(strstr(name, "fx_enetinf.html") != NULL || strstr(name, "fx_enetinf") != NULL) {
                    HTTPS_HandleMonitorCmd((char*)socket_buffer);
                    FX_ENETINF_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    current_page = HTML_PAGE_ENETINF;  /* 标记页面已处理 */
                }
                else if(strstr(name, "fx_acclog.html") != NULL || strstr(name, "fx_acclog") != NULL) {
                    FX_ACCLOG_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    current_page = HTML_PAGE_ACCLOG;  /* 标记页面已处理 */
                }
                else if(strstr(name, "fx_devmon.html") != NULL || strstr(name, "fx_devmon") != NULL) {
                    //MONT=D&DEVT=D&DEVN=0&CMD=%BC%E0%CA%D3%BF%AA%CA%BC&MDL=0&BFMN=0&BFMV=10%BD%F8%D6%C6&INT=5&DISP=16&VAL=D&FORM=WD&BITO=F
                    FX_DEVMON_METHOD_POST(Sour_Sock,Dest_Sock, (char*)socket_buffer);
                    FX_DEVMON_SendWebPage(Sour_Sock, Dest_Sock, (char*)name, 1);   /* 表单提交后渲染新配置 */
           
                    current_page = HTML_PAGE_DEVMON;  /* 标记页面已处理 */
                }
                Web_Page_State[Sour_Sock].current_page = current_page; 
                HTTPS_DEBUG("Sour_Sock =%d, current_page=%d ,timestamp = %u\r\n",Sour_Sock,current_page,Web_Page_State[Sour_Sock].timestamp);
                /* Analyze the requested resource type and return the response */
                /* 如果current_page为0，说明页面未找到或未处理，返回错误 */
                if ( current_page == 0xFF ) {
                    /* 发送404 Not Found响应 */
                    SendHttpResponse(Dest_Sock, PTYPE_HTML, RES_404HEAD_OK, 43);
                    HTTPS_DEBUG("404 Not Found: URL=%s\r\n", name);
                }
                /*After the request is processed, the current
                 * socket connection is closed, and a new connection
                 * will be established when the browser sends the next
                 * request.*/
                //WCHNET_SocketClose(Dest_Sock, TCP_CLOSE_NORMAL);

                /* 收尾：处于 chunked 流模式时补发结束块；若走 404 的
                 * Content-Length 路径，本调用自动无操作 */
                SX_End(Dest_Sock, current_page);
                break;
            }
            case METHOD_GET:                                        //'get' request
            {
                name = http_request.URL;
                ParseURLType(&http_request.TYPE, name);

                HTTPS_DEBUG("GET TYPE=%d, URL: %s \r\n",http_request.TYPE,name);

                /* 三菱FX3U-ENET-ADP HTTP页面 */
                if( strstr(name, "fx_status") != NULL) {
                    FX_STATUS_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    HTTPS_DEBUG("fx_status.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_STATUS ;  /* 标记页面已处理 */
                }
                else if( strstr(name, "fx_plcinf") != NULL) {
                    FX_PLCINF_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    HTTPS_DEBUG("fx_plcinf.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_PLCINF;  /* 标记页面已处理 */
                }
                else if( strstr(name, "fx_enetinf") != NULL) {
                    FX_ENETINF_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    HTTPS_DEBUG("fx_enetinf.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_ENETINF;  /* 标记页面已处理 */
                }
                else if( strstr(name, "fx_acclog") != NULL) {
                    FX_ACCLOG_SendWebPage(Sour_Sock,Dest_Sock, (char*)name);
                    HTTPS_DEBUG("fx_acclog.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_ACCLOG;  /* 标记页面已处理 */
                }
                else if( strstr(name, "fx_devmon") != NULL) {
                    /* 浏览器路径：直接渲染(fetch_data=1)，渲染内部会发起一次批量读；
                     * 回帧到达后由 Web_Usart_Handler 用 fetch_data=0 再渲染一次以显示新数据。 */
                    FX_DEVMON_SendWebPage(Sour_Sock, Dest_Sock, (char *)name,
                                          1);
                    HTTPS_DEBUG("fx_devmon.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_DEVMON ;  /* 标记页面已处理 */
                }
                else if( strstr(name, "index") != NULL ||  strstr(name, "HTTP") != NULL) {
                    FX_index_SendWebPage(Dest_Sock, (char*)name);
                    HTTPS_DEBUG("index.html 流式发送完成\r\n");
                    current_page = HTML_PAGE_INDEX;  /* 标记页面已处理 */
                }
                Web_Page_State[Sour_Sock].current_page = current_page;
                HTTPS_DEBUG("Sour_Sock =%dcurrent_page=%d ,timestamp = %u\r\n",Sour_Sock,current_page,Web_Page_State[Sour_Sock].timestamp);
                /*Analyze the requested resource type and return the response*/
                /* 如果current_page为0，说明页面未找到或未处理，返回错误 */
                if (current_page == 0xFF) {
                    /* 发送404 Not Found响应 */
                    SendHttpResponse(Dest_Sock, PTYPE_HTML, "<html><body>404 Not Found</body></html>", 43);
                    WCHNET_SocketClose(Dest_Sock, TCP_CLOSE_NORMAL);
                    HTTPS_DEBUG("404 Not Found: URL=%s\r\n", name);
                }

                /* 收尾：补发 chunked 结束块（404 路径自动无操作） */
                SX_End(Dest_Sock, current_page);
                break;
            }
 
        }
    }
    /*After the request is processed, the current
     * socket connection is closed, and a new connection
     * will be established when the browser sends the next
     * request.*/
   // WCHNET_SocketClose(Dest_Sock, TCP_CLOSE_NORMAL);

}

/*********************************************************************
 * @fn      Web_Server
 *
 * @brief   web process function.Web_Usart_Handler
 *
 * @return  none
 */
void Web_Usart_Handler(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *buffer,uint32_t lend )
{
 
    u8 current_page = 0;
    // 参数验证
    if ( buffer == NULL) {
        return ;
    }
    HTTPS_DEBUG("Web_Usart Sour_Sock =%d ,Dest_Sock =%d lend =%d\r\n",Sour_Sock,Dest_Sock,lend);

    // 查询当前Sour_Sock 对应的页面状态
    for(int i = 0;i<4;i++ )
    {
        if(Web_Page_State[i].Sour_Sock == Sour_Sock && Web_Page_State[i].connected ) 
        {
            current_page = Web_Page_State[i].current_page;  //得到当前页面
           
            break;
        }  
    }
    HTTPS_DEBUG("current_page=%d-----",current_page);
    switch(current_page)
    {
        case HTML_PAGE_INDEX:
            HTTPS_DEBUG("index.html \r\n");
            break;
        case HTML_PAGE_STATUS:
            HTTPS_DEBUG("fx_status.html \r\n");
            break;
        case HTML_PAGE_PLCINF:
            HTTPS_DEBUG("fx_plcinf.html \r\n");
            /* ★ M8000~M8015 状态字回帧：解析后刷新 RUN/BATT/ERROR 指示灯。
             * 解码方式与 devmon 分支相同：跳过 STX(buffer+1)，ASCII 十六进制→二进制，
             * 字节数 = (帧长-4)/2。回帧路径只更新 LED 状态、不重发页面
             * (浏览器每 5s 元刷新重新请求本页，届时用新状态渲染)。 */
            if (lend > 4u) {
                /* 独立静态缓冲：不占用 HtmlBuffer(渲染缓冲)，避免回帧撞上页面组包 */
                static uint8_t s_plc_status_data[8];
                uint16_t dlen = (uint16_t)((lend - 4u) / 2u);
                dlen &= 0xFFFEu;                          /* 该转换要求长度为偶数 */
                if (dlen > (uint16_t)sizeof(s_plc_status_data)) {
                    dlen = (uint16_t)sizeof(s_plc_status_data);
                }
                if (dlen >= 2u) {
                    hex_str_to_intlend(buffer + 1, dlen, s_plc_status_data);
                    FX_PLCINF_OnStatusReply(s_plc_status_data, dlen);
                }
            }
            break;
        case HTML_PAGE_ENETINF:
            HTTPS_DEBUG("fx_enetinf.html \r\n");
            break;
        case HTML_PAGE_ACCLOG:
            HTTPS_DEBUG("fx_acclog.html \r\n");
            break;
        case HTML_PAGE_DEVMON :
            HTTPS_DEBUG("fx_devmon.html \r\n");
            /* ★ 关键修复：buffer 是"原始 UART 回帧"(STX + ASCII 十六进制数据 + 校验和 + ETX)，
             * 而 FX_DEVMON_UpdateMonitor_Data 内部按"二进制小端字"解析
             * (fx_devmon.c 的 ..._16BIT_form/32BIT_form/REAL_form/ASCII_form)。
             * 原实现把原始帧直接传进去，数据被当二进制误解，表格永远全 0/空。
             * 这里照 sim_Process_local_machine()(ethernet_app.c:1463) 的既有范式先解码：
             *   跳过 STX(buffer+1)，ASCII→二进制，字节数 = (帧长-4)/2。
             * 解码缓冲复用 HtmlBuffer（紧随其后的 SendWebPage 会重新填充，二者不冲突）。 */
            if (lend > 4u) {
                /* 独立解码缓冲：以前解码进 HtmlBuffer，而它正是页面渲染使用的同一块
                 * 缓冲 -- 回帧若在渲染途中到达，会把已打包好的页面内容覆盖成二进制数据
                 * (浏览器收到乱码后可能中断连接 -> 后续分包发送反复重试 -> 看门狗复位)。 */
                static uint8_t s_plc_data[256];
                uint16_t dlen = (uint16_t)((lend - 4u) / 2u);
                dlen &= 0xFFFEu;                              /* 该转换要求长度为偶数 */
                if (dlen > (uint16_t)sizeof(s_plc_data)) {
                    dlen = (uint16_t)sizeof(s_plc_data);      /* 防超长帧越界 */
                }
                if (dlen >= 2u) {
                    hex_str_to_intlend(buffer + 1, dlen, s_plc_data);
                    FX_DEVMON_UpdateMonitor_Data(s_plc_data, dlen);   /* 与渲染缓冲彻底隔离 */
                }
            }
            /* ★ 回帧路径只更新数据，不再重发页面。
             * 现场日志证实：浏览器请求触发的响应发完(SX_End)后连接随即关闭，
             * 此时再对同一 socket 发第二个响应，WCHNET_SocketSend 恒返回 0，
             * 于是每个分包都要空转 50 次重试(日志: SX_RawSend timeout, remaining=...)，
             * 整页几十个分包会让主循环卡住数秒。
             * 新数据已由上面的 FX_DEVMON_UpdateMonitor_Data() 写入 g_data_rows，
             * 浏览器每 5s 元刷新重新请求本页时会用这份数据渲染，因此无需在此重发。 */
            break;
        default:
            HTTPS_DEBUG("current_page =%d \r\n",current_page);
            break;
    }

    /* 收尾：UART 侧回帧触发的页面发送同样需要补发 chunked 结束块 */
    SX_End(Dest_Sock, current_page);

    HTTPS_DEBUG("\r\ntimestamp_diff= %uu \r\n", Html_time_get() - Web_Page_State[Sour_Sock].timestamp );
 
}
