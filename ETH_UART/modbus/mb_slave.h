/*********************************************************************
 * @file    mb_slave.h
 * @brief   Modbus TCP 从站 → 三菱 MELSEC-FX 串口 网关模块（对外接口）
 *
 * @note    模块职责
 *          1) 在同一条"MC 协议"以太网连接上，按帧内容识别 Modbus TCP 请求；
 *          2) 依据 SLAVE_ADDR_MAP_T 映射表把 Modbus 地址解析为三菱软元件，
 *             并复用 melsec_fx 模块构造 MELSEC-FX 串口命令；
 *          3) PLC 串口响应到达后，再转换回 Modbus TCP 响应回送给上位机。
 *
 *          地址映射依据"三菱 FX3U/FX3UC Modbus 软元件对应表"：
 *            - 位软元件区(线圈/离散输入)  0x0000~0x34FF
 *            - 字软元件区(保持/输入寄存器) 0x0000~0xA7C7
 *
 *          设计约束（与本工程既有架构保持一致）
 *          - 与 MC 协议共用同一 TCP 端口，不新增 socket / 监听端口；
 *          - 与 MC 一样采用"单笔在途事务 + 全局上下文"模型；
 *          - 所有状态集中在 mb_slave_t 结构体中，不散落全局变量，便于移植。
 *
 * @author  AI Assistant
 * @date    2026-09-14
 *********************************************************************/
#ifndef __MB_SLAVE_H__
#define __MB_SLAVE_H__

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*=====================================================================
 *                          调试开关
 *  1(默认) = 打印"请求接受 / 请求被拒 / 串口响应"三类诊断，用于定位
 *            "响应 TID 与请求不一致 / 迟到响应错配 / 连收 0x06"类问题。
 *  0      = 关闭，恢复零打印开销。
 *  注：printf 由包含本头文件的 .c 所引入的既有调试头(debug.h)提供。
 *===================================================================*/
#ifndef MB_SLAVE_DEBUG_ON
#define MB_SLAVE_DEBUG_ON           1
#endif

/* ---- 主站已改用新事务时，如何处理旧事务的迟到串口响应 ----
 *  现象：本机单笔在途模型下，某笔事务因串口停顿（内部上报占用链路/PLC 迟答）
 *        迟迟拿不到响应，主站已用"新的 TID"继续轮询（每个新请求都被回 0x06 忙）。
 *        此时旧事务的串口响应才到达 —— 若仍按原 TID 回送，主站会把这条"旧 TID
 *        的数据帧"与它当前在等的请求对不上，现场表现为"MODBUS TCP 传输标识号对不上"。
 *  1(默认) = 丢弃该响应并作废事务（主站看不到错位的 TID，需重试该笔读取）
 *  0      = 仍按原 TID 回送（符合"一问一答"的字面语义，但不适合按 TID 严格配对的主站） */
#ifndef MB_SLAVE_DROP_SUPERSEDED
#define MB_SLAVE_DROP_SUPERSEDED    1
#endif

#if (MB_SLAVE_DEBUG_ON == 1)
    #define MB_DEBUG(format, ...)  printf(format, ##__VA_ARGS__)
#else
    #define MB_DEBUG(format, ...)
#endif

/*=====================================================================
 *                            可移植配置区
 *  移植到其它平台时，只需调整本节的容量 / 超时 / 字节序参数；
 *  地址映射关系见 mb_slave.c 的 g_slave_addr_map[] 数据表。
 *===================================================================*/

/* ---- Modbus 单元标识(从站地址)：0 = 接受任意单元标识 ---- */
#ifndef MB_SLAVE_UNIT_ID
#define MB_SLAVE_UNIT_ID            0u
#endif

