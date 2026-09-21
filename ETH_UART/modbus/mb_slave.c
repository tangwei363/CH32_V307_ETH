/*********************************************************************
 * @file    mb_slave.c
 * @brief   Modbus TCP 从站 → 三菱 MELSEC-FX 串口 网关模块（实现）
 *
 * @note    数据流（与 MC 协议完全复用同一条串口链路）
 *
 *   上位机(Modbus TCP 主站)
 *      │  ①TCP 字节流（支持跨报文段重组 / 单段多条请求）
 *      ▼
 *   MB_Slave_IsFrame()  ──否──? 交回 MC 协议处理
 *      │是
 *      ▼
 *   MB_Slave_HandleRequest()  流重组 → 逐条 mb_process_adu()
 *      │  ②按 g_slave_addr_map[] 解析 Modbus 地址 → 三菱软元件
 *      │  ③复用 melsec_fx 的 MELSEC_FX_Build* 构造 FX 串口命令
 *      ▼
 *   uartTxWithSocketID()  ──?  串口 DMA  ──?  PLC
 *
 *   PLC ──? 串口响应 ──? Analysis_usart_handler()
 *                              │
 *                              ▼
 *                    MB_Slave_HandleSerialResp()  ④还原为 Modbus 响应
 *                              │                     (位批量写非对齐时走 RMW 两阶段)
 *                              ▼
 *                    ethernet_send() ──? 上位机
 *
 * @author  AI Assistant
 * @date    2026-09-14
 *********************************************************************/

#include "mb_slave.h"

#include "ethernet_app.h"        /* eth_socket[] / ethernet_send() / synch_time_get() */
#include "bsp_uart.h"            /* uartTxWithSocketID() / uart_rx_ctx                */
#include "melsec_fx_core.h"      /* MELSEC_FX_Build* / AsciiHexToUint8 / STX / ETX    */
#include "melsec_fx_net.h"       /* net_mc_meta / MC_CMD_* / BitBatch_queue_*         */
#include "melsec_fx_tables.h"    /* MC_FX_* 软元件代码                                 */

#include <string.h>
#include <stdio.h>               /* MB_DEBUG 使用的 printf */

/*=====================================================================
 *                        模块唯一全局上下文
 *===================================================================*/
mb_slave_t g_mb_slave;

/*=====================================================================
 *              Modbus 地址 → 三菱软元件 映射表
 *
 *  依据"三菱 FX3U/FX3UC 可编程控制器 Modbus 软元件对应表"整理。
 *
 *  ── 位软元件区(线圈 SLAVE_AREA_COIL / 离散输入 SLAVE_AREA_DISCRETE) ──
 *    Modbus 地址          位软元件       读写属性
 *    0x0000 ~ 0x1DFF      M0   ~ M7679   线圈读写 / 离散输入只读
 *    0x1E00 ~ 0x1FFF      M8000~ M8511   同上
 *    0x2000 ~ 0x2FFF      S0   ~ S4095   同上
 *    0x3000 ~ 0x31FF      TS0  ~ TS511   同上
 *    0x3200 ~ 0x32FF      CS0  ~ CS255   同上
 *    0x3300 ~ 0x33FF      Y0   ~ Y377    同上
 *    0x3400 ~ 0x34FF      X0   ~ X377    ★仅输入区(只读)
 *
 *  ── 字软元件区(保持寄存器 SLAVE_AREA_HOLDING / 输入寄存器 SLAVE_AREA_INPUT) ──
 *    Modbus 地址          字软元件         说明
 *    0x0000 ~ 0x1F3F      D0    ~ D7999
 *    0x1F40 ~ 0x213F      D8000 ~ D8511
 *    0x2140 ~ 0xA13F      R0    ~ R32767
 *    0xA140 ~ 0xA33F      TN0   ~ TN511
 *    0xA340 ~ 0xA407      CN0   ~ CN199
 *    0xA408 ~ 0xA477      CN200 ~ CN255    32 位计数器(1 个占 2 个寄存器)
 *    0xA478 ~ 0xA657      M0    ~ M7679    位软元件挂在字区(1 寄存器 = 16 位)
 *    0xA658 ~ 0xA677      M8000 ~ M8511    同上
 *    0xA678 ~ 0xA777      S0    ~ S4095    同上
 *    0xA778 ~ 0xA797      TS0   ~ TS511    同上
 *    0xA798 ~ 0xA7A7      CS0   ~ CS255    同上
 *    0xA7A8 ~ 0xA7B7      Y0    ~ Y377     同上
 *    0xA7B8 ~ 0xA7C7      X0    ~ X377     ★仅输入寄存器区(只读)
 *
 *  字段含义: { Modbus起, Modbus止, 软元件代码, 起始软元件编号,
 *              适用区域掩码, 是否位软元件, 每寄存器位数, 每软元件占寄存器数 }
 *
 *  ── 本模块的读写支持矩阵 ──
 *    区域 / Modbus 地址区间            目标软元件        支持              串口指令
 *    线圈      0x0000~0x33FF          M/S/TS/CS/Y       读 + 写           E00 / E7·E8 / E10(+RMW)
 *    离散输入  0x0000~0x34FF          M/S/TS/CS/Y/X     读                E00
 *    保持寄存器 0x0000~0x213F         D                 读 + 写           E00 / E10
 *    保持寄存器 0x2140~0xA13F         R                 读 + 写           E06 / E16 (专用)
 *    保持寄存器 0xA140~0xA33F         TN                读 + 写           E00 / E10
 *    保持寄存器 0xA340~0xA407         CN0~CN199         读 + 写           E00 / E10
 *    保持寄存器 0xA408~0xA477         CN200~CN255(32位)  读 + 写(整软元件)  E00 / E10
 *    保持寄存器 0xA478~0xA7C7         M/S/TS/CS/Y       读 + 写(16位整字)  E00 / E10
 *    输入寄存器 0x0000~0xA7C7         同上(含X)         读                E00 / E06
 *
 *  注意 1: 扩展寄存器 R 在 FX 协议中必须使用**独立的 E06(读) / E16(写) 指令**，
 *          不能与通用 E00 / E10 混用；本模块按 net_mc_meta.device_name == MC_FX_R 区分。
 *  注意 2: X(输入继电器)在官方对应表中**只出现在"输入/输入寄存器"列**，
 *          "线圈/保持寄存器"列为空(只读)，故其区域掩码仅含 DISCRETE / INPUT。
 *===================================================================*/
const SLAVE_ADDR_MAP_T g_slave_addr_map[] =
{
    /* ────────── 位软元件区：线圈(读写) + 离散输入(只读) ────────── */
    { 0x0000, 0x1DFF, MC_FX_M,    0,   SLAVE_AREA_BIT, 1, 1, 1 },   /* M0    ~ M7679  */
    { 0x1E00, 0x1FFF, MC_FX_M,    8000, SLAVE_AREA_BIT, 1, 1, 1 },  /* M8000 ~ M8511  */
    { 0x2000, 0x2FFF, MC_FX_S,    0,   SLAVE_AREA_BIT, 1, 1, 1 },   /* S0    ~ S4095  */
    { 0x3000, 0x31FF, MC_FX_TS,   0,   SLAVE_AREA_BIT, 1, 1, 1 },   /* TS0   ~ TS511  */
    { 0x3200, 0x32FF, MC_FX_CS,   0,   SLAVE_AREA_BIT, 1, 1, 1 },   /* CS0   ~ CS255  */
    { 0x3300, 0x33FF, MC_FX_Y,    0,   SLAVE_AREA_BIT, 1, 1, 1 },   /* Y0    ~ Y377   */
    { 0x3400, 0x34FF, MC_FX_X,    0,   SLAVE_AREA_DISCRETE, 1, 1, 1 }, /* X0 ~ X377(只读) */

    /* ────────── 字软元件区：保持寄存器(读写) + 输入寄存器(只读) ────────── */
    { 0x0000, 0x1F3F, MC_FX_D,    0,   SLAVE_AREA_WORD, 0, 0,          1 }, /* D0    ~ D7999  */
    { 0x1F40, 0x213F, MC_FX_D,    8000, SLAVE_AREA_WORD, 0, 0,          1 }, /* D8000 ~ D8511  */
    { 0x2140, 0xA13F, MC_FX_R,    0,   SLAVE_AREA_WORD, 0, 0,          1 }, /* R0    ~ R32767 */
    { 0xA140, 0xA33F, MC_FX_TN,   0,   SLAVE_AREA_WORD, 0, 0,          1 }, /* TN0   ~ TN511  */
    { 0xA340, 0xA407, MC_FX_CN,   0,   SLAVE_AREA_WORD, 0, 0,          1 }, /* CN0   ~ CN199  */
    { 0xA408, 0xA477, MC_FX_CN,   200, SLAVE_AREA_WORD, 0, 0,          2 }, /* CN200 ~ CN255(32位) */
    { 0xA478, 0xA657, MC_FX_M,    0,   SLAVE_AREA_WORD, 1, 16,         1 }, /* M0    ~ M7679  */
    { 0xA658, 0xA677, MC_FX_M,    8000, SLAVE_AREA_WORD, 1, 16,         1 }, /* M8000 ~ M8511  */
    { 0xA678, 0xA777, MC_FX_S,    0,   SLAVE_AREA_WORD, 1, 16,         1 }, /* S0    ~ S4095  */
    { 0xA778, 0xA797, MC_FX_TS,   0,   SLAVE_AREA_WORD, 1, 16,         1 }, /* TS0   ~ TS511  */
    { 0xA798, 0xA7A7, MC_FX_CS,   0,   SLAVE_AREA_WORD, 1, 16,         1 }, /* CS0   ~ CS255  */
    { 0xA7A8, 0xA7B7, MC_FX_Y,    0,   SLAVE_AREA_WORD, 1, 16,         1 }, /* Y0    ~ Y377   */
    { 0xA7B8, 0xA7C7, MC_FX_X,    0,   SLAVE_AREA_INPUT, 1, 16,        1 }, /* X0 ~ X377(只读) */
};

