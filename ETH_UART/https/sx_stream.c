/********************************** (C) COPYRIGHT *******************************
 * File Name          : sx_stream.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/09/13
 * Description        : HTTP 分包流式发送与校验模块实现
 *******************************************************************************/

#include <stdio.h>
#include <string.h>

#include "HTTPS.h"
#include "sx_stream.h"

/* ============================ 模块内部状态 ============================ */

/* 每个 socket 一份流状态（静态分配，无动态内存） */
static sx_stream_t g_sx[WCHNET_MAX_SOCKET_NUM];

/* 全局任务号：每开始一个新页面请求自增，用于识别过期数据 */
static u16 g_sx_tid = 0;

/* 页面类型名，仅用于元信息注释（索引 = html_page_type_t） */
static const char * const g_sx_page_name[] = {
    "index", "devmon", "plcinf", "enetinf", "status", "acclog"
};

#define SX_PAGE_NAME_CNT   ((int)(sizeof(g_sx_page_name) / sizeof(g_sx_page_name[0])))

/* ============================ CRC 实现 ============================ */

/*
 * CRC-16/CCITT-FALSE，多项式 0x1021，初值 0xFFFF，不反转输入输出。
 * 使用位运算而非查表，节省 512 字节 Flash 且 RAM 开销为 0。
 */
u16 SX_CRC16(const u8 *p, u32 n)
{
    u16 crc = 0xFFFF;
    u8  i;

    if (p == NULL) {
        return crc;
    }

    while (n--) {
        crc ^= (u16)(*p++) << 8;
        for (i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (u16)((crc << 1) ^ 0x1021);
            } else {
                crc = (u16)(crc << 1);
            }
        }
    }
    return crc;
}

/*
 * CRC-32 (IEEE 802.3)，多项式 0xEDB88320（反射），初值/收尾由宏控制。
 * 累进调用：crc = SX_CRC32_Update(crc, buf, len); 最终 final = SX_CRC32_FINAL(crc)
 */