/* ---- 数量上限(Modbus 规范上限 与 FX 命令上限 的交集) ---- */
#define MB_SLAVE_MAX_READ_BITS      2000u    /* 0x01/0x02 单次最大读位数(规范 0x07D0) */
#define MB_SLAVE_MAX_READ_REGS      125u     /* 0x03/0x04 单次最大读寄存器数(规范 0x7D) */
#define MB_SLAVE_MAX_WRITE_COILS    1968u    /* 0x0F 单次最大写线圈数(规范 0x07B0)  */
#define MB_SLAVE_MAX_WRITE_REGS     123u     /* 0x10 单次最大写寄存器数(规范 0x7B)  */

/* 位写入"整字打包"缓冲的容量(单位: 16 位字)。
 * 0x0F 走 E10 整字写入时的字数上限 = ceil((7 + 1968)/16) = 124 ≤ 128，故取 128。 */
#define MB_SLAVE_MAX_WRITE_WORDS    128u

/* ---- 报文容量 ---- */
#define MB_SLAVE_PDU_MAX            253u     /* Modbus PDU 上限(功能码+数据) */
#define MB_SLAVE_ADU_MAX            (MB_SLAVE_PDU_MAX + 7u) /* 含 MBAP 7 字节 */

/* ---- TCP 流重组缓冲容量(单条 ADU 上限) ---- */
#define MB_SLAVE_RX_MAX             MB_SLAVE_ADU_MAX

/* ---- 字区中"位软元件"每寄存器对应的位数(M0~M7679 等挂在字区时) ---- */
#define MB_SLAVE_BITS_PER_WORD      16u

/* ---- 寄存器字节序：1 = 把串口返回的(低字节在前)交换为 Modbus 大端 ---- */
#ifndef MB_SLAVE_REG_SWAP
#define MB_SLAVE_REG_SWAP           1
#endif

/* ---- 串口响应超时(ms)：超时后回 Modbus 异常码 0x0B ----
 * 取值依据：实测串口一问一答往返 21~29ms，留 10 倍余量即可；
 * 过大(原 1000ms)会使"一笔卡住的事务"长期占用单笔在途模型，
 * 主站快轮询时后续请求被连续回 0x06(从站忙)，表现为"批量报错"。 */
#ifndef MB_SLAVE_TIMEOUT_MS
#define MB_SLAVE_TIMEOUT_MS         300u
#endif

/* ---- 是否接受任意功能码(规范符合性) ----
 * 1(默认): 只要 MBAP 特征成立即视为 Modbus，未实现的功能码由本模块回异常 0x01
 *          —— 符合 MODBUS 规范，但与 MC 二进制帧存在极小歧义窗口
 *          (需 MC 帧偏移 2~3 恰为 00 00，即监视定时器为 0)
 * 0      : 额外要求功能码落在本模块支持列表内，歧义窗口最小
 *          —— 适用于与三菱 MC 共用端口且对误判极度敏感的场景
 */
#ifndef MB_SLAVE_ACCEPT_ANY_FC
#define MB_SLAVE_ACCEPT_ANY_FC      1u
#endif

/*=====================================================================
 *                        Modbus 区域掩码
 *  同一个 Modbus 地址数值在不同区域含义不同(位区 0x0000 与字区 0x0000
 *  指向完全不同的软元件)，因此地址解析必须"按区域过滤"。
 *===================================================================*/
#define SLAVE_AREA_COIL             0x01u    /* 线圈        (0x01/0x05/0x0F) 读写 */
#define SLAVE_AREA_DISCRETE         0x02u    /* 离散输入    (0x02)           只读 */
#define SLAVE_AREA_HOLDING          0x04u    /* 保持寄存器  (0x03/0x06/0x10) 读写 */
#define SLAVE_AREA_INPUT            0x08u    /* 输入寄存器  (0x04)           只读 */

#define SLAVE_AREA_BIT              (SLAVE_AREA_COIL | SLAVE_AREA_DISCRETE)   /* 位区 */
#define SLAVE_AREA_WORD             (SLAVE_AREA_HOLDING | SLAVE_AREA_INPUT)   /* 字区 */

/*=====================================================================
 *                          协议常量
 *===================================================================*/