/* 映射表条目数 */
const uint16_t g_slave_addr_map_size =
    (uint16_t)(sizeof(g_slave_addr_map) / sizeof(g_slave_addr_map[0]));

/*=====================================================================
 *                          内部辅助函数
 *===================================================================*/

/**
 * @brief  读取 MBAP 头中的 16 位大端字段
 * @param  p  指向字段首字节
 * @retval 大端解析后的 16 位值
 */
static uint16_t mb_get_u16_be(const uint8_t *p)
{
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

/**
 * @brief  16 位值的 高字节 / 低字节 互换
 * @param  v  原值
 * @retval 互换后的值
 * @note   用于"字区写位软元件"与"位批量写入的整字打包"：
 *         三菱 E10 命令经 Uint16ToAscii 发送时是"高字节先发"，
 *         而位软元件的线序约定为"首字节 = 字内低 8 位设备(偏移 0~7)"，
 *         故需互换字节，使线上首字节恰好对应低 8 位设备。
 *         (与 melsec_fx 的 MC_Net_Process_BitBatchWrite 在 E10 前的互换一致)
 */
static uint16_t mb_swap16(uint16_t v)
{
    return (uint16_t)((v >> 8) | (v << 8));
}

/**
 * @brief  把 Modbus 位数据打包为 FX"字单位写入"所需的 uint16_t 数组
 * @param  dst         输出：字数组(容量须 >= word_count)
 * @param  word_count  输出字数
 * @param  bits        输入：Modbus 位数据(每字节 8 点，低位先行)
 * @param  points      输入：位点数
 * @param  bit_offset  输入：起始软元件相对 8 位对齐读地址的位偏移 (0~7)
 * @retval 无
 * @note   打包规则与 melsec_fx 的 MC_Net_Process_BitBatchWrite 完全一致：
 *           total_pos = bit_offset + n;  word_idx = total_pos >> 4;
 *           bit_in_w  = total_pos & 0x0F;  dst[word_idx] |= 1 << bit_in_w;
 *         区别仅在于 Modbus 位数据是"每字节 8 点(LSB 对应最低地址)"，
 *         而 MC 用的是"每字节 2 点的高/低 nibble"编码。
 *         末尾对每个字做高/低字节互换以对齐 FX 线序。
 */
static void mb_pack_bit_words(uint16_t *dst, uint16_t word_count,
                              const uint8_t *bits, uint16_t points, uint16_t bit_offset)
{
    uint16_t n;

    memset(dst, 0, (size_t)word_count * sizeof(uint16_t));

    for (n = 0u; n < points; n++) {
        uint16_t total_pos;

        if (((bits[n >> 3] >> (n & 7u)) & 0x01u) == 0u) {
            continue;                                   /* 该点为 0，保持默认 */
        }
        total_pos = (uint16_t)(bit_offset + n);
        dst[total_pos >> 4] |= (uint16_t)(1u << (total_pos & 0x0Fu));
    }

    for (n = 0u; n < word_count; n++) {
        dst[n] = mb_swap16(dst[n]);
    }
}

/**
 * @brief  把功能码转换为所属 Modbus 区域
 * @param  func  功能码
 * @param  area  输出：区域掩码(SLAVE_AREA_xxx)
 * @retval 0 - 支持的功能码；-1 - 不支持
 */
static int mb_func_to_area(uint8_t func, uint8_t *area)
{
    if (area == NULL) {
        return -1;
    }

    switch (func) {
        case MB_FC_READ_COILS:
        case MB_FC_WRITE_SINGLE_COIL:
        case MB_FC_WRITE_MULTI_COILS:
            *area = SLAVE_AREA_COIL;
            return 0;

        case MB_FC_READ_DISCRETE_INPUTS:
            *area = SLAVE_AREA_DISCRETE;
            return 0;

        case MB_FC_READ_HOLDING_REGS:
        case MB_FC_WRITE_SINGLE_REG:
        case MB_FC_WRITE_MULTI_REGS:
            *area = SLAVE_AREA_HOLDING;
            return 0;

        case MB_FC_READ_INPUT_REGS:
            *area = SLAVE_AREA_INPUT;
            return 0;

        default:
            return -1;
    }
}

/**
 * @brief  判断功能码是否为写操作
 * @param  func  功能码
 * @retval 1 = 写；0 = 读
 */
static uint8_t mb_is_write_func(uint8_t func)
{
    return (uint8_t)(func == MB_FC_WRITE_SINGLE_COIL ||
                     func == MB_FC_WRITE_SINGLE_REG  ||
                     func == MB_FC_WRITE_MULTI_COILS ||
                     func == MB_FC_WRITE_MULTI_REGS);
}

/**
 * @brief  判断功能码是否为"单点写"(其 PDU 第三字段是写入值而非数量)
 * @param  func  功能码
 * @retval 1 = 是(0x05/0x06)；0 = 否
 */
static uint8_t mb_is_single_write(uint8_t func)
{
    return (uint8_t)(func == MB_FC_WRITE_SINGLE_COIL ||
                     func == MB_FC_WRITE_SINGLE_REG);
}

/**
 * @brief  在映射表中查找覆盖 [addr, addr+qty-1] 的条目
 * @param  area  区域掩码(SLAVE_AREA_xxx)
 * @param  addr  Modbus 起始地址
 * @param  qty   数量
 * @retval 命中的条目首指针；未命中返回 NULL
 * @note   边界检查使用 32 位运算，天然规避 addr+qty 的 16 位回绕。
 */
const SLAVE_ADDR_MAP_T *MB_Slave_FindMap(uint8_t area, uint16_t addr, uint16_t qty)
{
    uint16_t i;

    if (qty == 0u) {
        return NULL;
    }

    for (i = 0u; i < g_slave_addr_map_size; i++) {
        const SLAVE_ADDR_MAP_T *e = &g_slave_addr_map[i];

        if ((e->area_mask & area) == 0u) {          /* 区域不符 */
            continue;
        }
        if (addr < e->mb_start) {                   /* 起始地址在区间之前 */
            continue;
        }
        if (((uint32_t)addr + (uint32_t)qty - 1u) > (uint32_t)e->mb_end) {
            continue;                               /* 末端越界 */
        }
        return e;
    }

    return NULL;
}

/**
 * @brief  解析 Modbus TCP 请求帧到结构体
 * @param  req    输出：解析结果
 * @param  frame  输入：请求帧首指针(完整 ADU)
 * @param  len    输入：请求帧长度
 * @retval 0 - 解析成功；-1 - 帧太短/数据区被截断
 * @note   ADU 布局: MBAP[0..6] + func[7] + addr[8..9] + (quantity|value)[10..11] ...
 *         0x05/0x06 的写入值位于整帧偏移 **10~11**(PDU 偏移 3~4)，
 *         0x0F/0x10 的字节数位于偏移 12、数据从偏移 13 起。
 */
static int mb_parse_request(mb_req_t *req, const uint8_t *frame, uint16_t len)
{
    if (req == NULL || frame == NULL || len < 12u) {   /* 读请求最小长度即 12 字节 */
        return -1;
    }

    req->trans_id   = mb_get_u16_be(&frame[0]);
    req->proto_id   = mb_get_u16_be(&frame[2]);
    req->length     = mb_get_u16_be(&frame[4]);
    req->unit_id    = frame[6];
    req->func       = frame[7];
    req->start_addr = mb_get_u16_be(&frame[8]);
    req->quantity   = mb_get_u16_be(&frame[10]);
    req->data       = NULL;
    req->data_len   = 0;

    switch (req->func) {
        case MB_FC_WRITE_SINGLE_COIL:
        case MB_FC_WRITE_SINGLE_REG:
            /* 单点写：值就在偏移 10~11(即 req->quantity 的位置) */
            req->data     = &frame[10];
            req->data_len = 2;
            break;

        case MB_FC_WRITE_MULTI_COILS:
        case MB_FC_WRITE_MULTI_REGS:
            if (len < 13u) {
                return -1;
            }
            req->data_len = frame[12];                 /* 字节数 */
            if ((uint16_t)(13u + req->data_len) > len) {
                return -1;                             /* 数据区被截断 */
            }
            req->data = &frame[13];
            break;

        default:
            break;
    }

    return 0;
}

/**
 * @brief  校验 Modbus 数量是否在该功能码的允许范围内
 * @param  func      功能码
 * @param  quantity  数量
 * @retval MB_EXC_OK 或具体异常码
 */
static uint8_t mb_check_quantity(uint8_t func, uint16_t quantity)
{
    switch (func) {
        case MB_FC_READ_COILS:
        case MB_FC_READ_DISCRETE_INPUTS:
            if (quantity == 0u || quantity > MB_SLAVE_MAX_READ_BITS) {
                return MB_EXC_ILLEGAL_VALUE;
            }
            break;

        case MB_FC_READ_HOLDING_REGS:
        case MB_FC_READ_INPUT_REGS:
            if (quantity == 0u || quantity > MB_SLAVE_MAX_READ_REGS) {
                return MB_EXC_ILLEGAL_VALUE;
            }
            break;

        case MB_FC_WRITE_MULTI_COILS:
            if (quantity == 0u || quantity > MB_SLAVE_MAX_WRITE_COILS) {
                return MB_EXC_ILLEGAL_VALUE;
            }
            break;

        case MB_FC_WRITE_MULTI_REGS:
            if (quantity == 0u || quantity > MB_SLAVE_MAX_WRITE_REGS) {
                return MB_EXC_ILLEGAL_VALUE;
            }
            break;

        default:
            break;
    }

    return MB_EXC_OK;
}

/**
 * @brief  序列化 Modbus TCP 响应（MBAP + PDU）
 * @param  trans_id  事务标识符
 * @param  unit_id   单元标识符
 * @param  pdu       PDU 首指针
 * @param  pdu_len   PDU 长度
 * @param  out       输出缓冲
 * @param  out_size  输出缓冲容量
 * @retval 组装后的总字节数；0 表示容量不足
 */
static uint16_t mb_serialize_adu(uint16_t trans_id, uint8_t unit_id,
                                 const uint8_t *pdu, uint16_t pdu_len,
                                 uint8_t *out, uint16_t out_size)
{
    uint16_t total = (uint16_t)(7u + pdu_len);

    if (out == NULL || pdu == NULL || total > out_size) {
        return 0;
    }

    out[0] = (uint8_t)(trans_id >> 8);      /* 事务标识符(大端) */
    out[1] = (uint8_t)(trans_id & 0xFFu);
    out[2] = 0x00;                          /* 协议标识符固定 0  */
    out[3] = 0x00;
    out[4] = (uint8_t)((pdu_len + 1u) >> 8);/* 长度 = 单元标识 + PDU */
    out[5] = (uint8_t)((pdu_len + 1u) & 0xFFu);
    out[6] = unit_id;                       /* 单元标识符        */

    memcpy(&out[7], pdu, pdu_len);

    return total;
}

/**
 * @brief  响应自检：打印即将回送的 Modbus 响应的类别 / TID / 功能码 / PDU
 * @param  trans_id  事务标识符(原样回送主站的 TID)
 * @param  unit_id   单元标识符
 * @param  pdu       响应 PDU 首指针
 * @param  pdu_len   PDU 长度
 * @retval 无
 * @note   现场排查"响应与请求错配 / 上一笔的响应被主站当成这一笔"时，本行是
 *         网关侧的**权威记录**：本模块回送的 TID 恒等于请求的 TID，故把本行与
 *         主站 RX 日志按时间对齐，即可确认"哪个 TID、哪个功能码、哪类响应被发出"。
 *
 *         类别由功能码推导，无需额外传参：
 *           最高位为 1          → EXC        异常响应(功能码+异常码)
 *           0x05/0x06/0x0F/0x10 → WRITE-ECHO 写响应(回显 功能码+地址+值或数量)
 *           其余                → READ-DATA  读响应(含字节数与数据)
 *
 *         典型误判场景：主站把一条 WRITE-ECHO 当成上一笔读请求的响应 ——
 *         此时本行会显示 func=0F 且类别为 WRITE-ECHO，与主站期望的 0x01(READ-DATA)
 *         一眼可辨，无需再逐字段反推 PDU 结构。
 */
static void mb_trace_response(uint16_t trans_id, uint8_t unit_id,
                              const uint8_t *pdu, uint16_t pdu_len)
{
    char        hex[25];                /* 最多打印 8 字节: 8×3 字符 + 结束符 */
    const char *kind;
    uint16_t    n;
    uint16_t    show;
    uint8_t     func;

    if (pdu == NULL || pdu_len == 0u) {
        return;
    }

    func = pdu[0];
    if ((func & 0x80u) != 0u) {
        kind = "EXC";
    } else if (func == MB_FC_WRITE_SINGLE_COIL || func == MB_FC_WRITE_SINGLE_REG ||
               func == MB_FC_WRITE_MULTI_COILS || func == MB_FC_WRITE_MULTI_REGS) {
        kind = "WRITE-ECHO";
    } else {
        kind = "READ-DATA";
    }

    show = (pdu_len > 8u) ? 8u : pdu_len;
    for (n = 0u; n < show; n++) {
        (void)sprintf(&hex[n * 3u], "%02X ", pdu[n]);
    }

    MB_DEBUG("MB 响应自检[%s] TID=%04X unit=%02X func=%02X pdu_len=%u pdu=%s%s\r\n",
             kind, (unsigned int)trans_id, unit_id, func, pdu_len, hex,
             (show < pdu_len) ? "..." : "");
}

/**
 * @brief  向指定 socket 回送一帧 Modbus TCP 响应
 * @param  sock       目标以太网 socket
 * @param  dest_sock  串口链路的目的 socket(与请求一致)
 * @param  trans_id   事务标识符
 * @param  unit_id    单元标识符
 * @param  pdu        PDU 首指针
 * @param  pdu_len    PDU 长度
 * @retval 0 - 成功；-1 - 失败(容量不足或 socket 非法)
 */
static int mb_send_response(uint8_t sock, uint8_t dest_sock,
                            uint16_t trans_id, uint8_t unit_id,
                            const uint8_t *pdu, uint16_t pdu_len)
{
    uint8_t  adu[MB_SLAVE_ADU_MAX];
    uint16_t adu_len;

    if (sock >= WCHNET_MAX_SOCKET_NUM || pdu == NULL) {
        return -1;
    }

    adu_len = mb_serialize_adu(trans_id, unit_id, pdu, pdu_len, adu, sizeof(adu));
    if (adu_len == 0) {
        return -1;
    }

    /* 响应自检：异常响应与写回显都经由本函数回送 */
    mb_trace_response(trans_id, unit_id, pdu, pdu_len);

    /* 复用工程既有的以太网发送接口(destip/destport 取自连接表) */
    ethernet_send(sock, dest_sock, adu, adu_len,
                  ETH_S(sock).destip, ETH_S(sock).destport);
    return 0;
}

/**
 * @brief  以"就地 PDU"方式回送响应（PDU 已写在 adu[7] 起的位置）
 * @param  sock       目标以太网 socket
 * @param  dest_sock  串口链路目的 socket
 * @param  trans_id   事务标识符
 * @param  unit_id    单元标识符
 * @param  adu        工作缓冲(容量须 >= MB_SLAVE_ADU_MAX)，其中 [7..] 为 PDU
 * @param  pdu_len    PDU 长度
 * @retval 0 - 成功；-1 - 失败
 * @note   使用单一工作缓冲承载整个 ADU，避免在嵌套调用中额外占用栈空间。
 */
static int mb_send_pdu_inplace(uint8_t sock, uint8_t dest_sock,
                               uint16_t trans_id, uint8_t unit_id,
                               uint8_t *adu, uint16_t pdu_len)
{
    uint16_t total = (uint16_t)(7u + pdu_len);

    if (sock >= WCHNET_MAX_SOCKET_NUM || adu == NULL || total > MB_SLAVE_ADU_MAX) {
        return -1;
    }

    adu[0] = (uint8_t)(trans_id >> 8);              /* 事务标识符(大端) */
    adu[1] = (uint8_t)(trans_id & 0xFFu);
    adu[2] = 0x00;                                  /* 协议标识符固定 0  */
    adu[3] = 0x00;
    adu[4] = (uint8_t)((pdu_len + 1u) >> 8);        /* 长度 = 单元标识 + PDU */
    adu[5] = (uint8_t)((pdu_len + 1u) & 0xFFu);
    adu[6] = unit_id;                               /* 单元标识符        */

    /* 响应自检：读数据响应(PDU 位于 adu[7..]) */
    mb_trace_response(trans_id, unit_id, &adu[7], pdu_len);

    ethernet_send(sock, dest_sock, adu, total,
                  ETH_S(sock).destip, ETH_S(sock).destport);
    return 0;
}

/**
 * @brief  回送 Modbus 异常响应
 * @param  sock       目标以太网 socket
 * @param  dest_sock  串口链路目的 socket
 * @param  trans_id   事务标识符
 * @param  unit_id    单元标识符
 * @param  func       原功能码(响应功能码 = func | 0x80)
 * @param  exc        异常码(mb_exc_t)
 * @retval 0 - 成功；-1 - 失败
 */
static int mb_send_exception(uint8_t sock, uint8_t dest_sock,
                             uint16_t trans_id, uint8_t unit_id,
                             uint8_t func, uint8_t exc)
{
    uint8_t pdu[2];

    pdu[0] = (uint8_t)(func | 0x80u);   /* 异常响应：功能码最高位置 1 */
    pdu[1] = exc;

    return mb_send_response(sock, dest_sock, trans_id, unit_id, pdu, sizeof(pdu));
}

/**
 * @brief  回送写操作的正常响应（规范要求：原样回显 功能码+地址+数量或写入值）
 * @param  sock       目标以太网 socket
 * @param  dest_sock  串口链路目的 socket
 * @param  trans      在途事务(承载待回显字段)
 * @retval 无
 */
static void mb_send_write_echo(uint8_t sock, uint8_t dest_sock, const mb_trans_t *trans)
{
    uint8_t pdu[5];

    pdu[0] = trans->func;
    pdu[1] = (uint8_t)(trans->start_addr >> 8);
    pdu[2] = (uint8_t)(trans->start_addr & 0xFFu);
    pdu[3] = (uint8_t)(trans->quantity >> 8);
    pdu[4] = (uint8_t)(trans->quantity & 0xFFu);

    mb_send_response(sock, dest_sock, trans->trans_id, trans->unit_id, pdu, sizeof(pdu));
}

/**
 * @brief  按软元件类型选择"字写入"指令并下发串口
 * @param  Sour_Sock  源 socket
 * @param  Dest_Sock  目的 socket
 * @param  dev_code   三菱软元件代码(MC_FX_*)
 * @param  dev_index  起始软元件编号
 * @param  words      待写入的 16 位字数组(主机序)
 * @param  word_count 字数
 * @retval 无
 * @note   R(扩展寄存器)在 FX 协议中必须使用**专用 E16 指令**，不能与通用指令混用；
 *         其余软元件(D/TN/CN 及字区中的位软元件)使用通用 E10 指令。
 *         选路依据与 melsec_fx 的 MC_Net_binary_ParseRecvResp 完全一致。
 */
static void mb_build_word_write(uint8_t Sour_Sock, uint8_t Dest_Sock, uint16_t dev_code,
                                uint16_t dev_index, const uint16_t *words, uint16_t word_count)
{
    net_mc_meta.sub_header = MC_CMD_WORD_BATCH_WRITE;

    if (dev_code == MC_FX_R) {
        /* 扩展寄存器 R：专用 E16 指令(无表映射，地址直传，内部按 address*2 换算) */
        MELSEC_FX_Build_E16_write_R_Cmd(Sour_Sock, Dest_Sock, dev_index, words, word_count);
    } else {
        /* 通用 E10 指令(经 FX_E0_E1_Tables 查表映射) */
        MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock, dev_index, words, word_count);
    }
}

