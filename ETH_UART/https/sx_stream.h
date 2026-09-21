/********************************** (C) COPYRIGHT *******************************
 * File Name          : sx_stream.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/09/13
 * Description        : HTTP 分包流式发送与校验模块
 *                      - Transfer-Encoding: chunked 可靠分包
 *                      - 块级 CRC-16 / 整页累进 CRC-32 校验
 *                      - 发送节流，避免 TCP 发送描述符耗尽丢包
 *                      - 任务号(TID)防止多客户端/多请求交错串扰
 *********************************************************************************
 * 设计要点:
 *   1) 浏览器侧零改造：chunked 由 HTTP 协议栈自动重组，CRC 仅作为自检信息携带。
 *   2) 单块负载上限 SX_CHUNK_MAX 必须 < HTML_LEN，防止发送缓冲被 chunk 包装撑破。
 *   3) 每块之后节流 SX_THROTTLE_MS，每 SX_YIELD_EVERY 块额外让出一次 CPU，
 *      保证 WCHNET 的 1ms 定时器 (WCHNET_TimeIsr) 能被喂到。
 *******************************************************************************/

#ifndef __SX_STREAM_H__
#define __SX_STREAM_H__

#include "ch32v30x.h"
#include "debug.h"
#include "net_config.h"

/* ============================ 可调参数 ============================ */

/* 单个 chunk 的最大负载字节数（必须 < HTML_LEN，留足 sprintf 尾巴余量） */
#ifndef SX_CHUNK_MAX
#define SX_CHUNK_MAX            900
#endif

/* 每块之后的最小间隔(ms)：TCP 发送描述符只有 ETH_TXBUFNB 个，不留时间必丢包 */
#ifndef SX_THROTTLE_MS
#define SX_THROTTLE_MS          2
#endif

/* 连续发送多少块后强制让出 CPU 一次 */
#ifndef SX_YIELD_EVERY
#define SX_YIELD_EVERY          16
#endif

/* 让出 CPU 的时间片(ms)，不可过大，否则影响网络心跳与看门狗 */
#ifndef SX_YIELD_MS
#define SX_YIELD_MS             1
#endif

/* 1: 响应末尾追加 <!--SX:...--> 元信息注释（浏览器不渲染），便于抓包定位与前端自检 */
#ifndef SX_META_ENABLE
#define SX_META_ENABLE          1
#endif

/* 页面 id 未知时的占位值 */
#define SX_PAGE_UNKNOWN         0xFF

/* CRC-32 累进计算的初值与收尾 */
#define SX_CRC32_INIT           0xFFFFFFFFu
#define SX_CRC32_FINAL(c)       ((c) ^ 0xFFFFFFFFu)

/* ============================ 数据结构 ============================ */

/* 每个 socket 一份的分包发送状态 */
typedef struct {
    u8  active;         /* 1 = 正在以 chunked 模式发送 */
    u8  page;           /* 页面类型(html_page_type_t)，用于元信息 */
    u16 tid;            /* 任务号：每个新页面请求 +1，用于识别过期数据 */
    u16 chunks;         /* 已发送块数 */
    u16 since_yield;    /* 距上次让出 CPU 已发送的块数 */
    u32 bytes;          /* 已发送负载总字节数（不含 chunk 包装） */
    u32 crc;            /* 整页累进 CRC-32 原始状态（未取反） */
} sx_stream_t;

/* ============================ 对外接口 ============================ */

/**
 * @brief  CRC-16/CCITT-FALSE 校验（零表位运算版，RAM 开销 0）
 * @param  p 数据指针  n 字节数
 * @return 16 位校验值
 */
u16 SX_CRC16(const u8 *p, u32 n);

/**
 * @brief  CRC-32 (IEEE 802.3) 累进更新，零表位运算版
 * @param  crc 当前状态（首次传 SX_CRC32_INIT）
 * @param  p 数据指针  n 字节数
 * @return 更新后的状态；最终值用 SX_CRC32_FINAL() 收尾
 */
u32 SX_CRC32_Update(u32 crc, const u8 *p, u32 n);

/**
 * @brief  裸发送（原 Data_Send 逻辑：重试 + LED 触发），由 HTTPS.c 实现
 * @note   不参与 chunk 包装，仅供内部与未进入流模式的场景使用
 */
void SX_RawSend(u8 id, const u8 *dataptr, u32 datalen);

/**
 * @brief  开始一个 chunked 响应：发送 HTTP 头并使能流模式
 * @param  sock    目标 socket
 * @param  type    内容类型(PTYPE_HTML / PTYPE_CSS / ...)
 * @param  page_id 页面类型，未知可传 SX_PAGE_UNKNOWN（后续由 SX_End 指定）
 */
void SX_Begin(u8 sock, char type, u8 page_id);

/**
 * @brief  流模式下发送负载：自动按 SX_CHUNK_MAX 切分 + chunked 包装 + 节流 + CRC 累进
 * @note   若该 socket 未处于流模式，退化为裸发送
 */
void SX_Send(u8 sock, const u8 *data, u32 len);

/**
 * @brief  结束 chunked 响应：发送元信息注释(可选)与结束块 "0\r\n\r\n"
 * @param  sock    目标 socket
 * @param  page_id 本页类型，用于元信息标注
 * @note   未处于流模式时什么也不做（兼容 SendHttpResponse 的 Content-Length 路径）
 */
void SX_End(u8 sock, u8 page_id);

/**
 * @brief  异常中断当前流（连接断开时调用），清状态且不发结束块
 */
void SX_Abort(u8 sock);

/**
 * @brief  查询是否处于流模式
 * @return 1 处于流模式，0 否
 */
u8 SX_Active(u8 sock);

/**
 * @brief  只读查询某 socket 的流状态（调试用），越界或未启用返回 NULL
 */
const sx_stream_t *SX_GetState(u8 sock);

#endif /* __SX_STREAM_H__ */