/* Modbus 功能码 */
typedef enum {
    MB_FC_READ_COILS           = 0x01,   /* 读线圈(位)      */
    MB_FC_READ_DISCRETE_INPUTS = 0x02,   /* 读离散输入(位)  */
    MB_FC_READ_HOLDING_REGS    = 0x03,   /* 读保持寄存器(字) */
    MB_FC_READ_INPUT_REGS      = 0x04,   /* 读输入寄存器(字) */
    MB_FC_WRITE_SINGLE_COIL    = 0x05,   /* 写单个线圈      */
    MB_FC_WRITE_SINGLE_REG     = 0x06,   /* 写单个寄存器    */
    MB_FC_WRITE_MULTI_COILS    = 0x0F,   /* 写多个线圈      */
    MB_FC_WRITE_MULTI_REGS     = 0x10    /* 写多个寄存器    */
} mb_func_t;

/* Modbus 异常码 */
typedef enum {
    MB_EXC_OK                  = 0x00,   /* 无异常 */
    MB_EXC_ILLEGAL_FUNCTION    = 0x01,   /* 非法功能码 */
    MB_EXC_ILLEGAL_ADDRESS     = 0x02,   /* 非法数据地址 */
    MB_EXC_ILLEGAL_VALUE       = 0x03,   /* 非法数据值 */
    MB_EXC_SLAVE_FAILURE       = 0x04,   /* 从站设备故障 */
    MB_EXC_SERVER_BUSY         = 0x06,   /* 从站设备忙(已有在途事务，主站稍后重试) */
    MB_EXC_GATEWAY_TIMEOUT     = 0x0B    /* 网关目标设备无响应(串口超时) */
} mb_exc_t;

/* 事务阶段：位批量写入在读-改-写(RMW)时需分两个串口往返完成 */
#define MB_STAGE_NORMAL             0u   /* 常规: 读等数据帧 / 写等 ACK        */
#define MB_STAGE_RMW_READ           1u   /* 已下发 E00 读，等待 PLC 回状态数据 */
#define MB_STAGE_RMW_WRITE          2u   /* 已下发 E10 写回，等待 ACK          */

/*=====================================================================
 *                          数据结构定义
 *===================================================================*/

/**
 * @brief  Modbus 地址 → 三菱软元件 映射条目
 * @note   一条记录描述"一段连续的 Modbus 地址区间"与"一段连续的
 *         三菱软元件区间"之间的对应关系(闭区间)。
 *         数据表为 const，编译后驻留 Flash，不占用 SRAM。
 */
typedef struct {
    uint16_t mb_start;      /* Modbus 起始地址(闭区间)                        */
    uint16_t mb_end;        /* Modbus 结束地址(闭区间)                        */
    uint16_t dev_code;      /* 三菱软元件类型(MC_FX_D / MC_FX_M / MC_FX_X ...) */
    uint16_t dev_base;      /* 与 mb_start 对应的起始软元件编号                */
    uint8_t  area_mask;     /* 适用的 Modbus 区域掩码(SLAVE_AREA_xxx)          */
    uint8_t  is_bit;        /* 1 = 位软元件(M/S/TS/CS/Y/X)；0 = 字软元件(D/R/TN/CN) */
    uint8_t  bits_per_reg;  /* 字区中的位软元件: 每寄存器 16 位；位区: 1；字软元件: 0 */
    uint8_t  words_per_dev; /* 每个软元件占用的寄存器数(32 位计数器=2，其余=1)  */
} SLAVE_ADDR_MAP_T;

/**
 * @brief Modbus TCP 请求(解析结果)
 * @note  注意 quantity 的语义随功能码不同：
 *        - 0x01/0x02/0x03/0x04/0x0F/0x10 : 数量(线圈数/寄存器数)
 *        - 0x05/0x06                     : 该字段位置存放的是"写入值"
 *          (Modbus 规范中这两个功能码的 PDU 为 功能码+地址+值，不存在数量字段)
 */