/**
 * @brief  按软元件类型选择"字读取"指令并下发串口
 * @param  Sour_Sock  源 socket
 * @param  Dest_Sock  目的 socket
 * @param  dev_code   三菱软元件代码(MC_FX_*)
 * @param  dev_index  起始软元件编号
 * @param  dev_count  读取字数
 * @retval 无
 * @note   R(扩展寄存器)使用**专用 E06 指令**，其余软元件使用通用 E00 指令。
 */
static void mb_build_word_read(uint8_t Sour_Sock, uint8_t Dest_Sock, uint16_t dev_code,
                               uint16_t dev_index, uint16_t dev_count)
{
    net_mc_meta.sub_header = MC_CMD_WORD_BATCH_READ;

    if (dev_code == MC_FX_R) {
        MELSEC_FX_BuildE06ReadCmd(Sour_Sock, Dest_Sock, dev_index, dev_count);
    } else {
        MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock, dev_index, dev_count);
    }
}

/**
 * @brief  下发 E00 位读取命令（入参以"字节数"表达）
 * @param  Sour_Sock   源 socket
 * @param  Dest_Sock   目的 socket
 * @param  dev_index   起始软元件编号
 * @param  byte_count  期望 PLC 返回的字节数(1 字节 = 8 个位点)
 * @retval 无
 * @note   MELSEC_FX_BuildExxReadCmd 的 length 参数单位是**字数(每字 2 字节)**，
 *         帧内写入的是 length*2 作为"字节数"，且该字段仅有 1 字节宽度。
 *         因此必须把字节数换算为字数上传：
 *             length = ceil(byte_count / 2)  →  帧内字节数 = 2*ceil(byte_count/2) >= byte_count
 *         若直接把"字节数"当 length 传入(MC 路径当前做法)，会造成两方面问题：
 *           ① PLC 被要求多读一倍数据(250 点需 32 字节，实际下发 64)；
 *           ② 字节数 > 127 时 (uint8_t)(length*2) 溢出截断，
 *              例如 2000 点需 250 字节 → 下发 244 → 返回数据不足，解析失败。
 */
