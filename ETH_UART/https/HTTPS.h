/********************************** (C) COPYRIGHT *******************************
 * File Name          : HTTPS.h
 * Author             : WCH
 * Version            : V1.0.0
 * Date               : 2022/05/31
 * Description        : HTTP related parameters.
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/

#ifndef	__HTTPS_H__
#define	__HTTPS_H__

#include "debug.h"
#include "wchnet.h"
#include "bsp_flash.h"
/* USER CODE END Private defines */
//#define _HTTPS_DEBUG            //日志模块 开关，注释掉将关闭日志输出

#ifdef _HTTPS_DEBUG
    #define HTTPS_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define HTTPS_DEBUG(format, ...)
#endif

/*Address where configuration
 * information is stored*/


#define MAX_URL_SIZE              32
#define HTTP_SERVER_PORT          80

/* HTTP request method*/
#define	METHOD_ERR		          0
#define	METHOD_GET		          1
#define	METHOD_HEAD		          2
#define	METHOD_POST		          3

/* HTTP request URL */
#define	PTYPE_ERR		          0
#define	PTYPE_HTML	              1
#define	PTYPE_PNG		          2
#define	PTYPE_CSS		          3
#define PTYPE_GIF                 4

/*WCHNET communication Mode*/
#define MODE_TCPSERVER            0
#define MODE_TCPCLIENT            1

/* HTML Doc. for ERROR */
#define RES_HTMLHEAD_OK "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nContent-Length:"

#define RES_PNGHEAD_OK  "HTTP/1.1 200 OK\r\nContent-Type: image/png\r\nContent-Length:"
  
#define RES_CSSHEAD_OK  "HTTP/1.1 200 OK\r\nContent-Type: text/css\r\nContent-Length:"

#define RES_GIFHEAD_OK  "HTTP/1.1 200 OK\r\nContent-Type: image/gif\r\nContent-Length:"

#define RES_404HEAD_OK  "<html><body>404 Not Found</body></html>"

#define RES_END "\r\n\r\n"

#define HTML_LEN     1600                         //Maximum size of a single web page(1024→768: 大页面走chunked流式, 此处仅作片段暂存)

typedef struct _st_http_request                 //Browser request information
{
	char	METHOD;					
	char	TYPE;					
	char	URL[MAX_URL_SIZE];
}st_http_request;

typedef struct Para_Tab                         //Configuration information parameter table
{
	char *para;                                 //Configuration item name
	char value[30];                             //Configuration item value
}Parameter;
// <https.h>

/*
 * 配置参数表实例，定义在 bsp_flash.c：
 *     Parameter Para_Basic[4], Para_Port[4], Para_Login[2];
 * 原先缺少 extern 声明，导致 HTTPS.c 的 Init_Para_Tab() 编译报
 * "'Para_Basic' undeclared"，此处补上使定义对使用者可见。
 */
extern Parameter Para_Basic[4];
extern Parameter Para_Port[4];
extern Parameter Para_Login[2];

/* 网页状态结构体 - 用于记录HTTP连接和页面处理状态 */
typedef struct _Web_Page_State
{
    u8  connected;                              // 连接状态标志: 0-未连接, 1-已连接
    u8  current_page;                           // 当前页面类型
    u8  Sour_Sock;                              // 发送端套接字  
    u8  Dest_Sock;                              // 接收端套接字  
    u8  tx_busy;                                // 发送忙标志: 0-空闲, 1-忙碌
    u8  rx_complete;                            // 接收完成标志: 0-未完成, 1-已完成

    u32 timestamp;                              // 时间戳
} Web_Page_State_t;

extern Web_Page_State_t Web_Page_State[4];

extern st_http_request http_request;

extern const u8 Basic_Default[BASIC_CFG_LEN];

extern const u8 Login_Default[LOGIN_CFG_LEN];

extern const u8 Port_Default[PORT_CFG_LEN];

extern char HtmlBuffer[];
 
extern char* GetHtmlBuffer(void);

extern void  ClearHtmlBuffer(void);

extern void Html_time_handler(void);

extern uint32_t Html_time_get(void);

extern void ParseHttpRequest(st_http_request *, char *);

extern void ParseURLType(char *, char *);

extern char *GetURLName(char* url);

extern char *DataLocate(char *buf,char *name);

extern void copy_flash(const char *html, u32 len);

extern void Init_Para_Tab(void) ;

extern void Web_Server(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *socket_buffer,uint32_t lend );
extern void Web_Usart_Handler(uint8_t Sour_Sock, uint8_t Dest_Sock, uint8_t *buffer, uint32_t lend);

extern void Data_Send(u8 id, uint8_t *dataptr, uint32_t datalen);

/* 流式HTTP响应发送接口 - 用于大页面分段发送 */
extern void SendHttpHeader(u8 socket_id, char type);

#endif

char *extract_http_body(const char *http_request);

char *extract_http_body_with_content_length(const char *http_request);