typedef struct {
    uint16_t trans_id;      /* MBAP: 事务标识符            */
    uint16_t proto_id;      /* MBAP: 协议标识符(必须为 0)  */
    uint16_t length;        /* MBAP: 后续字节数            */
    uint8_t  unit_id;       /* MBAP: 单元标识符(从站地址)  */
    uint8_t  func;          /* PDU : 功能码                */
    uint16_t start_addr;    /* PDU : 起始地址              */
    uint16_t quantity;      /* PDU : 数量 或 写入值(见上注) */
    uint16_t data_len;      /* PDU : 写数据的字节数        */
    const uint8_t *data;    /* PDU : 写数据首指针          */
} mb_req_t;

/**
 * @brief Modbus TCP 事务上下文(在途请求)
 * @note  与 MC 协议一致：同一时刻只保留一笔在途事务。
 *        串口响应到达时，靠本结构还原"原请求"以构造响应。
 */
typedef struct {
    uint8_t  busy;          /* 1 = 有一笔事务正在等待串口响应 */
    uint8_t  sock;          /* 发起请求的以太网 socket         */
    uint8_t  unit_id;       /* 单元标识符(原样回送)            */
    uint8_t  func;          /* 功能码                          */
    uint8_t  is_write;      /* 1 = 写操作(串口回 ACK)          */
    uint8_t  stage;         /* 事务阶段(MB_STAGE_xxx)          */
    uint16_t trans_id;      /* 事务标识符(原样回送)            */
    uint16_t start_addr;    /* 原请求起始地址                  */
    uint16_t quantity;      /* 原请求数量 或 写入值(见 mb_req_t 注) */
    uint16_t dev_index;     /* 解析出的三菱软元件起始编号      */
    uint16_t dev_points;    /* 需要访问的软元件点数(位软元件=位数) */
    const SLAVE_ADDR_MAP_T *map;   /* 命中的映射条目(串口响应解析依据) */
    uint32_t tick;          /* 请求下发时刻(ms)，用于超时判定  */
    uint32_t uart_seq;      /* 本事务下发串口命令时入队的序号(uartTxGetLastSeq)。
                             * 串口响应到达时与 uart_rx_ctx.eth_seq_num 比对，
                             * 不等即为迟到/重复响应，必须丢弃而非误配。 */
    uint8_t  superseded;    /* 1 = 主站已改用另一个 TID 继续轮询(本事务已被放弃)。
                             * 其迟到响应若按原 TID 回送，会让主站看到"TID 对不上"。
                             * 处置见 MB_SLAVE_DROP_SUPERSEDED。 */
} mb_trans_t;

/**
 * @brief Modbus 从站模块总上下文
 * @note  整个模块唯一的全局状态载体。
 */
typedef struct {
    mb_trans_t trans;                                  /* 在途事务                 */

    /* 工作暂存区：0x0F 的"整字打包结果"与"队列超时丢弃"两个用途互斥，共用同一块 SRAM。
     * 128 字既满足整字打包上限(124)，也足够容纳 BitBatch 队列单节点(40 字)的出队丢弃。 */
    uint16_t scratch[MB_SLAVE_MAX_WRITE_WORDS];

    /* TCP 流重组缓冲：容忍 PDU 跨报文段 / 单段含多条请求 */
    uint8_t  rx_buf[MB_SLAVE_RX_MAX];
    uint16_t rx_len;                                   /* 已缓存字节数             */
    uint8_t  rx_sock;                                  /* 当前缓存归属的 socket    */

    /* 已确认为 Modbus 会话的 socket 位图(bit n = socket n)。
     * 用途：该 socket 上的串口响应一旦找不到归属事务，必须直接丢弃，
     * 不能回落到 MC 路径 —— 否则会把 MC 二进制帧发给 Modbus 主站。 */
    uint16_t modbus_sock_mask;
} mb_slave_t;

/* 模块唯一全局上下文(定义在 mb_slave.c) */
extern mb_slave_t g_mb_slave;

/* 地址映射表(定义在 mb_slave.c，驻留 Flash) */
extern const SLAVE_ADDR_MAP_T g_slave_addr_map[];
extern const uint16_t         g_slave_addr_map_size;

/*=====================================================================
 *                            对外接口
 *===================================================================*/