u32 SX_CRC32_Update(u32 crc, const u8 *p, u32 n)
{
    u8 i;

    if (p == NULL) {
        return crc;
    }

    while (n--) {
        crc ^= (u32)(*p++);
        for (i = 0; i < 8; i++) {
            /* (0u - (crc & 1)) 生成 0x00000000 或 0xFFFFFFFF 掩码，避免分支 */
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return crc;
}

/* ============================ 内部辅助 ============================ */

/*
 * 发送一个 HTTP chunk，线格式为：
 *     <hex-length>\r\n<payload>\r\n
 * 说明：长度、CRLF 与负载分多次裸发送，避免额外占用栈上的拼接缓冲。
 */
static void SX_SendChunk(u8 sock, const u8 *data, u32 len)
{
    static const u8 CRLF[2] = { '\r', '\n' };
    char hexlen[8];

    /* chunked 规范要求长度为十六进制字符串；长度最大 SX_CHUNK_MAX，32 位足够 */
    sprintf(hexlen, "%X", (unsigned int)len);

    SX_RawSend(sock, (const u8 *)hexlen, (u32)strlen(hexlen));
    SX_RawSend(sock, CRLF, 2);
    SX_RawSend(sock, data, len);
    SX_RawSend(sock, CRLF, 2);
}

/*
 * 节流：每块之后给 TCP 栈留出搬运时间；
 * 每 SX_YIELD_EVERY 块额外让出一次 CPU，使 WCHNET 定时器得到调度。
 */
static void SX_Throttle(sx_stream_t *s)
{
#if (SX_THROTTLE_MS > 0)
    Delay_Ms(SX_THROTTLE_MS);
#endif

    if (++s->since_yield >= SX_YIELD_EVERY) {
        s->since_yield = 0;
#if (SX_YIELD_MS > 0)
        Delay_Ms(SX_YIELD_MS);
#endif
    }
}

/* ============================ 对外接口 ============================ */

u8 SX_Active(u8 sock)
{
    if (sock >= WCHNET_MAX_SOCKET_NUM) {
        return 0;
    }
    return g_sx[sock].active;
}

const sx_stream_t *SX_GetState(u8 sock)
{
    if (sock >= WCHNET_MAX_SOCKET_NUM) {
        return NULL;
    }
    return &g_sx[sock];
}

void SX_Abort(u8 sock)
{
    if (sock >= WCHNET_MAX_SOCKET_NUM) {
        return;
    }
    memset(&g_sx[sock], 0, sizeof(sx_stream_t));
}

void SX_Begin(u8 sock, char type, u8 page_id)
{
    const char *head;

    if (sock >= WCHNET_MAX_SOCKET_NUM) {
        return;
    }

    /* 同一 socket 上若还有未结束的流，先中断，避免两块响应交错 */
    if (g_sx[sock].active) {
        SX_Abort(sock);
    }

    /*
     * 统一使用 chunked 传输编码：
     *   - 无需预先知道页面总长度（页面是流式拼装的）
     *   - 浏览器按 chunk 自行重组，末尾以 0\r\n\r\n 明确结束
     *   - Cache-Control: no-store 避免浏览器缓存动态页面
     */
    if (type == PTYPE_PNG) {
        head = "HTTP/1.1 200 OK\r\n"
               "Content-Type: image/png\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n";
    } else if (type == PTYPE_CSS) {
        head = "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/css\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n";
    } else if (type == PTYPE_GIF) {
        head = "HTTP/1.1 200 OK\r\n"
               "Content-Type: image/gif\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n";
    } else {
        /* 源文件中的中文字符串为 UTF-8，此处必须与之一致，否则中文乱码 */
        head = "HTTP/1.1 200 OK\r\n"
               "Content-Type: text/html; charset=UTF-8\r\n"
               "Transfer-Encoding: chunked\r\n"
               "Cache-Control: no-store\r\n"
               "\r\n";
    }

    /* HTTP 头本身不走 chunk 包装，直接裸发送 */
    SX_RawSend(sock, (const u8 *)head, (u32)strlen(head));

    memset(&g_sx[sock], 0, sizeof(sx_stream_t));
    g_sx[sock].active = 1;
    g_sx[sock].page   = page_id;
    g_sx[sock].tid    = ++g_sx_tid;
    g_sx[sock].crc    = SX_CRC32_INIT;

    HTTPS_DEBUG("SX_Begin sock=%d type=%d tid=%u\r\n", sock, type, g_sx[sock].tid);
}

void SX_Send(u8 sock, const u8 *data, u32 len)
{
    sx_stream_t *s;
    u32 sent = 0;
    u32 chunk;

    if (sock >= WCHNET_MAX_SOCKET_NUM || data == NULL || len == 0) {
        return;
    }

    s = &g_sx[sock];

    /* 未进入流模式（例如 SendHttpResponse 的 Content-Length 路径），退化为裸发送 */
    if (!s->active) {
        SX_RawSend(sock, data, len);
        return;
    }

    /* 累进整页 CRC-32：只统计真实负载，不含 chunk 包装字节 */
    s->crc   = SX_CRC32_Update(s->crc, data, len);
    s->bytes += len;

    /* 按 SX_CHUNK_MAX 切分为多个 chunk，逐块发送并节流 */
    while (sent < len) {
        chunk = len - sent;
        if (chunk > SX_CHUNK_MAX) {
            chunk = SX_CHUNK_MAX;
        }

        SX_SendChunk(sock, data + sent, chunk);
        sent += chunk;
        s->chunks++;

        SX_Throttle(s);
    }
}

void SX_End(u8 sock, u8 page_id)
{
    sx_stream_t *s;
    u32 crc;

    if (sock >= WCHNET_MAX_SOCKET_NUM) {
        return;
    }

    s = &g_sx[sock];

    /* 未处于流模式：本次响应走的是带 Content-Length 的路径，无需结束块 */
    if (!s->active) {
        return;
    }

    if (page_id != SX_PAGE_UNKNOWN) {
        s->page = page_id;
    }

    crc = SX_CRC32_FINAL(s->crc);

#if SX_META_ENABLE
    {
        const char *pname = (s->page < SX_PAGE_NAME_CNT) ? g_sx_page_name[s->page] : "?";
        char meta[112];

        /*
         * 元信息以 HTML 注释形式附在响应末尾（浏览器不渲染），内容为：
         *   PAGE  页面名
         *   TID   本次传输任务号，变化即代表新一次请求
         *   CHUNKS 实际分块数
         *   BYTES  负载总字节数
         *   CRC32  整页 CRC-32，供抓包/AP 侧校验完整性
         */
        sprintf(meta,
                "<!--SX:PAGE=%s;TID=%u;CHUNKS=%u;BYTES=%u;CRC32=%08X-->\r\n",
                pname,
                (unsigned)s->tid,
                (unsigned)s->chunks,
                (unsigned)s->bytes,
                (unsigned)crc);

        SX_SendChunk(sock, (const u8 *)meta, (u32)strlen(meta));
        SX_Throttle(s);
    }
#endif

    /* chunked 结束块 */
    SX_RawSend(sock, (const u8 *)"0\r\n\r\n", 5);

    HTTPS_DEBUG("SX_End sock=%d page=%d chunks=%u bytes=%u crc=%08X\r\n",
                sock, s->page, (unsigned)s->chunks,
                (unsigned)s->bytes, (unsigned)crc);

    memset(s, 0, sizeof(sx_stream_t));
}