static void mb_build_bit_read(uint8_t Sour_Sock, uint8_t Dest_Sock,
                              uint16_t dev_index, uint16_t byte_count)
{
    net_mc_meta.sub_header = MC_CMD_BIT_BATCH_READ;
    MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock, dev_index,
                              (uint16_t)((byte_count + 1u) / 2u));
}

/**
 * @brief  结束在途事务
 * @param  trans  事务上下文
 * @retval 无
 */
static void mb_trans_end(mb_trans_t *trans)
{
    trans->busy       = 0u;
    trans->stage      = MB_STAGE_NORMAL;
    trans->map        = NULL;
    trans->uart_seq   = 0u;
    trans->superseded = 0u;
}

/**
 * @brief  标记某 socket 已确认为 Modbus 会话
 * @param  sock  以太网 socket 编号
 * @retval 无
 * @note   仅在解析出"合法 MBAP + 受支持功能码"后调用。这样可避免把
 *         MC 二进制帧(监视定时器恰为 0 时其偏移 2~3 也是 00 00)误标记为
 *         Modbus，从而错误阻断 MC 路径。
 */
static void mb_sock_mark_modbus(uint8_t sock)
{
    if (sock < (uint8_t)WCHNET_MAX_SOCKET_NUM && sock < 16u) {
        g_mb_slave.modbus_sock_mask |= (uint16_t)(1u << sock);
    }
}

/**
 * @brief  查询某 socket 是否为已确认的 Modbus 会话
 * @param  sock  以太网 socket 编号
 * @retval 1 = 是；0 = 否
 */
static uint8_t mb_sock_is_modbus(uint8_t sock)
{
    if (sock >= (uint8_t)WCHNET_MAX_SOCKET_NUM || sock >= 16u) {
        return 0u;
    }
    return (uint8_t)((g_mb_slave.modbus_sock_mask >> sock) & 1u);
}

/**
 * @brief  选取 Modbus 响应的发送 socket（合法性兜底）
 * @param  trans  当前在途事务
 * @retval 合法的以太网 socket 号
 * @note   【为什么必须兜底】串口链路由 MC 与 Modbus 共用，周期性内部上报
 *         (wizchip_*_to_PLC → MELSEC_FX_BuildEEWriteCmd(0xFF, 0xFF, ...))
 *         会把 uart_rx_ctx 的 Sour/Dest 覆盖为 0xFF(255)。
 *         若把该值当 socket 传给 WCHNET_SocketSend()，会越界索引其内部连接表，
 *         访问到 64KB SRAM 之外的地址并触发 Load access fault ——
 *         现场实测即 HardFault(mcause=5, mtval=0x20011d48)。
 *         因此响应一律回到"发起本次请求的 socket"(trans->sock)。
 */
static uint8_t mb_pick_dest_sock(const mb_trans_t *trans)
{
    if (uart_rx_ctx.Dest_Sockid < (uint8_t)WCHNET_MAX_SOCKET_NUM) {
        return uart_rx_ctx.Dest_Sockid;
    }
    return trans->sock;
}

/*=====================================================================
 *                          对外接口实现
 *===================================================================*/

/**
 * @brief  初始化 Modbus 从站模块
 */
void MB_Slave_Init(void)
{
    memset(&g_mb_slave, 0, sizeof(g_mb_slave));
}

/**
 * @brief  查询某个 socket 是否存在在途 Modbus 事务
 */
uint8_t MB_Slave_IsPending(uint8_t sock)
{
    return (uint8_t)(g_mb_slave.trans.busy && g_mb_slave.trans.sock == sock);
}

/**
 * @brief  查询某个 socket 是否已被确认为 Modbus 会话
 * @param  sock  以太网 socket 编号
 * @retval 1 = 是；0 = 否
 * @note   直接复用内部会话位图，供 MC 协议层抑制"越权回包"。
 */
uint8_t MB_Slave_IsModbusSock(uint8_t sock)
{
    return mb_sock_is_modbus(sock);
}

/**
 * @brief  判断一帧以太网数据是否为 Modbus TCP 数据
 * @note   MBAP 协议标识符为 0 是 Modbus TCP 最强、最稳定的特征。
 *         MB_SLAVE_ACCEPT_ANY_FC == 1(默认)：不再限制功能码，未实现的功能码
 *         由 mb_process_adu() 统一回异常码 0x01 —— 符合 MODBUS 规范。
 *         == 0：额外要求功能码在支持列表内，最大限度降低与 MC 二进制帧的歧义。
 */
uint8_t MB_Slave_IsFrame(const uint8_t *frame, uint16_t len)
{
    if (frame == NULL || len < 4u) {
        return 0u;
    }

    /* 协议标识符必须为 0 */
    if (mb_get_u16_be(&frame[2]) != 0x0000u) {
        return 0u;
    }

#if (MB_SLAVE_ACCEPT_ANY_FC == 0u)
    /* 兼容模式：要求功能码落在支持列表内(歧义窗口最小，但不符合规范) */
    if (len < 8u) {
        return 0u;
    }
    switch (frame[7]) {
        case MB_FC_READ_COILS:
        case MB_FC_READ_DISCRETE_INPUTS:
        case MB_FC_READ_HOLDING_REGS:
        case MB_FC_READ_INPUT_REGS:
        case MB_FC_WRITE_SINGLE_COIL:
        case MB_FC_WRITE_SINGLE_REG:
        case MB_FC_WRITE_MULTI_COILS:
        case MB_FC_WRITE_MULTI_REGS:
            break;
        default:
            return 0u;
    }
#endif

    return 1u;
}

/**
 * @brief  处理一条完整的 Modbus TCP ADU（内部函数）
 * @param  Sour_Sock  源 socket
 * @param  Dest_Sock  目的 socket
 * @param  frame      完整 ADU 首指针
 * @param  len        ADU 长度
 * @retval 0 - 已接管；-1 - 未接管(应回落到 MC 处理)
 * @note   判定顺序遵循 MODBUS 规范推荐：
 *         功能码(0x01) → 数据地址(0x02) → 数据值(0x03) → 从站故障(0x04)
 *         其中"数量 == 0"因会使地址区间退化，单独提前拦截为 0x03。
 */
