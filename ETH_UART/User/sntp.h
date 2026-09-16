/*
 * sntp.h
 *
 *  Created on: 2014. 12. 15.
 *      Author: Administrator
 */

#ifndef SNTP_H_
#define SNTP_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "debug.h"
/*
 * @brief Define it for Debug & Monitor DNS processing.
 * @note If defined, it dependens on <stdio.h>
 */


#ifdef _SNTP_DEBUG_
    #define SNTP_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define SNTP_DEBUG(format, ...)
#endif

#define    MAX_SNTP_BUF_SIZE    sizeof(ntpformat)        ///< maximum size of DNS buffer. */

/* for ntpclient */
typedef signed char s_char;
typedef unsigned long long tstamp;
typedef unsigned int tdist;

typedef struct _ntpformat
{
    uint8_t  *dstaddr;        /* 目标服务器地址（本地） */
    char    version;        /* NTP版本号 */
    char    leap;           /* 闰秒指示器 */
    char    mode;           /* NTP工作模式 */
    char    stratum;        /* 时间层级（服务器级别） */
    char    poll;           /* 轮询间隔（以2的幂次方表示） */
    s_char  precision;      /* 时间精度（以秒为单位的2的幂次方） */
    tdist   rootdelay;      /* 到主参考源的往返延迟 */
    tdist   rootdisp;       /* 最大误差（根离散度） */
    char    refid;          /* 参考时钟标识符 */
    tstamp  reftime;        /* 上次更新时间的时间戳 */
    tstamp  org;            /* 原始时间戳（客户端发送时间） */
    tstamp  rec;            /* 接收时间戳（服务器接收时间） */
    tstamp  xmt;            /* 发送时间戳（服务器发送时间） */
} ntpformat;

typedef struct _datetime
{
    uint16_t yy;
    uint8_t mo;
    uint8_t dd;
    uint8_t hh;
    uint8_t mm;
    uint8_t ss;
} datetime;

#define ntp_port            123                     // NTP服务器端口号
#define SECS_PERDAY         86400UL                 // 一天的秒数 = 60*60*24
#define UTC_ADJ_HRS         9                       // SEOUL : GMT+9
#define UTC_ADJ_HRS         9                       // SEOUL : GMT+9
#define EPOCH               1900                    // NTP start year

extern datetime Nowdatetime;

void get_seconds_from_ntp_server(uint8_t *buf, uint16_t idx);
void SNTP_init(uint8_t s, uint8_t *ntp_server, uint8_t tz);
void wizchip_time_result_to_PLC(void);
int8_t process_sntp_response(uint8_t sn, uint8_t *frame_buff, uint16_t frame_len);
int8_t check_scheduled_execution(void);
int8_t execute_retry(uint8_t sn);
int8_t handle_retry_logic(uint8_t sn);
int8_t sntp_run(uint8_t sn);
tstamp changedatetime_to_seconds(void);

void calcdatetime(tstamp seconds);
int8_t Sntp_client_task( uint8_t phy_status );
uint8_t get_ntp_retry_state(void);
void set_ntp_retry_state(uint8_t enable);
#ifdef __cplusplus
}
#endif

#endif /* SNTP_H_ */
