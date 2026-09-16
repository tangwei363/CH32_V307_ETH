#ifndef MELSEC_FX_NET_H
#define MELSEC_FX_NET_H

#include "ch32v30x.h"

#include "melsec_fx_core.h"
#include "melsec_fx_tables.h"
 
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#define MC_FX_TX_Subtitle       0x80       // 副标  发 

/* MELSEC FX 二进制协议命令码 */
typedef enum {
    MC_CMD_BIT_BATCH_READ   = 0x00,   // 位单位的成批读出
    MC_CMD_WORD_BATCH_READ  = 0x01,   // 字单位的成批读出
    MC_CMD_BIT_BATCH_WRITE  = 0x02,   // 位单位的成批写入
    MC_CMD_WORD_BATCH_WRITE = 0x03,   // 字单位的成批写入
    MC_CMD_BIT_RANDOM_WRITE = 0x04,   // 位单位的随机写入
    MC_CMD_WORD_RANDOM_WRITE= 0x05,   // 字单位的随机写入
    MC_CMD_REMOTE_RUN       = 0x13,   // 针对可编程控制器请求远程RUN
    MC_CMD_REMOTE_STOP      = 0x14,   // 针对可编程控制器请求远程STOP
    MC_CMD_PLC_MODEL        = 0x15,   // PLC可编程控制器的型号名
    MC_CMD_ECHO_TEST        = 0x16    // 从其他节点接收的字符直接返回到其他节点
} MC_Cmd_t;


#pragma pack (1)                //编译器将按照1个字节对齐

// ==================== MC协议数据结构定义 二进制码 / ASCII通信时的格式 ====================

/**
 * @brief MC协议接收 命令结构
 * @note 帧格式：副标 (1) + PC编号(1) + 监视定时 (2) + 起始软元件 (4) + 软元件名(2) + 软元件点数(2) + 结束代码(1)
 */
typedef struct {
    uint8_t   sub_header;         // 副标题 (例如: 0x00)  b7: 0:命令/ 1:响应标志 
    uint8_t   pc_number;          // PC编号 (例如: 0xFF)
    uint16_t  monitor_timer;      // 监视定时器 (例如: 0x0A 0x00)
    uint32_t  start_device;       // 起始软元件 (4字节)
    uint16_t  device_name;        // 软元件名   (2字节)
    uint16_t  device_count;       // 软元件点数 (例如: 0x00 0x00)

    uint8_t   Format_Code;        // 格式代码 (1字节)  0：二进制 / 1：ASCII
    uint8_t   device_type;        // 元件类型枚举
   
} NET_MC_Recv_Resp_t;

extern NET_MC_Recv_Resp_t net_mc_meta ;       //网络 传入  MC协议上下文 
extern NET_MC_Recv_Resp_t uart_mc_meta ;      //串口 fifo  MC协议上下文 

#pragma pack ()                   // 取消自定义字节对齐方 

/* 位批量写入(读-改-写) 待写位队列
 * 非8对齐/非8倍数的位批量写入: 先 E00 读回当前状态, 再与本队列保存的待写位
 * 合并后 E10 写回. 按 FIFO 出队, 与 FX 串口响应顺序对应.
 * 对齐元数据(bit_offset/dev_count)由响应侧的 uart_mc_meta 提供,
 * 故要求串口同时仅一笔事务在途(与既有单全局上下文设计一致). */
#define BITBATCH_QUEUE_SIZE    4    /* 最大在途非对齐写笔数 */
#define BITBATCH_NODE_DATA_LEN 40   /* 节点缓冲: 256点+7偏移→最大33字节, 留余量 */

/* MC ASCII 通信单次成批操作的最大点数硬上限
 * 字写入与 BuildE1xWriteCommon 的 max_count(=128) 对齐;
 * 位写入 1 点 = 1 个 ASCII 十六进制字符(每字节存 2 点), 故上限取 256 点(=16字).
 * 用于把"依赖不可信输入的运行时 VLA"改为编译期固定大小缓冲, 杜绝栈越界/撑爆. */
#define MC_ASCII_MAX_WORD_POINTS  128
#define MC_ASCII_MAX_BIT_POINTS   256

typedef struct {
    uint16_t  data[BITBATCH_NODE_DATA_LEN]; /* 保存位单位整合后的16bit状态(uint16_t字) 缓冲区 */
    uint16_t word_count;                    /* 实际字数 */
} BitBatch_queue_node_t;

/* 环形队列管理: O(1) 入队/出队操作 */
typedef struct {
    BitBatch_queue_node_t nodes[BITBATCH_QUEUE_SIZE]; /* 队列节点数组 */
    uint8_t head;                                      /* 头指针(出队位置) */
    uint8_t tail;                                      /* 尾指针(入队位置) */
    uint16_t count;                                    /* 当前队列长度 */
} BitBatch_queue_t;

extern BitBatch_queue_t g_bitbatch_queue;

void BitBatch_queue_Init(void);
int  BitBatch_queue_Enqueue(const uint16_t *data, uint16_t word_count);
int  BitBatch_queue_Dequeue(uint16_t *data, uint16_t *word_count);


uint8_t get_net_MC_Recv_active(void);

void clr_net_MC_Recv(void);

int MC_Net_binary_ParseRecvResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *frame_buff, uint16_t frame_len);
int MC_Net_binary_BuildSendResp(uint8_t *Resp_data, uint8_t* frame_buff, uint16_t* frame_len);
int MC_Net_ASCII_ParseRecvResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t *frame_buff, uint16_t frame_len);
int MC_Net_ASCII_BuildSendResp(uint8_t *Resp_data, uint8_t* frame_buff, uint16_t* frame_len);
int MC_Net_BuildModelResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t type);
int MC_Net_Build_EchoTest_Resp(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t *Resp_data, uint16_t Resp_len, uint8_t type );
int MC_Net_BuildSendResp(uint8_t *Resp_data, uint8_t *txbuf, uint16_t *size);
int MC_Net_BuildSendAbnormalResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock);

/**
 * @brief  位批量写入 读-改-写(RMW) 合并接口
 * @param  Resp_data   PLC 回读的 ASCII 数据区首指针(即串口响应帧 STX 之后)
 * @param  bit_offset  起始软元件相对 8 位对齐读地址的位偏移 (0~7)
 * @param  dev_cnt     本次写入的软元件点数
 * @retval 0 成功；其它 失败
 * @note   从 BitBatch 队列取出本笔请求保存的待写位(uint16_t 字)，
 *         与 E00 读回值按位掩码合并后调用 E10 写回。
 *         该接口原先为 melsec_fx 内部静态函数，现对外暴露，
 *         以便 Modbus 从站模块复用同一套已验证的合并逻辑。
 */
int MC_Net_BitBatchWrite_RMW_Merge(const uint8_t *Resp_data, uint8_t bit_offset, uint16_t dev_cnt);
 

#endif