static int mb_process_adu(uint8_t Sour_Sock, uint8_t Dest_Sock,
                          uint8_t *frame, uint16_t len)
{
    mb_req_t                req;
    mb_trans_t             *trans = &g_mb_slave.trans;
    const SLAVE_ADDR_MAP_T *map;
    uint16_t                offset;
    uint16_t                dev_index;
    uint16_t                dev_count;      /* 下发给串口命令的"软元件数/字节数" */
    uint16_t                dev_points;     /* 需要访问的位点数或字数              */
    uint16_t                qty;            /* 归一化"数量": 0x05/0x06 该字段位置存的是
                                             * 写入值，须按 1 点参与地址校验与点数推导 */
    uint8_t                 area;
    uint8_t                 stage = MB_STAGE_NORMAL;
    uint8_t                 exc;
    uint32_t                now;

    if (mb_parse_request(&req, frame, len) != 0) {
        /* 连 MBAP 都解析不出来：静默丢弃，不回响应(避免向陌生人回包) */
        return 0;
    }

    /* ① 单元标识过滤(0 表示接受任意单元标识)
     *    非本从站地址：不回响应(Modbus TCP 惯例：由 TCP 层静默丢弃) */
    if (MB_SLAVE_UNIT_ID != 0u && req.unit_id != (uint8_t)MB_SLAVE_UNIT_ID) {
        return 0;
    }

    /* ② 单笔在途事务模型：忙时回"从站设备忙"，不覆盖旧事务 */
    if (trans->busy) {
        now = synch_time_get();
        if ((now - trans->tick) < MB_SLAVE_TIMEOUT_MS) {
            MB_DEBUG("MB 拒绝(BUSY) TID=%04X func=%02X: 在途 TID=%04X 已占 %ums\r\n",
                     (unsigned int)req.trans_id, req.func,
                     (unsigned int)trans->trans_id,
                     (unsigned int)(now - trans->tick));
#if (MB_SLAVE_DROP_SUPERSEDED == 1)
            /* 主站用"另一个 TID"继续轮询 → 它已放弃在途事务。
             * 该事务的串口响应稍后到达时会带着过期 TID，必须丢弃(见 HandleSerialResp)。 */
            if (req.trans_id != trans->trans_id) {
                trans->superseded = 1u;
            }
#endif
            mb_send_exception(Sour_Sock, Dest_Sock,
                              req.trans_id, req.unit_id, req.func, MB_EXC_SERVER_BUSY);
            return 0;
        }
        /* 旧事务已超时：直接作废，接受新请求 */
        mb_trans_end(trans);
    }

    /* ③ 功能码 → 区域(不支持的功能码回 0x01 非法功能) */
    if (mb_func_to_area(req.func, &area) != 0) {
        mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                          req.func, MB_EXC_ILLEGAL_FUNCTION);
        return 0;
    }

    /* 到达此处说明"MBAP 合法 + 功能码受支持" → 该 socket 确认为 Modbus 会话。
     * 之后该 socket 上的串口响应若找不到归属事务，将被直接丢弃而不回落 MC。 */
    mb_sock_mark_modbus(Sour_Sock);

    /* ④ 数量为 0 属非法数据值：会使 [addr, addr+qty-1] 退化，故单独提前拦截。
     *    注意 0x05/0x06 该字段位置是"写入值"，0 是合法值，不参与本判定。 */
    if (!mb_is_single_write(req.func) && req.quantity == 0u) {
        mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                          req.func, MB_EXC_ILLEGAL_VALUE);
        return 0;
    }

    /* ⑤ 归一化"数量"字段
     *    0x05/0x06 的 PDU 是"功能码+地址+值"，不存在数量字段，其 quantity 位置
     *    存放的是**写入值**(见 mb_req_t 注释)。该值只能用于回显，绝不能参与
     *    "按数量推导"的计算，否则：
     *      - 值 0xFF00(ON) 当数量：addr+0xFF00-1 越过任何映射区间末端 → 误回 0x02
     *        (实测: FC05 写线圈 0x3300 → 0x3300+0xFF00-1=0x131FF > 0x33FF)
     *      - 值 0x0000(OFF) 当数量：命中 FindMap 的 qty==0 提前返回 → 同样误回 0x02
     *    即未归一化时 FC05 无论置位/复位都必然失败，FC06 则表现为"写入值越大越易报错"。
     *    故此处统一归一化为 qty，供地址校验、点数推导使用；
     *    而 req.quantity 保持原值不动 —— 写响应需原样回显该值(mb_send_write_echo)。 */
    qty = mb_is_single_write(req.func) ? 1u : req.quantity;

    /* ⑥ 查表：解析 Modbus 地址区间 → 三菱软元件区间(含边界检查)
     *    规范推荐"数据地址"判定先于"数据值"，故置于数量范围校验之前 */
    map = MB_Slave_FindMap(area, req.start_addr, qty);
    if (map == NULL) {
        /* 0x02 成因①：地址区间 [addr, addr+qty-1] 不在映射表覆盖范围内
         * (地址本身越界，或数量跨出了所在档位的末端)。
         * 排查：核对 g_slave_addr_map[] 中该 area 的 mb_start/mb_end。 */
        MB_DEBUG("MB 拒绝(0x02-地址不在映射表) func=%02X area=0x%02X addr=0x%04X qty=%u\r\n",
                 req.func, area, req.start_addr, qty);
        mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                          req.func, MB_EXC_ILLEGAL_ADDRESS);
        return 0;
    }

    /* ⑦ 数量范围校验(非法数据值) —— 同样使用归一化值 */
    exc = mb_check_quantity(req.func, qty);
    if (exc != MB_EXC_OK) {
        mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id, req.func, exc);
        return 0;
    }

    offset = (uint16_t)(req.start_addr - map->mb_start);

    /* 计算起始软元件编号：
     *   位软元件 → 按"位点"步进(位区 1 地址=1 点；字区 1 寄存器=16 点)
     *   字软元件 → 按"字"步进(32 位计数器 1 软元件=2 寄存器) */
    if (map->is_bit) {
        dev_index = (uint16_t)(map->dev_base + (uint16_t)(offset * map->bits_per_reg));
    } else {
        /* 32 位计数器要求请求起点落在软元件边界上 */
        if (map->words_per_dev > 1u && (offset % map->words_per_dev) != 0u) {
            /* 0x02 成因②：32 位软元件(CN200~CN255)占 2 个寄存器，
             * 请求起点落在"半个软元件"上 → 无法定位到完整软元件，属地址语义非法。
             * 排查：起始地址必须为该软元件占位数的整数倍。 */
            MB_DEBUG("MB 拒绝(0x02-32位软元件起点未对齐) func=%02X addr=0x%04X offset=%u words_per_dev=%u\r\n",
                     req.func, req.start_addr, offset, map->words_per_dev);
            mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                              req.func, MB_EXC_ILLEGAL_ADDRESS);
            return 0;
        }
        dev_index = (uint16_t)(map->dev_base + offset / map->words_per_dev);
    }

    /* 计算实际需要访问的软元件点数与串口命令长度
     * (一律基于归一化 qty：单写功能码恒为 1 点，杜绝写入值 0xFF00 被当成 65280 点) */
    if (map->is_bit) {
        dev_points = (uint16_t)(qty * map->bits_per_reg);               /* 位点数 */
        /* 位命令按"字节数"下发，并补偿起始位的 8 位对齐偏移 */
        dev_count  = (uint16_t)(((dev_index % 8u) + dev_points + 7u) / 8u);
    } else {
        dev_points = qty;                                               /* 字数 */
        dev_count  = (uint16_t)((qty + map->words_per_dev - 1u) / map->words_per_dev);
    }

    /* ⑧ 统一设置 MC 上下文：底层 MELSEC_FX_Build* 依赖这些字段查表与构造帧 */
    net_mc_meta.Format_Code  = 0;                   /* 0 = 二进制 */
    net_mc_meta.pc_number    = 0xFF;
    net_mc_meta.device_name  = map->dev_code;
    net_mc_meta.start_device = dev_index;
    net_mc_meta.device_count = dev_count;

    switch (req.func) {
        /* ── 位读：0x01 线圈 / 0x02 离散输入（位区，1 线圈 = 1 点） ──
         * 参照 melsec_fx 的 MC_Net_Process_ReadCommand：
         *   位读取统一走 E00 指令，数据量按"字节数"表达 —— 1 字节 = 8 个位点。
         * 三点说明：
         *   ① MC 侧 device_count == 0 视为 256(原厂特殊处理)；
         *      Modbus 规范规定数量 0 为非法值，故此处回异常码 0x03。
         *   ② 起始位未 8 位对齐时多取 1 字节(补偿 bit_offset)，
         *      使 PLC 返回的载荷覆盖 ceil((offset+点数)/8) 个字节，解析时无需越读。
         *   ③ 由 mb_build_bit_read() 把"字节数"换算为"字数"上传，
         *      修正 MC 路径把字节数直接当 length 传入而导致的多读一倍与字段溢出。 */
        case MB_FC_READ_COILS:
        case MB_FC_READ_DISCRETE_INPUTS:
            mb_build_bit_read(Sour_Sock, Dest_Sock, dev_index, dev_count);
            break;

        /* ── 字读：0x03 保持寄存器 / 0x04 输入寄存器 ── */
        case MB_FC_READ_HOLDING_REGS:
        case MB_FC_READ_INPUT_REGS:
            if (map->is_bit) {
                /* 字区中的位软元件(M/S/TS/CS/Y/X)：按位读出，响应侧按 16 位打包 */
                mb_build_bit_read(Sour_Sock, Dest_Sock, dev_index, dev_count);
            } else {
                /* 字软元件：R 走专用 E06，其余(D/TN/CN)走通用 E00 */
                mb_build_word_read(Sour_Sock, Dest_Sock, map->dev_code, dev_index, dev_count);
            }
            break;

        /* ── 写单个线圈：0x05（0xFF00=ON，0x0000=OFF） ── */
        case MB_FC_WRITE_SINGLE_COIL: {
            uint16_t value = mb_get_u16_be(req.data);

            if (value != 0xFF00u && value != 0x0000u) {
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }
            net_mc_meta.sub_header = MC_CMD_BIT_BATCH_WRITE;
            MELSEC_FX_Build_E7_E8_ForceCmd(Sour_Sock, Dest_Sock, map->dev_code, dev_index,
                                           (uint8_t)(value == 0xFF00u ? 1u : 0u));
            break;
        }

        /* ── 写多个线圈：0x0F ──
         * 指令选路与 melsec_fx 的 MC_Net_Process_BitBatchWrite 完全一致：
         *   分支1 单点            → E7/E8 强制 ON/OFF
         *   分支2 起始16位对齐且结束16位对齐 → E10 整字直接写入(干净路径)
         *   分支3 其它(非对齐)    → E00 读回 → 位掩码合并 → E10 写回(读-改-写)
         * 分支3 复用 melsec_fx 的 g_bitbatch_queue 暂存待写位，
         * 并在串口响应侧调用 MC_Net_BitBatchWrite_RMW_Merge 完成合并，
         * 与 MC 路径共享同一套已验证逻辑，且不新增 SRAM。
         * 注: MC 侧的对齐判据只要求"起始 % 8 == 0"，而 E10 实际要求
         *     "起始 % 16 == 0"，此处收紧为 16 位判据以免落到 E10 的报错分支。 */
        case MB_FC_WRITE_MULTI_COILS: {
            uint8_t  bytes      = (uint8_t)((req.quantity + 7u) / 8u);
            /* 起始位在"16 位读回块"内的偏移。RMW 的读地址与回写地址都以 16 位对齐
             * (见下方 net_mc_meta.start_device = dev_index & ~15u)，故必须按 16 取模；
             * 用 %8 会让回写地址落在 8 而非 16 的倍数上，被 E10 直接拒绝。 */
            uint16_t bit_offset = (uint16_t)(dev_index % 16u);
            uint16_t total_bits = (uint16_t)(bit_offset + req.quantity);
            uint16_t word_count = (uint16_t)((total_bits + 15u) / 16u);

            /* 规范要求 byte_count 精确等于 ceil(quantity/8) */
            if ((uint16_t)req.data_len != (uint16_t)bytes) {
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }
            /* 整字打包缓冲上限 */
            if (word_count > MB_SLAVE_MAX_WRITE_WORDS) {
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }

            net_mc_meta.sub_header = MC_CMD_BIT_BATCH_WRITE;

            /* 分支1：单点写入 → E7/E8 强制 */
            if (req.quantity == 1u) {
                MELSEC_FX_Build_E7_E8_ForceCmd(Sour_Sock, Dest_Sock, map->dev_code,
                                               dev_index, (uint8_t)(req.data[0] & 0x01u));
                break;
            }

            /* 位打包为 uint16_t 字数组(与 MC 侧相同的位映射 + 高低字节互换) */
            mb_pack_bit_words(g_mb_slave.scratch, word_count, req.data, req.quantity, bit_offset);

            if (((dev_index % 16u) == 0u) &&
                ((uint16_t)((dev_index + total_bits) % 16u) == 0u)) {
                /* 分支2：整字对齐 → E10 直接批量写入
                 * 必须检查返回码：命令未下发时若继续等待 ACK 会白等超时并锁死在途模型 */
                int e10_ret = MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock, dev_index,
                                                              g_mb_slave.scratch, word_count);

                if (e10_ret != MELSEC_FX_SUCCESS) {
                    MB_DEBUG("MB 异常(整字批量写未下发, code=%d) TID=%04X addr=0x%04X -> 立即回 %s\r\n",
                             e10_ret, (unsigned int)req.trans_id,
                             (unsigned int)req.start_addr,
                             (e10_ret == MELSEC_FX_ERR_ADDR_RANGE) ? "0x02" : "0x04");
                    mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                      req.func,
                                      (e10_ret == MELSEC_FX_ERR_ADDR_RANGE)
                                          ? MB_EXC_ILLEGAL_ADDRESS : MB_EXC_SLAVE_FAILURE);
                    return 0;
                }
            } else {
                /* 分支3：非对齐 → 读-改-写(两阶段事务) */
                if (word_count > BITBATCH_NODE_DATA_LEN) {
                    /* 超出共享队列单节点容量，无法 RMW */
                    mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                      req.func, MB_EXC_ILLEGAL_VALUE);
                    return 0;
                }
                if (BitBatch_queue_Enqueue(g_mb_slave.scratch, word_count) != 0) {
                    mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                      req.func, MB_EXC_SLAVE_FAILURE);
                    return 0;
                }
                /* E00 读回当前状态。后续 MC_Net_BitBatchWrite_RMW_Merge 取
                 * (uart_mc_meta.start_device & ~7) 作为 E10 回写地址 —— 对"本就是
                 * 16 倍数"的地址，该掩码等效于不变，故这里直接按 16 位对齐下发，
                 * 使 E10 的「字单位写位软元件要求 addr % 16 == 0」校验得以通过。
                 * (原按 & ~7 对齐时，Y8/Y24 等会落在 8 的倍数但非 16 的倍数上 → 被拒) */
                net_mc_meta.start_device = (uint16_t)(dev_index & ~15u);
                net_mc_meta.device_count = word_count;
                MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                                          net_mc_meta.start_device, word_count);
                stage = MB_STAGE_RMW_READ;
            }
            break;
        }

        /* ── 写单个寄存器：0x06 ──
         * 目标既可为字软元件(D/R/TN/CN)，也可为"字区中的位软元件"(M/S/TS/CS/Y)。
         * 后者 1 个寄存器恰为 1 个完整 16 位字，且起始恒为 16 位对齐，
         * 因此可直接用 E10 整字写入，无需读-改-写(RMW)。 */
        case MB_FC_WRITE_SINGLE_REG: {
            uint16_t words[1];

            if (map->words_per_dev > 1u) {
                /* 32 位计数器(CN200~CN255)占 2 个寄存器，
                 * 单寄存器写入只能覆盖一半 → 非法数据值 */
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }
            if (map->is_bit && ((dev_index % 16u) != 0u)) {
                /* 0x02 成因③：字区中的位软元件(M/S/TS/CS/Y)用 E10 整字写，
                 * 起始位必须落在 16 位整字边界上。
                 * 排查：改用字区中 16 位对齐的地址，或改用位区(0x3300 段)寻址。 */
                MB_DEBUG("MB 拒绝(0x02-字区位写入未16位对齐) func=%02X addr=0x%04X dev_index=%u\r\n",
                         req.func, req.start_addr, dev_index);
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_ADDRESS);
                return 0;
            }
            words[0] = mb_get_u16_be(req.data);
            if (map->is_bit) {
                words[0] = mb_swap16(words[0]);     /* 位软元件：对齐线序 */
            }
            /* R 走专用 E16，其余(D/TN/CN/字区位软元件)走通用 E10 */
            mb_build_word_write(Sour_Sock, Dest_Sock, map->dev_code, dev_index, words, 1u);
            break;
        }

        /* ── 写多个寄存器：0x10 ──
         * 支持字软元件批量写入；同样支持"字区中的位软元件"——
         * 连续多个寄存器 = 连续多个完整 16 位字，起始 16 位对齐，
         * 故亦可直接 E10 整字批量写入，无需读-改-写。 */
        case MB_FC_WRITE_MULTI_REGS: {
            uint16_t words[MB_SLAVE_MAX_WRITE_REGS];
            uint16_t i;

            if (map->words_per_dev > 1u && (req.quantity % map->words_per_dev) != 0u) {
                /* 32 位计数器(CN200~CN255)必须按整个软元件成对写入，
                 * 否则会出现"只写半个 32 位值"的未定义结果 → 非法数据值 */
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }
            if (map->is_bit && ((dev_index % 16u) != 0u)) {
                /* 0x02 成因④：同成因③，字区中的位软元件用 E10 整字批量写，
                 * 起始位必须落在 16 位整字边界上。 */
                MB_DEBUG("MB 拒绝(0x02-字区位写入未16位对齐) func=%02X addr=0x%04X dev_index=%u\r\n",
                         req.func, req.start_addr, dev_index);
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_ADDRESS);
                return 0;
            }
            /* 规范要求 byte_count 精确等于 quantity * 2 */
            if ((uint16_t)req.data_len != (uint16_t)(req.quantity * 2u)) {
                mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                                  req.func, MB_EXC_ILLEGAL_VALUE);
                return 0;
            }
            /* Modbus 大端字节 → 主机序 16 位（位软元件需再对齐线序） */
            for (i = 0u; i < req.quantity; i++) {
                uint16_t w = mb_get_u16_be(&req.data[i * 2u]);

                words[i] = map->is_bit ? mb_swap16(w) : w;
            }
            /* R 走专用 E16，其余(D/TN/CN/字区位软元件)走通用 E10 */
            mb_build_word_write(Sour_Sock, Dest_Sock, map->dev_code, dev_index,
                                words, req.quantity);
            break;
        }

        default:
            mb_send_exception(Sour_Sock, Dest_Sock, req.trans_id, req.unit_id,
                              req.func, MB_EXC_ILLEGAL_FUNCTION);
            return 0;
    }

    /* ⑧ 登记在途事务：串口响应到达后据此构造 Modbus 响应 */
    trans->busy       = 1u;
    trans->sock       = Sour_Sock;
    trans->unit_id    = req.unit_id;
    trans->func       = req.func;
    trans->is_write   = mb_is_write_func(req.func);
    trans->stage      = stage;
    trans->trans_id   = req.trans_id;
    trans->start_addr = req.start_addr;
    trans->quantity   = req.quantity;   /* 读=数量；0x05/0x06=写入值(写响应须原样回显，
                                         * 见 mb_send_write_echo，勿改为归一化值) */
    trans->dev_index  = dev_index;
    trans->dev_points = dev_points;
    trans->map        = map;
    trans->tick       = synch_time_get();
    /* 记录本次下发的串口命令序号（上面各 case 已通过 MELSEC_FX_Build* 入队），
     * 串口响应到达时据此做严格配对，杜绝迟到响应被算到本事务 */
    trans->uart_seq   = uartTxGetLastSeq();

    MB_DEBUG("MB 接受 TID=%04X func=%02X qty=%u dev=%u pts=%u uart_seq=%u\r\n",
             (unsigned int)trans->trans_id, trans->func,
             (unsigned int)trans->quantity,
             (unsigned int)trans->dev_index, (unsigned int)trans->dev_points,
             (unsigned int)trans->uart_seq);

    return 0;
}