/**
 * @brief  初始化 Modbus 从站模块
 * @param  无
 * @retval 无
 * @note   清空在途事务、重组缓冲与位打包暂存，上电时调用一次即可。
 */
void MB_Slave_Init(void);

/**
 * @brief  判断一帧以太网数据是否为 Modbus TCP 请求
 * @param  frame    收到的数据首指针
 * @param  len      数据长度
 * @retval 1 = 是 Modbus TCP；0 = 不是
 * @note   判据：MBAP 协议标识符 == 0x0000。
 *         当 MB_SLAVE_ACCEPT_ANY_FC == 1(默认) 时不再限制功能码，
 *         未实现的功能码交由 MB_Slave_HandleRequest() 回异常码 0x01，
 *         以符合 MODBUS 规范；置 0 则额外要求功能码在支持列表内。
 */
uint8_t MB_Slave_IsFrame(const uint8_t *frame, uint16_t len);

/**
 * @brief  处理一帧 Modbus TCP 数据(流重组 + 逐条处理)
 * @param  Sour_Sock  源 socket(发起请求的以太网连接)
 * @param  Dest_Sock  目的 socket
 * @param  frame      本次收到的数据首指针
 * @param  len        本次收到的数据长度
 * @retval 0   已接管(无论成功或已回异常响应)
 * @note   内部先把数据追加到模块接收缓冲，再依据 MBAP 长度域切出完整 PDU
 *         逐条处理，因此支持：
 *           - PDU 跨越多个 TCP 报文段
 *           - 单个报文段内含多条流水线请求
 *         若参数非法，会直接回送 Modbus 异常响应。
 */
int MB_Slave_HandleRequest(uint8_t Sour_Sock, uint8_t Dest_Sock,
                           uint8_t *frame, uint16_t len);

/**
 * @brief  处理 PLC 串口响应，转换成 Modbus TCP 响应回送
 * @param  buf  PLC 串口响应帧首指针(由 sim_Process_switch 透传)
 * @param  len  响应长度
 * @retval 0   已接管并回送 Modbus 响应(或已转入 RMW 下一阶段)
 *         -1  当前无在途 Modbus 事务，未接管
 */
int MB_Slave_HandleSerialResp(uint8_t *buf, uint16_t len);

/**
 * @brief  Modbus 从站周期任务：在途事务超时检测
 * @param  无
 * @retval 无
 * @note   需在主循环中周期调用(建议 10ms 以内调用一次)。
 *         超时会向上位机回送异常码 0x0B(网关目标设备无响应)；
 *         若正处于读-改-写阶段，会同时丢弃已入队的待写位，避免污染后续事务。
 */
void MB_Slave_Tick(void);

/**
 * @brief  串口接收帧错误通知：立即结束在途事务并回异常码 0x0B
 * @param  无
 * @retval 无
 * @note   由串口接收侧在检测到"帧残缺 / 缓冲溢出 / 半帧滞留超时"时调用，
 *         把故障上报从 MB_SLAVE_TIMEOUT_MS(1s) 缩短为一次串口往返时间，
 *         避免主站在该窗口内的请求被连续回 0x06(从站设备忙)。
 */
void MB_Slave_NotifyFrameError(void);

/**
 * @brief  查询某个 socket 是否存在在途 Modbus 事务
 * @param  sock  以太网 socket 编号
 * @retval 1 = 是；0 = 否
 */
uint8_t MB_Slave_IsPending(uint8_t sock);

/**
 * @brief  查询 Modbus 地址对应的映射条目(供外部诊断/调试使用)
 * @param  area  区域掩码(SLAVE_AREA_xxx)
 * @param  addr  Modbus 起始地址
 * @param  qty   数量
 * @retval 命中的条目首指针；未命中返回 NULL
 */
const SLAVE_ADDR_MAP_T *MB_Slave_FindMap(uint8_t area, uint16_t addr, uint16_t qty);

#ifdef __cplusplus
}
#endif

#endif /* __MB_SLAVE_H__ */