/**
 * @brief  处理一帧 Modbus TCP 数据（TCP 流重组 + 逐条处理）
 * @note   先追加到模块接收缓冲，再依据 MBAP 长度域切出完整 ADU 逐条处理。
 *         因此支持 PDU 跨报文段、以及单报文段含多条流水线请求。
 *         若长度域非法或缓冲溢出，丢弃整段缓冲，避免"粘住"后续报文。
 */
int MB_Slave_HandleRequest(uint8_t Sour_Sock, uint8_t Dest_Sock,
                           uint8_t *frame, uint16_t len)
{
    mb_slave_t *ctx = &g_mb_slave;

    if (frame == NULL || len == 0u) {
        return 0;
    }

    /* 连接切换：丢弃上一连接的残留半包 */
    if (ctx->rx_len != 0u && ctx->rx_sock != Sour_Sock) {
        ctx->rx_len = 0u;
    }
    ctx->rx_sock = Sour_Sock;

    /* 追加本次收到的字节；溢出则丢弃整段缓冲 */
    if (len > (uint16_t)(MB_SLAVE_RX_MAX - ctx->rx_len)) {
        ctx->rx_len = 0u;
        return 0;
    }
    memcpy(&ctx->rx_buf[ctx->rx_len], frame, len);
    ctx->rx_len = (uint16_t)(ctx->rx_len + len);

    /* 逐条切分并处理 */
    while (ctx->rx_len >= 6u) {
        uint16_t adu_len = (uint16_t)(mb_get_u16_be(&ctx->rx_buf[4]) + 6u);

        if (adu_len < 8u || adu_len > MB_SLAVE_RX_MAX) {
            ctx->rx_len = 0u;                       /* 长度域非法：丢弃整段 */
            break;
        }
        if (ctx->rx_len < adu_len) {
            break;                                  /* PDU 未收全，等待后续报文段 */
        }

        (void)mb_process_adu(Sour_Sock, Dest_Sock, ctx->rx_buf, adu_len);

        /* 移除已处理的 ADU */
        ctx->rx_len = (uint16_t)(ctx->rx_len - adu_len);
        if (ctx->rx_len > 0u) {
            memmove(ctx->rx_buf, &ctx->rx_buf[adu_len], ctx->rx_len);
        }
    }

    return 0;
}

/**
 * @brief  处理 PLC 串口响应，转换成 Modbus TCP 响应回送
 * @note   串口响应帧形态(与 MC 协议一致)：
 *         - 写命令应答 : 单字节 0x06(ACK) / 0x15(NAK)
 *         - 读命令应答 : STX + ASCII十六进制数据 + ETX + 2字节ASCII校验和
 *         位批量写非对齐时，事务分两个串口往返完成：
 *           stage=RMW_READ  : 收到 E00 读回数据 → 合并 → 下发 E10 → stage=RMW_WRITE
 *           stage=RMW_WRITE : 收到 E10 的 ACK → 回 Modbus 写响应
 */
int MB_Slave_HandleSerialResp(uint8_t *buf, uint16_t len)
{
    mb_trans_t *trans = &g_mb_slave.trans;
    uint8_t     Sour_Sock;
    uint8_t     Dest_Sock;

    if (buf == NULL || len == 0u) {
        return -1;
    }
    /* ── 事务归属三级校验 ──
     * ① 无在途事务：若该 socket 已是 Modbus 会话，则本帧是迟到/重复的串口响应，
     *    直接丢弃并返回"已接管"，绝不回落到 MC 路径
     *    (否则会把 MC 二进制帧发给 Modbus 主站)；
     * ② socket 不符：同样按上述规则处理；
     * ③ 串口序号不符：本帧属于更早的串口命令，若继续用于当前事务会导致
     *    "响应 TID 与请求不一致"或数据张冠李戴，必须丢弃。 */
    if (!trans->busy || trans->map == NULL) {
        if (mb_sock_is_modbus(uart_rx_ctx.Sour_Sockid)) {
            MB_DEBUG("MB 丢弃无主串口响应(sock=%u len=%u rx_seq=%u)\r\n",
                     uart_rx_ctx.Sour_Sockid, len,
                     (unsigned int)uart_rx_ctx.eth_seq_num);
            return 0;
        }
        return -1;                          /* 与 Modbus 无关，交回 MC 处理 */
    }
    if (uart_rx_ctx.Sour_Sockid != trans->sock) {
        return mb_sock_is_modbus(uart_rx_ctx.Sour_Sockid) ? 0 : -1;
    }
    if (uart_rx_ctx.eth_seq_num != trans->uart_seq) {
        MB_DEBUG("MB 丢弃错配串口响应(rx_seq=%u != trans_seq=%u, TID=%04X)\r\n",
                 (unsigned int)uart_rx_ctx.eth_seq_num,
                 (unsigned int)trans->uart_seq,
                 (unsigned int)trans->trans_id);
        return 0;
    }

    Sour_Sock = trans->sock;
    Dest_Sock = mb_pick_dest_sock(trans);

    /* 入口自检：补齐 req_func / is_write，明确"接下来会回哪一类响应"
     * (与后面每笔实际回送的"MB 响应自检[...]"配对阅读) */
    MB_DEBUG("MB 串口响应 TID=%04X req_func=%02X is_write=%u stage=%u uart_seq=%u len=%u\r\n",
             (unsigned int)trans->trans_id, trans->func, (unsigned int)trans->is_write,
             trans->stage, (unsigned int)trans->uart_seq, len);

#if (MB_SLAVE_DROP_SUPERSEDED == 1)
    /* 本事务已被主站的新请求取代 → 若按原 TID 回送，主站会把这条迟到帧
     * 与它当前在等的请求配对，表现为"MODBUS TCP 传输标识号对不上"。
     * 此时丢弃并作废事务，让主站对那笔读取重试即可。 */
    if (trans->superseded) {
        MB_DEBUG("MB 丢弃已作废事务的串口响应(在途 TID=%04X 已被主站新请求取代)\r\n",
                 (unsigned int)trans->trans_id);
        mb_trans_end(trans);
        return 0;
    }
#endif

    /* ── 读-改-写 阶段1：E00 读回数据到达 → 合并并下发 E10 写回 ── */
    if (trans->stage == MB_STAGE_RMW_READ) {
        if (buf[0] != STX) {
            mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                              trans->func, MB_EXC_SLAVE_FAILURE);
            mb_trans_end(trans);
            return 0;
        }
        /* 复用 melsec_fx 已验证的合并逻辑：从 BitBatch 队列取出待写位，
         * 与 E00 读回状态按位掩码合并后，内部自动下发 E10 写回命令。
         *
         * ★ 诊断陷阱（现场定位必读）：该函数内部对
         *   MELSEC_FX_BuildE10WriteParamCmd 的返回值**未做传播**。若 E10 因
         *   "起始位未 16 位对齐"等被本地拒绝，此处仍返回 0，本模块无法感知，
         *   表现为"回写命令根本没发出去 → 本事务等不到 ACK → 300ms 后回 0x0B"。
         *   此时唯一的现场特征就是串口层那条打印：
         *     "以字单位指令写入位软元件 address=0xXXXX(N) 需要==16的倍数"
         *   （另一种 0x02 成因④/③是网关侧预检，日志为"MB 拒绝(0x02-…)"，两者不同。） */
        {
            /* bit_offset 取 16 位模，与 0x0F 分支的 %16 及 16 位对齐读地址配套 */
            int rmw_ret = MC_Net_BitBatchWrite_RMW_Merge(&buf[1],
                                                         (uint8_t)(trans->dev_index & 15u),
                                                         trans->quantity);
            if (rmw_ret != MELSEC_FX_SUCCESS) {
                /* E10 回写命令**未能下发**(被 E10 拒绝 / 待写位队列为空)：
                 * 绝不能推进到 RMW_WRITE 阶段去等一个永远不会来的 ACK —— 否则会白等
                 * MB_SLAVE_TIMEOUT_MS 再回 0x0B，期间还锁死单笔在途模型，把后续请求
                 * 全部堵成 0x06。此处立即丢弃本请求已入队的待写位，回一条 Modbus
                 * 异常并结束事务。 */
                uint16_t discard_words = 0u;

                (void)BitBatch_queue_Dequeue(g_mb_slave.scratch, &discard_words);

                MB_DEBUG("MB 异常(读-改-写回写未下发, code=%d) TID=%04X addr=0x%04X -> 立即回 %s\r\n",
                         rmw_ret, (unsigned int)trans->trans_id,
                         (unsigned int)trans->start_addr,
                         (rmw_ret == MELSEC_FX_ERR_ADDR_RANGE) ? "0x02" : "0x04");

                mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                                  trans->func,
                                  (rmw_ret == MELSEC_FX_ERR_ADDR_RANGE)
                                      ? MB_EXC_ILLEGAL_ADDRESS : MB_EXC_SLAVE_FAILURE);
                mb_trans_end(trans);
                return 0;
            }
        }
        trans->stage = MB_STAGE_RMW_WRITE;
        /* 合并函数内部已下发 E10 写回命令 → 刷新本事务的串口序号，
         * 使第二阶段的 ACK 能通过序号配对校验 */
        trans->uart_seq = uartTxGetLastSeq();
        return 0;
    }

    /* ── 读-改-写 阶段2：E10 写回的 ACK 到达 → 回 Modbus 写响应 ── */
    if (trans->stage == MB_STAGE_RMW_WRITE) {
        if (buf[0] != ACK) {
            mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                              trans->func, MB_EXC_SLAVE_FAILURE);
            mb_trans_end(trans);
            return 0;
        }
        mb_send_write_echo(Sour_Sock, Dest_Sock, trans);
        mb_trans_end(trans);
        return 0;
    }

    /* ── 常规阶段 ── */

    /* PLC 明确拒绝(NAK) */
    if (buf[0] == NAK) {
        mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                          trans->func, MB_EXC_SLAVE_FAILURE);
        mb_trans_end(trans);
        return 0;
    }

    /* 写操作 → PLC 只回一个 ACK，Modbus 侧回显请求即可 */
    if (trans->is_write) {
        if (buf[0] != ACK) {
            mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                              trans->func, MB_EXC_SLAVE_FAILURE);
            mb_trans_end(trans);
            return 0;
        }
        mb_send_write_echo(Sour_Sock, Dest_Sock, trans);
        mb_trans_end(trans);
        return 0;
    }

    /* ── 读操作 → 解析 ASCII 数据区并按映射类型重新打包 ── */
    {
        const uint8_t *ascii;
        uint16_t       payload_bytes;
        uint8_t        adu[MB_SLAVE_ADU_MAX];
        uint8_t       *pdu = &adu[7];
        uint16_t       byte_count;

        /* 最小有效读响应 = STX + 数据(至少 1 字节=2 字符) + ETX + 校验和(2) = 6 字节；
         * 长度不足时直接判为从站故障，避免 len-4 下溢后越界解析 */
        if (buf[0] != STX || len < 6u) {
            mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                              trans->func, MB_EXC_SLAVE_FAILURE);
            mb_trans_end(trans);
            return 0;
        }

        /* 帧布局: [0]=STX [1..N]=ASCII数据 [N+1]=ETX [N+2..N+3]=校验和 */
        ascii         = &buf[1];
        payload_bytes = (uint16_t)((len - 4u) / 2u);

        /* ── 3a：位读取（位区线圈/离散输入，或字区中的位软元件） ──
         * 解析方式与 melsec_fx 的 BIT_BATCH_READ 解析保持一致：
         *   ① 每个 ASCII 载荷字节只解码一次(AsciiHexToUint8)，而非逐点重复解码；
         *   ② bit_offset(起始位 % 8) 仅作用于首个载荷字节，其余字节自 bit0 起；
         *   ③ 输出按 Modbus 惯例"低位先行"打包，用移位寄存器一次组装一个输出字节。
         * 位区(1 线圈 = 1 点)与字区位软元件(1 寄存器 = 16 点)共用本流程。 */
        if (trans->map->is_bit) {
            uint16_t remaining  = trans->dev_points;      /* 待提取的位点数 */
            uint16_t src_index  = 0u;                     /* 已消耗载荷字节数 */
            uint16_t out_index  = 0u;                     /* 已输出字节数     */
            uint16_t acc        = 0u;                     /* 位组装寄存器(<16位) */
            uint8_t  acc_bits   = 0u;                     /* 寄存器内有效位数 */
            uint8_t  start_bit  = (uint8_t)(trans->dev_index % 8u);
            uint16_t need_bytes = (uint16_t)((start_bit + trans->dev_points + 7u) / 8u);

            byte_count = (uint16_t)((trans->dev_points + 7u) / 8u);
            if (need_bytes > payload_bytes || byte_count > (uint16_t)(MB_SLAVE_ADU_MAX - 9u)) {
                mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                                  trans->func, MB_EXC_SLAVE_FAILURE);
                mb_trans_end(trans);
                return 0;
            }

            memset(&pdu[2], 0, byte_count);

            while (remaining > 0u) {
                /* 本载荷字节可提供的点数：仅首字节需跳过 start_bit 个前导位 */
                uint8_t  avail = (uint8_t)(8u - start_bit);
                uint16_t take  = (remaining < avail) ? remaining : (uint16_t)avail;
                uint8_t  bin   = AsciiHexToUint8(ascii[src_index * 2u],
                                                 ascii[src_index * 2u + 1u]);

                /* 右移掉前导位后只保留 take 个有效位(低位在先) */
                bin = (uint8_t)((bin >> start_bit) & (uint8_t)((1u << take) - 1u));

                /* 压入组装寄存器：acc_bits 始终 < 8，加上 take(<=8) 后 < 16，不会溢出 */
                acc      = (uint16_t)(acc | ((uint16_t)bin << acc_bits));
                acc_bits = (uint8_t)(acc_bits + (uint8_t)take);

                /* 寄存器攒满 8 位即输出一个字节(低位先行) */
                while (acc_bits >= 8u) {
                    pdu[2u + out_index] = (uint8_t)acc;
                    out_index++;
                    acc      = (uint16_t)(acc >> 8);
                    acc_bits = (uint8_t)(acc_bits - 8u);
                }

                remaining = (uint16_t)(remaining - take);
                src_index++;
                start_bit = 0u;                           /* 仅首字节生效 */
            }

            if (acc_bits > 0u) {                          /* 末尾不足 8 位的余量 */
                pdu[2u + out_index] = (uint8_t)acc;
            }

            pdu[0] = trans->func;
            pdu[1] = (uint8_t)byte_count;

            mb_send_pdu_inplace(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                                adu, (uint16_t)(2u + byte_count));
            mb_trans_end(trans);
            return 0;
        }

        /* ── 3b：字读取（字软元件 D/R/TN/CN） ── */
        byte_count = (uint16_t)(trans->quantity * 2u);
        if (byte_count > payload_bytes || byte_count > (uint16_t)(MB_SLAVE_ADU_MAX - 9u)) {
            mb_send_exception(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                              trans->func, MB_EXC_SLAVE_FAILURE);
            mb_trans_end(trans);
            return 0;
        }

        {
            uint16_t i;

            for (i = 0u; i < trans->quantity; i++) {
                uint8_t b0 = AsciiHexToUint8(ascii[i * 4u],      ascii[i * 4u + 1u]);
                uint8_t b1 = AsciiHexToUint8(ascii[i * 4u + 2u], ascii[i * 4u + 3u]);

#if MB_SLAVE_REG_SWAP
                /* 三菱串口按低字节在前传输 → 交换为 Modbus 大端 */
                pdu[2u + i * 2u]      = b1;
                pdu[2u + i * 2u + 1u] = b0;
#else
                pdu[2u + i * 2u]      = b0;
                pdu[2u + i * 2u + 1u] = b1;
#endif
            }
        }

        pdu[0] = trans->func;
        pdu[1] = (uint8_t)byte_count;

        mb_send_pdu_inplace(Sour_Sock, Dest_Sock, trans->trans_id, trans->unit_id,
                            adu, (uint16_t)(2u + byte_count));
        mb_trans_end(trans);
        return 0;
    }
}

/**
 * @brief  Modbus 从站周期任务：在途事务超时检测
 * @note   串口长时间无响应时，向上位机回送 0x0B(网关目标设备无响应)，
 *         避免主站无限等待(Modbus 主站超时通常更长)。
 *         若正处于读-改-写阶段(已把待写位入队)，需同时把队列项取出丢弃，
 *         否则残留项会被后续事务的合并流程误用。
 */
void MB_Slave_Tick(void)
{
    mb_trans_t *trans = &g_mb_slave.trans;
    uint32_t    now;

    if (!trans->busy) {
        return;
    }

    now = synch_time_get();
    if ((now - trans->tick) < MB_SLAVE_TIMEOUT_MS) {
        return;
    }

#if (MB_SLAVE_DROP_SUPERSEDED == 1)
    /* 主站已改用新事务 → 本事务的 0x0B 同样会带着过期 TID 回给主站，
     * 造成"TID 对不上"；此时静默作废更干净（该笔读取由主站重试）。 */
    if (trans->superseded) {
        MB_DEBUG("MB 静默作废已被取代的事务 TID=%04X\r\n",
                 (unsigned int)trans->trans_id);
        mb_trans_end(trans);
        return;
    }
#endif

    if (trans->stage == MB_STAGE_RMW_READ) {
        /* 丢弃已入队但未消费的待写位(出队到暂存区，内容无需使用) */
        uint16_t discard_words = 0u;
        (void)BitBatch_queue_Dequeue(g_mb_slave.scratch, &discard_words);
    }

    MB_DEBUG("MB 串口超时(%ums): 回 0x0B TID=%04X (事务已占 %ums)\r\n",
             (unsigned int)MB_SLAVE_TIMEOUT_MS,
             (unsigned int)trans->trans_id,
             (unsigned int)(now - trans->tick));

    mb_send_exception(trans->sock, mb_pick_dest_sock(trans),
                      trans->trans_id, trans->unit_id,
                      trans->func, MB_EXC_GATEWAY_TIMEOUT);
    mb_trans_end(trans);
}

/**
 * @brief  串口接收帧错误通知：立即结束在途事务并回异常码 0x0B
 * @param  无
 * @retval 无
 * @note   与 MB_Slave_Tick() 的分工：
 *           Tick 是"等 MB_SLAVE_TIMEOUT_MS 超时后"被动上报；
 *           本函数由串口接收侧在检测到"帧残缺 / 缓冲溢出 / 半帧滞留超时"
 *           时立即调用，把故障上报窗口从 1s 缩短为一次串口往返时间，
 *           避免主站在该窗口内的后续请求被连续回 0x06(从站忙) 而误判网关故障。
 *         无在途事务时直接返回（该次串口异常与 Modbus 路径无关）。
 */
void MB_Slave_NotifyFrameError(void)
{
    mb_trans_t *trans = &g_mb_slave.trans;

    if (!trans->busy) {
        return;
    }

    MB_DEBUG("MB 串口帧异常: 立即回 0x0B TID=%04X\r\n",
             (unsigned int)trans->trans_id);

    /* 读-改-写阶段：丢弃已入队但未消费的待写位，避免污染后续事务 */
    if (trans->stage == MB_STAGE_RMW_READ) {
        uint16_t discard_words = 0u;
        (void)BitBatch_queue_Dequeue(g_mb_slave.scratch, &discard_words);
    }

    mb_send_exception(trans->sock, mb_pick_dest_sock(trans),
                      trans->trans_id, trans->unit_id,
                      trans->func, MB_EXC_GATEWAY_TIMEOUT);
    mb_trans_end(trans);
}
