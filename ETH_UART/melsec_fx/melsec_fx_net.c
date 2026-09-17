#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
 
#include "melsec_fx_tables.h"
#include "ethernet_app.h"
#include "bsp_uart.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>

NET_MC_Recv_Resp_t net_mc_meta = {0};       //网络 传入  MC协议上下文 
NET_MC_Recv_Resp_t uart_mc_meta = {0};      //串口 fifo  MC协议上下文 

/*
网络通信协议处理模块
帧到达 → 解析填入 net_mc_meta (melsec_fx_net.c)
       → 应用层按需修改字段 (ethernet_app/sntp/fx_enetinf/fx_acclog)
       → 构建 MC 协议命令并入队 (melsec_fx_core.c)
       → 串口响应到达 → 出队恢复 uart_mc_meta → 构建网络响应 (melsec_fx_net.c)
*/
void clr_net_MC_Recv(void)
{
    memset(&net_mc_meta, 0, sizeof(net_mc_meta));
    BitBatch_queue_Init();
}

/* ==================== 位批量写入 读-改-写 待写位队列 ==================== */
BitBatch_queue_t g_bitbatch_queue;

void BitBatch_queue_Init(void)
{
    g_bitbatch_queue.head  = 0;
    g_bitbatch_queue.tail  = 0;
    g_bitbatch_queue.count = 0;
    memset(g_bitbatch_queue.nodes, 0, sizeof(g_bitbatch_queue.nodes));
}

int BitBatch_queue_Enqueue(const uint16_t *data, uint16_t word_count)
{
    if (data == NULL || word_count == 0 || word_count > BITBATCH_NODE_DATA_LEN) {
        return -1;
    }
    /* 禁用中断保护队列操作 */
    __disable_irq();
    if (g_bitbatch_queue.count >= BITBATCH_QUEUE_SIZE) {
        __enable_irq();
        UART_DEBUG("BitBatch queue full, reject\r\n");
        return -2;                       /* 队列满 */
    }
    BitBatch_queue_node_t *node = &g_bitbatch_queue.nodes[g_bitbatch_queue.tail];
    memcpy(node->data, data, (size_t)word_count * sizeof(uint16_t));
    node->word_count = word_count;
    g_bitbatch_queue.tail = (uint8_t)((g_bitbatch_queue.tail + 1) % BITBATCH_QUEUE_SIZE);
    g_bitbatch_queue.count++;
    /* 启用中断 */
    __enable_irq();
    return 0;
}

int BitBatch_queue_Dequeue(uint16_t *data, uint16_t *word_count)
{
    if (data == NULL || word_count == NULL) {
        return -1;
    }
    /* 禁用中断保护队列操作 */
    __disable_irq();
    if (g_bitbatch_queue.count == 0) {
        __enable_irq();
        return -2;                       /* 队列空 */
    }
    BitBatch_queue_node_t *node = &g_bitbatch_queue.nodes[g_bitbatch_queue.head];
    memcpy(data, node->data, (size_t)node->word_count * sizeof(uint16_t));
    *word_count = node->word_count;
    g_bitbatch_queue.head = (uint8_t)((g_bitbatch_queue.head + 1) % BITBATCH_QUEUE_SIZE);
    g_bitbatch_queue.count--;
    /* 启用中断 */
    __enable_irq();
    return 0;
}

// ==================== 内部函数 ====================
 
/**
 * @brief 判断软元件类型是否为字软元件（支持单字写入）
 * @param device_type 软元件类型
 * @return 1-是字软元件(D,R,T,C)，0-是位软元件(X,Y,M,S等)
 */
static inline uint8_t MC_Net_IsWordDevice(uint8_t device_type)
{
    return (device_type == type_D || device_type == type_R ||
            device_type == type_T || device_type == type_C   );
}

/**
 * @brief 调整C200～C234软元件点数（32位软元件）
 * @param device_type 软元件类型
 * @param start_device 起始软元件编号
 * @param device_count 原始点数
 * @return 调整后的点数
 */
static inline uint16_t MC_Net_Adjust32BitCounterCount(uint8_t device_type, uint16_t start_device, uint16_t device_count)
{
    if (device_type == type_C && start_device >= 200) {
        MELSEC_DEBUG("C200～C234 32位 软元件 点数翻倍 \r\n" );
        if (device_count % 2 == 1) {
            MELSEC_DEBUG(" 软元件点数是奇数, 错误代码57H \r\n");
        }
        return device_count ;
    }
    return device_count;
}

/**
 * @brief 拷贝数据到大端序缓冲区（H+L顺序），使用16位字节交换优化
 * @param dest_buf 目标缓冲区
 * @param src_buff 源缓冲区
 * @param src_offset 源缓冲区偏移
 * @param count 拷贝数量（字数）
 * @return 成功返回0，失败返回-1
 */
static inline int16_t MC_Net_CopyDataToBigEndianBuffer(uint8_t* dest_buf, uint8_t* src_buff, uint16_t src_offset, uint16_t count)
{
    uint16_t max_count = (count > (MELSEC_FX_MAX_DATA_LEN / 2)) ? (MELSEC_FX_MAX_DATA_LEN / 2) : count;

    const uint8_t* src = src_buff + src_offset;
    for (uint16_t i = 0; i < max_count; i++) {
        dest_buf[0] = src[1];   /* 高字节 (MSB) */
        dest_buf[1] = src[0];   /* 低字节 (LSB) */
        dest_buf += 2;
        src += 2;
    }
    return 0;
}

/**
 * @brief 处理MC协议读命令   二进制和ASCII共用
 * @param socket_ID socket编号
 */
static inline void MC_Net_Process_ReadCommand(uint8_t Sour_Sock ,uint8_t  Dest_Sock)
{
    switch (net_mc_meta.sub_header)
    {
        case MC_CMD_BIT_BATCH_READ:
        {
            uint16_t read_bytes;

            MELSEC_DEBUG("Net 位单位的成批读出 \r\n");        //256
            if( net_mc_meta.device_count == 0 ){
                net_mc_meta.device_count = 256;              //位单位:  00代表256个 (按照原厂的特殊处理)
                MELSEC_DEBUG("位单位:  00代表256个 \r\n");   //256
            }
            //先按照字节读出位单位状态.然后在解析数据的将字单位数据转换成位单位发送net 
            //整数除法会截断小数部分。加上 d-1 后，只要有余数（1~7），就会进位到下一个整数；无余数时不影响结果。
            /* 字节数需补偿起始位的 8 位对齐偏移：解析侧按 (start % 8) 跳过首字节前导位，
             * 实际消耗 ceil((offset+点数)/8) 字节，故请求量必须覆盖该长度，否则解析会越读。
             * 又因 MELSEC_FX_BuildExxReadCmd 的 length 参数单位是"字数(每字2字节)"，
             * 帧内写入的是 length*2 作为字节数(该字段仅 1 字节宽)，故还需换算为字数：
             *     length = ceil(read_bytes / 2)
             * 若直接把字节数当 length 传入，会导致：
             *   ① PLC 被要求多读一倍数据(串口流量与响应延迟翻倍)；
             *   ② 字节数 > 127 时 (uint8_t)(length*2) 溢出截断，返回数据不足而解析失败。 */
            read_bytes = (uint16_t)(((net_mc_meta.start_device % 8u) +
                                     net_mc_meta.device_count + 7u) / 8u);
            MELSEC_FX_BuildE00ReadCmd( Sour_Sock , Dest_Sock,
                                      net_mc_meta.start_device,
                                      (uint16_t)((read_bytes + 1u) / 2u));
            break;
        }
        case MC_CMD_WORD_BATCH_READ:
        {
            MELSEC_DEBUG("Net (字单位)的成批读出 获取点数%d \n",net_mc_meta.device_count); // 32个字 (512点)
            if ( net_mc_meta.device_name == MC_FX_R) {

                MELSEC_FX_BuildE06ReadCmd( Sour_Sock , Dest_Sock,
                                            net_mc_meta.start_device,
                                            net_mc_meta.device_count);
            }
            else {
                 // 在C200～C255的成批读出/成批写入中， 点数指定为奇数。
                if( net_mc_meta.device_name == MC_FX_CN && 
                        net_mc_meta.start_device >= 200 && 
                        net_mc_meta.device_count % 2 != 0) 
                {
                    MELSEC_DEBUG("C200 -- C255 点数指定为奇数 %d  \r\n", net_mc_meta.device_count);
                    // 软元件的指定有误( 字单位随机写入时，指定C200 ～ C255)
                    eth_socket[Sour_Sock].Error_Code = 2555; 
                    //报错 处理 命令字节长度不是规定长度     57H  
                    ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                            net_mc_meta.sub_header,
                                            MC_END_ILLEGAL_POINT_COUNT );
                    
                    return  ;
                }
                MELSEC_FX_BuildE00ReadCmd( Sour_Sock , Dest_Sock,
                                            net_mc_meta.start_device,
                                            net_mc_meta.device_count);
            }
            break;
        }
        default:
            break; 
    }
}

 
/**
 * @brief 判断位批量写入区间是否"起始8位对齐且结束16位对齐"
 *        起始位按 8 位对齐(bit_offset==0), 数据长度按 16 位对齐(结束位 end_bit 为 16 的倍数);
 *        仅当两者都满足时, 整段恰好占据若干完整 16bit 字, 可直接 E10 整字写;
 *        否则区间跨越字或含残缺位, 需先 E00 读再合并 E10 写回(RMW).
 *
 * @param start_device   起始软元件编号
 * @param device_count   写入点数
 * @param aligned_start  输出: 起始位是否8位对齐
 * @param aligned_end    输出: 结束位是否16位对齐(= 尾16对齐)
 */
static inline void MC_Net_BitBatch_GetAlign(uint16_t start_device,
                                            uint16_t device_count,
                                            uint8_t *aligned_start,
                                            uint8_t *aligned_end)
{
    uint8_t  bit_offset = (uint8_t)(start_device % 8);
    uint16_t end_bit    = (uint16_t)(bit_offset + device_count);
    *aligned_start = (bit_offset == 0);
    *aligned_end   = (uint8_t)(end_bit % 16 == 0);
}

/**
 * @brief 位单位成批写入处理 (二进制码/ASCII共用)
 *
 * 将MC协议nibble编码(每字节2点)的位写入请求转换为E系列串口指令:
 *   单点写入 → E7(强制ON)/E8(强制OFF)
 *   起始8位对齐且结束16位对齐 → E10 整字直接批量写入(干净路径)
 *   否则(非对齐或数据长度非16位) → E00 先读当前状态 → E10 写回(脏路径,读-改-写)
 *
 * @param Sour_Sock  源socket (网络侧)
 * @param Dest_Sock  目标socket (串口侧)
 * @param nibble_buf nibble编码数据 (二进制: frame_buff+12, ASCII: hex解码后)
 * @param dev_count  软元件点数
 * @param dev_start  起始软元件编号
 * @param dev_name   软元件名称代码
 */
static inline void MC_Net_Process_BitBatchWrite(uint8_t  Sour_Sock,
                                                 uint8_t  Dest_Sock,
                                                 const uint8_t *nibble_buf,
                                                 uint16_t dev_count,
                                                 uint32_t dev_start,
                                                 uint16_t dev_name)
{
    if( dev_count == 0 || dev_count > 256){
        dev_count = 0xFF;
    }
    MELSEC_DEBUG("位单位的成批写入 \r\n");
    /*
     * 分支1: 单点写入 → E7/E8 强制ON/OFF
     * 直接从nibble_buf[0]高nibble取bit,不关心字节对齐
     */
    if (dev_count == 1) {
        uint8_t bit_val = (nibble_buf[0] >> 4) & 0x01;
        MELSEC_DEBUG("单点写入: %c%c[%d]=%d → 强制%s\r\n",
                     dev_name >> 8, dev_name, dev_start, bit_val,
                     bit_val ? "E7:ON" : "E8:OFF");
        MELSEC_FX_Build_E7_E8_ForceCmd(Sour_Sock, Dest_Sock,
                                       dev_name, dev_start, bit_val);
        return;
    }

    uint8_t  bit_offset    = (uint8_t)(dev_start % 8);
    uint8_t  aligned_start = 0;
    uint8_t  aligned_end   = 0;   /* 尾16对齐(结束位为16的倍数) */
    /* 判断起始(8位)对齐与结束(16位)对齐: 仅当都对齐时才可直接 E10 整字写, 否则需 RMW */
    MC_Net_BitBatch_GetAlign(dev_start, dev_count, &aligned_start, &aligned_end);
    MELSEC_DEBUG("起始=%c%c[%d] 偏移=%d 点数=%d 首8对齐=%s 尾16对齐=%s\r\n",
                 dev_name >> 8, dev_name, dev_start,
                 bit_offset, dev_count,
                 aligned_start ? "是" : "否",
                 aligned_end ? "是" : "否");

    /* 多点写入: 按16位字计算覆盖字数(与 GetAlign 尾16对齐一致) */
    uint16_t total_bits = (uint16_t)(bit_offset + dev_count);
    uint16_t word_count = (uint16_t)((total_bits + 15) / 16); /* 向上取整到16位字 */
    /*
     * nibble解码 → uint16_t 字数组(字w的位b对应设备 start+16w+b)
     * bit_offset = dev_start%8, 即相对8位对齐读地址(read_addr=dev_start&~7)的位偏移;
     * 字内位映射直接打包, 未写入位由 memset 填 0, 首/末残缺位在 RMW 合并或直写时自然处理.
     */
    MELSEC_DEBUG("位映射打包 字数=%d (total_bits=%d)\r\n", word_count, total_bits);
    uint16_t dst_w[word_count + 1];       /* 局部暂存: uint16_t 字(位b=对应设备) */
    memset(dst_w, 0, sizeof(dst_w));      /* 清零: 未写入位保持 0 */
    for (uint16_t n = 0; n < dev_count; n++)
    {
        uint8_t sbit = (n & 1)
            ? (nibble_buf[n / 2] & 0x01)          /* 奇数点→低nibble */
            : ((nibble_buf[n / 2] >> 4) & 0x01);  /* 偶数点→高nibble */

        uint16_t total_pos = (uint16_t)(bit_offset + n); /* 相对起始设备的全局位偏移 */
        uint16_t word_idx  = (uint16_t)(total_pos >> 4); /* 16位字索引 */
        uint8_t  bit_in_w  = (uint8_t)(total_pos & 0x0F);/* 字内位偏移(0~15) */

        if (sbit) {
            dst_w[word_idx] |= ((uint16_t)1u << bit_in_w); /* 设置对应设备位 */
        }
    }
    // 高八位和低八位互换
    for (uint16_t i = 0; i < word_count; i++) {
        uint16_t tmp = dst_w[i];
        dst_w[i] = (tmp >> 8) | (tmp << 8);
    }
    if ( aligned_start && aligned_end ) {
        /*
        * 分支2: E10 直接写入
        */
        MELSEC_DEBUG("[E10] 直接写入 \r\n");
        MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock,
                                        dev_start,
                                        (const uint16_t *)dst_w,
                                        word_count);
    } else {
        /*
         * 分支3: 非8对齐或点数非16倍数 → 读-改-写
         * E00读取 word_count 个16bit字(覆盖范围内所有位, 含首/末残缺位),
         * 待响应到达后在 MC_Net_BitBatchWrite_RMW_Merge 中与 E00 读回值按位掩码合并(非设置区bit状态:头和尾填充0的部分)
         */
        MELSEC_DEBUG("需要先读状态[E00],再写入状态[E10] \r\n");

        MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                                  dev_start & ~7u,     /* 向下对齐到8的倍数 */
                                  word_count);         /* word_count 个16bit字 */

        MELSEC_DEBUG("dst_w入队待合并\r\n"); 
        for(int i=0;i<word_count;i++){
            MELSEC_DEBUG("dst_w[%d]: 0x%04X\r\n", i, dst_w[i]);
        }

        if (BitBatch_queue_Enqueue(dst_w, word_count) != 0) {
            MELSEC_DEBUG("RMW队列异常, 待写位未保存! \r\n");
        }
    }
}

/**
 * @brief 位批量写入 读-改-写(RMW) 合并接口 (二进制码 / ASCII 共用)
 *        从待写位队列出队本次请求的已打包位(uint16_t 字, 位b=对应设备, E10 可直接发送),
 *        与 E00 读回的当前状态(uint16_t 字) 按位掩码合并, 再构造 E10 整字写命令.
 *
 * @param Resp_data     E00 读回的状态数据缓冲区(ASCII hex: 每字4字符=高字节+低字节)
 * @param bit_offset    请求起始软元件相对对齐地址(read_addr=start&~7)的位偏移 (0..7)
 * @param dev_cnt       请求写入的点数
 * @return 0-成功; 负-失败(BitBatch_queue_Dequeue 错误码)
 *
 * @note 队列/合并均以 uint16_t 字(N 字) 为单位: 字 w 的位 b(0..15) 对应设备 (read_addr+16w+b).
 *       仅写入区间 [bit_offset, bit_offset+dev_cnt) 内的位, 其余位保留 E00 读回值
 *       (首字保留 [0,bit_offset), 末字保留 [bit_offset+dev_cnt,16)). 要求串口仅一笔事务在途.
 */
int MC_Net_BitBatchWrite_RMW_Merge(const uint8_t *Resp_data,
                                   uint8_t bit_offset,
                                   uint16_t dev_cnt)
{
    /* 出队本次写请求保存的待写位(uint16_t 字, FIFO, 与 E00 响应顺序一致) */
    uint16_t pend[BITBATCH_NODE_DATA_LEN];
    uint16_t pend_words = 0;
    int ret = BitBatch_queue_Dequeue(pend, &pend_words);
    if (ret != 0) {
        MELSEC_DEBUG("RMW队列为空, 无法合并待写位! \r\n");
        return ret;
    }

    if (pend_words == 0) {
        return 0;
    }

    uint16_t word_count = pend_words;
    MELSEC_DEBUG("RMW合并 word_count=%d bit_offset=%d dev_cnt=%d\r\n",
                 word_count, bit_offset, dev_cnt);

    /* 合并: pend 为"字节已互换"布局(见解码末尾 高低字节互换):
     *   高字节(bit8..15) = 字内设备偏移 0..7  → ASCII 首字节 Resp_data[4w+0..1]
     *   低字节(bit0..7)  = 字内设备偏移 8..15 → ASCII 次字节 Resp_data[4w+2..3]
     * pend 仅写入区间(设备偏移 [bit_offset, end_global))位有效, 区间外为0;
     * 补齐位只出现在首字(头, 偏移<bit_offset)与末字(尾, 偏移>=末字写入结束偏移),
     * 只转换涉及的 ASCII 字节, 与 pend 对应字节相或后原地写回 pend. */
    uint16_t end_global = (uint16_t)(bit_offset + dev_cnt);

    /* 首字头部补齐: 设备偏移 [0,bit_offset) 位于高字节, bit_offset∈1..7, 仅需 Resp_data[0..1] */
    if (bit_offset != 0u) {
        uint8_t rb_hi = AsciiHexToUint8(Resp_data[0], Resp_data[1]); /* 设备偏移 0..7 */
        uint8_t hmask = (uint8_t)((1u << bit_offset) - 1u);          /* 保留低 bit_offset 个设备位 */
        /* ASCII only: this file is GBK-encoded, keep additions ASCII-safe.
         * hmask is uint8, so it only covers device offsets 0..7 (the high byte).
         * For bit_offset in 9..15 the offsets 8..bit_offset-1 live in the LOW
         * byte and must be preserved as well, otherwise those points get cleared.
         * Widened to 16 bits here: high part keeps offsets 0..7, low part keeps
         * offsets 8..bit_offset-1. For bit_offset <= 8 the low part is 0, so the
         * behaviour of the existing (8-bit aligned) callers is unchanged. */
        uint16_t hmask16 = (uint16_t)((1u << bit_offset) - 1u);
        uint8_t  rb_lo_h = AsciiHexToUint8(Resp_data[2], Resp_data[3]);

        pend[0] = (uint16_t)(pend[0]
                             | ((uint16_t)((rb_hi & (uint8_t)hmask16) << 8))
                             | (uint16_t)(rb_lo_h & (uint8_t)(hmask16 >> 8)));
        MELSEC_DEBUG("HEAD rb_hi=0x%02X hmask=0x%02X -> pend[0]=0x%04X\r\n", rb_hi, hmask, pend[0]);
    }

    /* 末字尾部补齐: 设备偏移 [end_local,16) 保留读回值 (末字内写入结束偏移 0<end_local<=16) */
    uint16_t tw        = (uint16_t)(word_count - 1u);
    uint16_t end_local = (uint16_t)(end_global - 16u * tw);
    if (end_local < 16u) {
        if (end_local >= 8u) {
            /* 保留区间全在低字节 [end_local,16) → 仅需末字次字节 Resp_data[4tw+2..3] */
            uint8_t rb_lo = AsciiHexToUint8(Resp_data[4 * tw + 2], Resp_data[4 * tw + 3]);
            uint8_t lmask = (uint8_t)(~((1u << (end_local - 8u)) - 1u) & 0xFFu);
            pend[tw] = (uint16_t)(pend[tw] | (rb_lo & lmask));
            MELSEC_DEBUG("TAIL rb_lo=0x%02X lmask=0x%02X -> pend[%d]=0x%04X\r\n", rb_lo, lmask, tw, pend[tw]);
        } else {
            /* 高字节 [end_local,8) + 低字节整字节 [8,16) 均需保留 → 末字两字节都要 */
            uint8_t rb_hi = AsciiHexToUint8(Resp_data[4 * tw],     Resp_data[4 * tw + 1]);
            uint8_t rb_lo = AsciiHexToUint8(Resp_data[4 * tw + 2], Resp_data[4 * tw + 3]);
            uint8_t hmask = (uint8_t)(~((1u << end_local) - 1u) & 0xFFu);
            pend[tw] = (uint16_t)(pend[tw] | ((uint16_t)(rb_hi & hmask) << 8) | rb_lo);
            MELSEC_DEBUG("TAIL rb_hi=0x%02X rb_lo=0x%02X hmask=0x%02X -> pend[%d]=0x%04X\r\n",
                         rb_hi, rb_lo, hmask, tw, pend[tw]);
        }
    }

    memcpy(&net_mc_meta, &uart_mc_meta, sizeof(uart_mc_meta));
    net_mc_meta.start_device = (uint32_t)(uart_mc_meta.start_device & ~7u);  /* 回写整字, 与 E00 读地址对齐 */
    net_mc_meta.device_count = (uint16_t)(word_count * 8);                    /* 实际点数(与原逻辑一致) */
    /* ASCII only (GBK-encoded file): propagate the builder result instead of
     * swallowing it. Callers can then turn "E10 was rejected locally" into an
     * immediate Modbus exception, instead of advancing the transaction and
     * waiting for an ACK that will never arrive (300 ms fake timeout). */
    return MELSEC_FX_BuildE10WriteParamCmd(uart_rx_ctx.Sour_Sockid,
                                           uart_rx_ctx.Dest_Sockid,
                                           (uint16_t)(uart_mc_meta.start_device & ~7u),
                                           (const uint16_t *)pend,
                                           word_count);
}
// ==================== MC协议数据结构定义 二进制码通信时的格式 ====================
//1) 二进制码通信时
//如果没有特殊说明， 各说明中的值将直接以二进制值的方式， 按照指定顺序(L-H)进行收发。
/**
 * @brief 解析MC协议接收命令帧 (二进制码) - 使用全局变量
 * @param frame_buff 接收到的数据帧缓冲区
 * @param frame_len 数据帧长度
 * @return 0-成功，-1-参数无效，-2-帧长度不足
 *
 * @note 帧格式解析：
 *   - offset 0: 副标题 (1字节)
 *   - offset 1: PC编号 (1字节)
 *   - offset 2-3: 监视定时器 (2字节)
 *   - offset 4-7: 起始软元件 (4字节)
 *   - offset 8-9: 软元件名 (2字节)
 *   - offset 10-11: 软元件点数 (2字节)
 *   - offset 12: 结束代码 (1字节)
 *   - 总长度: 13字节
 *
 * 示例：00 FF 0A 00 64 00 20 00 00 00 00 00 00
 *       副  PC  定时器  起始软元件  软元件名  点数  结束
 */
int MC_Net_binary_ParseRecvResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t* frame_buff, uint16_t frame_len)
{
    if (frame_buff == NULL) {
        return -1;
    }

    /* 远程RUN(13H)/STOP(14H)=4字节, 读写命令帧=13字节 */
    if (frame_len < 4) {
        MELSEC_DEBUG("帧长度不足 (len=%d, min=4)\r\n", frame_len);
        eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
        //报错 处理 命令字节长度不是规定长度     57H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                frame_buff[0],
                                MC_END_ILLEGAL_POINT_COUNT ); 
        return -2;
    }
  
    /* === 帧头解析: 固定索引直接读取, 消除 offset 变量维护开销 === */
    const uint8_t *p = frame_buff;
    net_mc_meta.sub_header    = p[0];
    net_mc_meta.pc_number     = p[1];
    net_mc_meta.monitor_timer = ((uint16_t)p[3] << 8)  | p[2];
    uint8_t sub_hdr = net_mc_meta.sub_header;   /* 移出 if 块，4字节帧也可用 */
 
    //PC号错误(10H):指定了FF以外的PC号
    if(net_mc_meta.pc_number != 0xFF)
    {
        MELSEC_DEBUG("PC编号错误: 0x%02X\r\n", net_mc_meta.pc_number);
        eth_socket[Sour_Sock].Error_Code =   2560; //   PC编号有误
        //报错 处理 PC号错误(10H):指定了FF以外的PC号      5BH  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                net_mc_meta.sub_header,
                                MC_END_UNABLE_TO_COMM ); 
        return -3;
    }
    #ifdef _MELSEC_FX_DEBUG
        //02 FF 00 00 00 00 00 00 20 59 01 00 01
        MELSEC_DEBUG("=== MC接收命令解析 ===\r\n");
        MELSEC_DEBUG("副标题: 0x%02X --", sub_hdr);
        MELSEC_DEBUG("PC编号: 0x%02X --", net_mc_meta.pc_number);
        MELSEC_DEBUG("监视定时器: 0x%04X\r\n", net_mc_meta.monitor_timer);
    #endif
    if( frame_len >= 10)
    {
        net_mc_meta.start_device  = ((uint32_t)p[7] << 24) | ((uint32_t)p[6] << 16)
                                  | ((uint32_t)p[5] << 8)  | p[4];
        net_mc_meta.device_name   = ((uint16_t)p[9] << 8)  | p[8]; 
        net_mc_meta.device_count  = (uint16_t) p[10];  // 数量: 只用一个字节[10]  .
        // p[11] : 空闲.暂时没有什么用.相当于占位符


    #ifdef _MELSEC_FX_DEBUG
        //02 FF 00 00 00 00 00 00 20 59 01 00 01
        MELSEC_DEBUG("起始软元件:%c%c[%d] --", net_mc_meta.device_name, net_mc_meta.device_name>>8,
                                            net_mc_meta.start_device);
        MELSEC_DEBUG("软元件点数: %d\r\n", net_mc_meta.device_count);
        MELSEC_DEBUG("====================\r\n");
    #endif

    }else { 
        // 读取命令帧长度不足 这个三个命令除外 ( frame_len == 4 )
        if( sub_hdr != MC_CMD_REMOTE_RUN &&   //     = 0x13,   // 针对可编程控制器请求远程RUN
            sub_hdr != MC_CMD_REMOTE_STOP &&  //      = 0x14,   // 针对可编程控制器请求远程STOP
            sub_hdr != MC_CMD_PLC_MODEL   )  //      = 0x15,   // PLC可编程控制器的型号名 )    // 8字节表示 1个点数
        {
            MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
            eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
            //报错 处理 命令字节长度不是规定长度     57H  
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                    frame_buff[0],
                                    MC_END_ILLEGAL_POINT_COUNT ); 
            return -1;
        } 
    }

    /* 缓存频繁访问的全局字段，减少全局结构体解引用 */
    uint16_t dev_count = net_mc_meta.device_count;
    uint32_t dev_start = net_mc_meta.start_device;
    uint16_t dev_name = net_mc_meta.device_name;
    switch (sub_hdr)
    {
        case MC_CMD_BIT_BATCH_READ:
        case MC_CMD_WORD_BATCH_READ:
            MC_Net_Process_ReadCommand(Sour_Sock, Dest_Sock);
            break;

        case MC_CMD_BIT_BATCH_WRITE:
        {
            if( dev_count > (frame_len-12)*2 ) // 一个字节表示2个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        frame_buff[0],
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 

            MC_Net_Process_BitBatchWrite(Sour_Sock, Dest_Sock,
                                            frame_buff + 12,
                                            dev_count, dev_start, dev_name);
            
            break;
        }
        case MC_CMD_WORD_BATCH_WRITE:
        {
            MELSEC_DEBUG("字单位的成批写入 max=160点\r\n");  
            if( dev_count == 0 || dev_count > 160)
            {
                dev_count = 160 ;
            }

            if( dev_count > (frame_len-12)/2 )    // 2字节表示 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        frame_buff[0],
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 

            // 在C200～C255的成批读出/成批写入中， 点数指定为奇数。
            if( net_mc_meta.device_name == MC_FX_CN && dev_start >= 200 && dev_count % 2 != 0) {
                MELSEC_DEBUG("C200～C255 点数指定为奇数 %d  \r\n", dev_count);
                eth_socket[Sour_Sock].Error_Code =   2553;// 软元件的指定有误(向C200～C255的访问，点数指定为奇数)      
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        frame_buff[0],
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            }

            uint8_t dest_buf[ dev_count*2 + 2 ];
            memset(dest_buf, 0, sizeof(dest_buf));   /* VLA不能内联初始化, 改用memset */
            /* 拷贝数据到大端序缓冲区（H+L顺序），帧头后第14字节起（offset=12)*/
            // 03 FF 00 00 1E 00 00 00 20 44 01 00 E7 03 
            if (MC_Net_CopyDataToBigEndianBuffer(dest_buf, frame_buff, 12, dev_count) != 0) {
                return -4;
            }
            
        #ifdef _MELSEC_FX_DEBUG
            {
                uint16_t dbg_end = dev_count * 2;
                for (uint16_t i = 0; i < dbg_end; i++) {
                    MELSEC_DEBUG(" 0x%02X", dest_buf[i]);
                }
            }
            MELSEC_DEBUG("\r\n");
        #endif
            //  0x5220  扩展寄存器  R  
            if (net_mc_meta.device_name == MC_FX_R){  
                MELSEC_FX_Build_E16_write_R_Cmd(Sour_Sock, Dest_Sock,
                                                dev_start,(uint16_t *)dest_buf, dev_count);
            } else {
                MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock,
                                                dev_start,(uint16_t *)dest_buf, dev_count);
            }
            break;
        }
        case MC_CMD_BIT_RANDOM_WRITE:
        {
           
            uint16_t random_num = frame_buff[4];
            uint16_t offset = 6;  /* 随机写入的数据从第7字节开始 */
            MELSEC_DEBUG("位单位的随机写入 总个数=%d  \r\n",random_num);
            if( random_num > (frame_len-6)/7 )    // 7字节表示 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        frame_buff[0],
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 

            for (uint16_t i = 0; i < random_num; i++) {
                uint8_t  bit_val;
                net_mc_meta.device_count  = 1;  //固定 1
                net_mc_meta.start_device = ((uint32_t)frame_buff[offset + 3] << 24) | ((uint32_t)frame_buff[offset + 2] << 16) |
                                            ((uint16_t)frame_buff[offset + 1] << 8)  | frame_buff[offset];
                offset += 4;
                net_mc_meta.device_name = (uint16_t)frame_buff[offset + 1] <<8 | frame_buff[offset ];
                offset += 2;
                bit_val = frame_buff[offset] & 0x01;
                offset += 1;
                MELSEC_DEBUG("%c%c[%d]=%04X(%s)\r\n",
                                net_mc_meta.device_name >>8, 
                                net_mc_meta.device_name ,
                                net_mc_meta.start_device,
                                bit_val, bit_val ? "E7:ON" : "E8:OFF"); 

                MELSEC_FX_Build_E7_E8_ForceCmd(Sour_Sock, Dest_Sock, 
                                                net_mc_meta.device_name,
                                                net_mc_meta.start_device, 
                                                bit_val );
            }
            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
            break;
        }
        case MC_CMD_WORD_RANDOM_WRITE:
        {
            uint16_t random_num = frame_buff[4];
            MELSEC_DEBUG(" 字单位的随机写入 数量 = %d\r\n", random_num);
            if( random_num > (frame_len-6)/8 )    // 8字节表示 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                eth_socket[Sour_Sock].Error_Code =   2558;//        命令、子命令的指定有误
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        frame_buff[0],
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 

            uint16_t offset =  6;   
            for (uint16_t i = 0; i < random_num; i++) 
            {                
   
                net_mc_meta.start_device = ((uint32_t)frame_buff[offset + 3] << 24) | ((uint32_t)frame_buff[offset + 2] << 16) |
                                            ((uint16_t)frame_buff[offset + 1] << 8)  | frame_buff[offset];
                offset += 4;
                net_mc_meta.device_name = (uint16_t)frame_buff[offset + 1] <<8 | frame_buff[offset ];
                offset += 2;
                // 高低字节交换
                uint16_t  data_val = (uint16_t)frame_buff[offset + 1] <<8 | frame_buff[offset + 0];
                MELSEC_DEBUG("%c%c[%d]=%04X  \r\n",
                                net_mc_meta.device_name >>8, 
                                net_mc_meta.device_name ,
                                net_mc_meta.start_device,
                                data_val); 
                //  0x5220  扩展寄存器  R  
                if ( net_mc_meta.device_name == MC_FX_R ){ 
                    MELSEC_FX_Build_E16_write_R_Cmd(Sour_Sock, Dest_Sock,
                                                    net_mc_meta.start_device,
                                                    &data_val, 1);
                } else {
                    MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock,
                                                    net_mc_meta.start_device,
                                                    &data_val, 1);
                }

                offset += 2;
            }
            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
        }break;

        case MC_CMD_REMOTE_RUN:
            MELSEC_DEBUG("请求远程RUN \r\n");
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2360); //0x6023=M8035 强制RUN模式
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2460); //0x6024=M8036 强制RUN指令
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'8',0x2560); //0x6025=M8037 强制STOP指令
            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
            break;
        case MC_CMD_REMOTE_STOP:
            MELSEC_DEBUG("请求远程STOP \r\n");
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2560); //M8037 强制STOP指令 

            break;
        case MC_CMD_PLC_MODEL:
            MELSEC_DEBUG(" PLC 的型号名称 fx3u= 0xF3 \r\n");
            MC_Net_BuildModelResp(Sour_Sock , Dest_Sock, 0);
            break;
        case MC_CMD_ECHO_TEST:
            MELSEC_DEBUG("直接返回到其他节点 \r\n");
            MC_Net_Build_EchoTest_Resp(Sour_Sock , Dest_Sock,frame_buff,frame_len,0 );
            break;

    default:
        MELSEC_DEBUG("未知的MC命令: 0x%02X\r\n", net_mc_meta.sub_header);
        break;
    }
    eth_socket[Sour_Sock].Error_Code = MC_END_NORMAL;
    return 0;
}

 /**
 * @brief 解析MC协议接收命令帧 (二进制码) - 使用全局变量
 * @param Resp_data 接收到的数据帧缓冲区
 * @param frame_buff 返回以太网的数据帧缓冲区
 * @param frame_len  返回以太网的数据帧长度
 * @return 0-成功，-1-参数无效，-2-帧长度不足
 *
 * @note 帧格式解析：
 *   - offset 0: 副标题 (1字节)
 *   - offset 1: PC编号 (1字节)
 *   - offset 2-3: 监视定时器 (2字节)
 *   - offset 4-7: 起始软元件 (4字节)
 *   - offset 8-9: 软元件名 (2字节)
 *   - offset 10-11: 软元件点数 (2字节)
 *   - offset 12: 结束代码 (1字节)
 *   - 总长度: 13字节
 *
 * 示例：00 FF 0A 00 64 00 20 00 00 00 00 00 00
 *       副  PC  定时器  起始软元件  软元件名  点数  结束
 */

int MC_Net_binary_BuildSendResp(uint8_t* Resp_data, uint8_t* frame_buff, uint16_t* frame_len)
{
    /* 合并参数校验：一个分支取代两个 */
    if (frame_buff == NULL || frame_len == NULL || Resp_data == NULL) {
        if (Resp_data == NULL) {
            MELSEC_DEBUG("错误: Resp_data为NULL\r\n");
            return -2;
        }
        return -1;
    }

    uint8_t  sub_hdr = uart_mc_meta.sub_header;  /* 缓存：switch 内复用 */
    uint16_t offset  = 0;
    uint16_t dev_cnt = uart_mc_meta.device_count;

    /* 帧头：副标 | 0x80 + 结束码 0x00 */
    frame_buff[offset++] = sub_hdr | 0x80;
    frame_buff[offset++] = 0x00;

    switch (sub_hdr) {
        /* ─── 写入/控制类：仅返回确认 ─── */
        case MC_CMD_WORD_BATCH_WRITE:   // 字单位的成批写入
        case MC_CMD_REMOTE_STOP:        // 针对可编程控制器请求远程STOP
        {
            MELSEC_DEBUG("写入操作都是返回确认，不需要额外数据 \r\n");
        }break;
        case MC_CMD_REMOTE_RUN:         // 针对可编程控制器请求远程RUN
        {
            MELSEC_DEBUG("写入操作都是返回确认，不需要额外数据 \r\n");
            offset = 0;
        }break;
        case MC_CMD_BIT_RANDOM_WRITE:    //位单位的随机写入
        case MC_CMD_WORD_RANDOM_WRITE:   //字单位的随机写入
        {
            MELSEC_DEBUG("(位/字)单位随机写入\r\n");
            // 检测下一个发送数据是否也是随机写入并且是连续的
            offset = 0;
        }break;
        /* ─── 位批量写入：每2点 → 1字节(高4位|低4位) ─── */
        case MC_CMD_BIT_BATCH_WRITE:
        {
            if( Resp_data[0] == 0x06 ){
                MELSEC_DEBUG("0x06:位批量写入完成, 不需要额外数据 \r\n");
                break;
            }
            uint8_t  bit_offset    = (uint8_t)(uart_mc_meta.start_device % 8);
            uint8_t  aligned_start = 0;
            uint8_t  aligned_end   = 0;   /* 尾16对齐(结束位为16的倍数) */
            MC_Net_BitBatch_GetAlign(uart_mc_meta.start_device, uart_mc_meta.device_count,
                                     &aligned_start, &aligned_end);

            /*
            * 分支1: 单点写入 / 起始8位对齐且结束16位对齐
            */
            if (uart_mc_meta.device_count == 1 ||( aligned_end && aligned_start )  ) {
                MELSEC_DEBUG("单点写入或者(起始8位对齐且结束16位对齐) → 不需要额外数据ack \r\n");
                break;
            }
            /* 分支2:  起始/结束未达对齐条件 → 读-改-写 (Resp_data 为原始PLC字节状态) */
            else {
                MELSEC_DEBUG("起始/结束未达对齐条件, 读回状态合并后写回 \r\n");

                MC_Net_BitBatchWrite_RMW_Merge(Resp_data+1, bit_offset, dev_cnt);
                offset = 0;
            }
        }
        break;
        /* ─── 位批量读出：每2点 → 1字节(高4位|低4位) ─── */
        case MC_CMD_BIT_BATCH_READ: {
            /* MC协议位批量读出最大256点，0按256处理 */
            MELSEC_DEBUG("获取点数 =%d\r\n", dev_cnt);

            const uint16_t out_bytes = (dev_cnt + 1) / 2; /* ceil(dev_cnt/2) */

            /* 边界检查：防止输出缓冲区溢出 */
            if (offset + out_bytes > 256) {
                MELSEC_DEBUG("错误: 位软元件缓冲区空间不足 offset=%u cnt=%u\r\n",
                      offset, dev_cnt);
                *frame_len = offset;
                return -3;
            }
            /* 检测起始地址是否满足8位对齐
             * 若 start_device 非8的倍数，首字节需跳过 bit_offset 位 */
            uint8_t bit_offset = uart_mc_meta.start_device % 8;
            if( bit_offset > 0){
                MELSEC_DEBUG("起始地址不满足8位对齐，首字节需跳过 %d位 \r\n", bit_offset);
            }
            /* 
             * 编码: 每 2 点数 → 1 字节(高nibble|低nibble)
             *   Resp_data = ASCII hex (如 "020000...")
             *   Step1: AsciiHexToUint8 → 1字节 = 8点位状态 (bit0=点0, ...)
             *   Step2: 按bit提取 → 高nibble|低nibble 打包
             *   数据长度: dev_cnt点 → ceil(dev_cnt/8)二进制字节 = dev_cnt/4 ASCII字符
             * 奇数点时末尾高nibble补 0
             */
            const uint8_t *src = Resp_data+1;   /* ASCII hex: "02", "00", "00", ... */
            uint8_t *dst = &frame_buff[offset];
            uint16_t remaining = dev_cnt;
            uint8_t has_pending = 0;          /* 是否留有未配对的高nibble */
            uint8_t pending_hi   = 0;         /* 暂存的高nibble值 */

            while (remaining > 0) {
                /* 1个hex字节(2 ASCII char) → 1二进制字节 → 最多8个点位 */
                uint8_t bin_byte = AsciiHexToUint8(src[0], src[1]);
                src += 2;

                /* 首字节从 bit_offset 开始，后续字节从 0 开始 */
                uint8_t start_bit = bit_offset;
                bit_offset = 0;                     /* 仅首字节生效 */

                uint8_t available = 8 - start_bit;  // 剩余点数
                // 2点数 → 1字节，若剩余点数不足2点，取剩余点数
                uint8_t points_in_byte = (remaining < available) ? (uint8_t)remaining : available;

                for (uint8_t i = 0; i < points_in_byte; i += 2) 
                {
                    uint8_t bit = start_bit + i;
                    uint8_t hi = (bin_byte >> bit) & 1;              /* 点N(高位bit) */

                    if (has_pending) {
                        /* 上一轮遗留的高nibble + 当前bit → 完整1字节 */
                        *dst++ = (pending_hi << 4) | hi;
                        has_pending = 0;
                    } else if (i + 1 < points_in_byte) {
                        /* 正常配对: 偶数bit → 高nibble, 奇数bit → 低nibble */
                        uint8_t lo = (bin_byte >> (bit + 1)) & 1;
                        *dst++ = (hi << 4) | lo;
                    } else {
                        /* 奇数位无法配对，留存到下一字节 */
                        pending_hi = hi;
                        has_pending = 1;
                    }
                }
                remaining -= points_in_byte;
            }

            /* 全部点位处理完，还有留存的高nibble时单独输出(lo=0) */
            if (has_pending) {
                *dst++ = pending_hi << 4;
            }
            offset = (uint16_t)(dst - frame_buff);
            break;
        }

        /* ── 字批量读出：4 ASCII hex → 2 字节 ─── */
        case MC_CMD_WORD_BATCH_READ: {
            if (offset + dev_cnt * 2 > 256) {
                MELSEC_DEBUG("错误: 字软元件缓冲区空间不足 offset=%u count=%u\r\n",
                      offset, dev_cnt);
                *frame_len = offset;
                return -3;
            }

            MELSEC_DEBUG("读出 %c%c[%d]  字软元件-- 数量:%d\r\n",
                  uart_mc_meta.device_name>>8, uart_mc_meta.device_name,
                  uart_mc_meta.start_device, dev_cnt);

            {
                const uint8_t *src = Resp_data+1;
                uint8_t *dst = &frame_buff[offset];
                for (uint16_t i = 0; i < dev_cnt; i++, src += 4) {
                    *dst++ = AsciiHexToUint8(src[0], src[1]);
                    *dst++ = AsciiHexToUint8(src[2], src[3]);
                }
                offset = (uint16_t)(dst - frame_buff);
            }
            break;
        }

        /* ─── 型号/回环：已在上层应答，此处空过 ─── */
        case MC_CMD_PLC_MODEL:
        case MC_CMD_ECHO_TEST:
            MELSEC_DEBUG(" 已经在接收网口数据是就应答了.\r\n");
            offset = 0;
            break;

        default:
            MELSEC_DEBUG("未知的MC命令: 0x%02X\r\n", sub_hdr);
            break;
    }

    *frame_len = offset;
    return 0;
}



// ==================== MC协议数据结构定义 ASCII码通信时的格式 ====================
//2) ASCII码通信时
//如果没有特殊说明， 各说明中的值将被转换成16进制数的ASCII码， 按照指定顺序(H-L)进行收发。
/**
 * @brief 解析MC协议接收命令帧 (ASCII) - 使用全局变量
 * @param frame_buff 接收到的数据帧缓冲区
 * @param frame_len 数据帧长度
 * @return 0-成功，-1-参数无效，-2-帧长度不足
 *
 * @note 帧格式解析：
 *   - offset 0-1: 副标题 (2字节)
 *   - offset 2-3: PC编号 (2字节)
 *   - offset 4-7: 监视定时器 (4字节) 
 *   - offset 8-15: 起始软元件 (8字节)
 *   - offset 16-17: 软元件名 (2字节)
 *   - offset 18-19: 软元件点数 (2字节)
 *   - offset 20-21: 结束代码 (2字节)
 *   - 总长度: 22字节
 *
 * 示例：3030 4646 30303041 34443230 30303030 30303634 3043  3030   
 *       副    PC  定时器   起始软元件        软元件名   点数   结束
 */

int MC_Net_ASCII_ParseRecvResp(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t *frame_buff, uint16_t frame_len)
{
    uint16_t offset;

    if (frame_buff == NULL || (frame_len < 6)) {
        MELSEC_DEBUG(" ASCII 帧长度不足 (len=%d, min = 6)\r\n", frame_len);
        //报错 处理 命令字节长度不是规定长度     57H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                net_mc_meta.pc_number,
                                MC_END_ILLEGAL_POINT_COUNT ); 
        return -2;
    }
    offset = 0;
    /* === 帧头解析: ASCII hex对 → 二进制值 === */
    net_mc_meta.sub_header = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
    offset += 2;
    /* 缓存频繁访问的全局字段，减少全局结构体解引用 */
    uint8_t  sub_hdr   = net_mc_meta.sub_header;
    uint16_t dev_count = 0;
    uint32_t dev_start = 0;
    net_mc_meta.pc_number = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
    offset += 2;

    //PC号错误(10H):指定了FF以外的PC号
    if(net_mc_meta.pc_number != 0xFF)
    {
        MELSEC_DEBUG("PC编号错误: 0x%02X\r\n", net_mc_meta.pc_number);
        //报错 处理 PC号错误(10H):指定了FF以外的PC号      5BH  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                net_mc_meta.sub_header,
                                MC_END_UNABLE_TO_COMM ); 
        return -3;
    }

    net_mc_meta.monitor_timer =
                (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 8) |
                AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3]);
    offset += 4;

    #ifdef _MELSEC_FX_DEBUG
            MELSEC_DEBUG("=== MC ASCII接收命令解析 ===\r\n");
            MELSEC_DEBUG("副标题: 0x%02X --", sub_hdr);
            MELSEC_DEBUG("PC编号: 0x%02X --", net_mc_meta.pc_number);
            MELSEC_DEBUG("监视定时器: 0x%04X\r\n", net_mc_meta.monitor_timer);
    #endif

    // 读取命令帧长度不足 这个三个命令( frame_len == 8 )
    if( sub_hdr == MC_CMD_REMOTE_RUN  ||   //     = 0x13,   // 针对可编程控制器请求远程RUN
        sub_hdr == MC_CMD_REMOTE_STOP ||  //      = 0x14,   // 针对可编程控制器请求远程STOP
        sub_hdr == MC_CMD_PLC_MODEL   )   //      = 0x15,   // PLC可编程控制器的型号名 )    // 8字节表示 1个点数
    {
        /* 远程RUN(13H)/STOP(14H)=4*2字节, 读写命令帧=13*2字节 */
        if (frame_len < 8) {
            MELSEC_DEBUG(" ASCII 帧长度不足 (sub_hdr:%d,len=%d, min=8)\r\n",sub_hdr, frame_len);
            //报错 处理 命令字节长度不是规定长度     57H  
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                    net_mc_meta.pc_number,
                                    MC_END_ILLEGAL_POINT_COUNT ); 
            return -2;
        }

    }else{
        if (frame_len > 8) 
        {
            net_mc_meta.device_name =
                (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 8) |
                (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3])) ;
            offset += 4;

            net_mc_meta.start_device =
                (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 24) |
                (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3]) << 16) |
                (AsciiHexToUint8(frame_buff[offset + 4], frame_buff[offset + 5]) << 8) |
                AsciiHexToUint8(frame_buff[offset + 6], frame_buff[offset + 7]);
            offset += 8;

            net_mc_meta.device_count = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
            offset += 4;
            
            /* 缓存频繁访问的全局字段，减少全局结构体解引用 */
            dev_count = net_mc_meta.device_count;
            dev_start = net_mc_meta.start_device;
        
            #ifdef _MELSEC_FX_DEBUG
                MELSEC_DEBUG("软元件名: %c%c --", net_mc_meta.device_name, net_mc_meta.device_name>>8);
                MELSEC_DEBUG("起始软元件: 0x%08X\r\n", dev_start);
                MELSEC_DEBUG("软元件点数: 0x%04X\r\n", dev_count);
                MELSEC_DEBUG("====================\r\n");
            // MELSEC_DEBUG("device_type:0x%02X\r\n", dev_type);
            #endif

        }else{
            MELSEC_DEBUG(" ASCII 帧长度不足 (len=%d, min=4)\r\n", frame_len);
            //报错 处理 命令字节长度不是规定长度     57H  
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                    frame_buff[0],
                                    MC_END_ILLEGAL_POINT_COUNT ); 
            return -1;
        } 
    } 
    switch (sub_hdr)
    {
        case MC_CMD_BIT_BATCH_READ:
        case MC_CMD_WORD_BATCH_READ:
            MC_Net_Process_ReadCommand(Sour_Sock, Dest_Sock);
            break;

        case MC_CMD_BIT_BATCH_WRITE:
        {
            MELSEC_DEBUG("ASCII 位单位的成批写入 \r\n");
            if( dev_count > (frame_len-24)  )    // 一个字节表示1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 
   
            uint8_t Ascii_parse_buf[ dev_count/2 + 2 ];
            for (uint8_t idx = 0; idx < dev_count/2 + 1; idx++)
            {
                //AsciiHexToUint8('0', '1') -> 0x01
                Ascii_parse_buf[idx] = AsciiHexToUint8(frame_buff[offset], frame_buff[offset+1]);
                offset += 2;
                MELSEC_DEBUG("%02X ", Ascii_parse_buf[idx]);
            }
            MELSEC_DEBUG("\r\n");

            MC_Net_Process_BitBatchWrite(Sour_Sock, Dest_Sock,
                                            Ascii_parse_buf,
                                            dev_count, dev_start, 
                                            net_mc_meta.device_name);
            
            break;
        }
        case MC_CMD_WORD_BATCH_WRITE:
        {
            MELSEC_DEBUG("ASCII 字单位的成批写入 \r\n");
            /* 硬上限对齐 BuildE1xWriteCommon 的 max_count(=128);
             * 同时避免依赖不可信输入的运行时 VLA 撑爆栈 */
            if( dev_count == 0 || dev_count > MC_ASCII_MAX_WORD_POINTS)
            {
                dev_count = MC_ASCII_MAX_WORD_POINTS ;
            }

            if( dev_count > (frame_len-24)/4 )    // 4字节表示 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 

            // 在C200～C255的成批读出/成批写入中， 点数指定为奇数。
            if( net_mc_meta.device_name == MC_FX_CN && dev_start >= 200 && dev_count % 2 != 0) {
                MELSEC_DEBUG("C200～C255 点数指定为奇数 %d  \r\n", dev_count);
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            }

            /* 固定编译期大小: 每字 2 字节(低/高), +2 余量; dev_count 已 cap 到 MC_ASCII_MAX_WORD_POINTS */
            uint8_t Ascii_parse_buf[ MC_ASCII_MAX_WORD_POINTS*2 + 2 ] ;
            memset(Ascii_parse_buf,0,sizeof(Ascii_parse_buf));
            /* 指针递推代替 idx*2 计算 */
            {
                const uint8_t *src = frame_buff + offset;
                for (uint16_t idx = 0; idx < dev_count; idx++) {
                    Ascii_parse_buf[idx*2] = AsciiHexToUint8(src[0], src[1]);
                    Ascii_parse_buf[idx*2+1] = AsciiHexToUint8(src[2], src[3]);
                    src += 4;
                    MELSEC_DEBUG("%02X %02X ", Ascii_parse_buf[idx*2], Ascii_parse_buf[idx*2+1]);
                }
                offset = (uint16_t)(src - frame_buff);
            }
            MELSEC_DEBUG("\r\n");

            if (net_mc_meta.device_name == MC_FX_R) {
                MELSEC_FX_Build_E16_write_R_Cmd(Sour_Sock, Dest_Sock,
                                        dev_start, (uint16_t *)Ascii_parse_buf, dev_count);
            } else {
                MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock,
                                        dev_start, (uint16_t *)Ascii_parse_buf, dev_count);
            }
            break;
        }
        case MC_CMD_BIT_RANDOM_WRITE:
        {
            
            offset = 8;
            uint16_t random_num = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
            MELSEC_DEBUG("ASCII 位单位的随机写入 点数=%d \r\n",random_num);
            if( random_num > (frame_len-12)/14 )    // 14字节表示 随机位 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 
            offset += 4;

            for (uint16_t i = 0; i < random_num; i++) {
 
                uint8_t  data;

                net_mc_meta.device_name =
                                        (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 8) |
                                        (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3])) ;                            
                offset += 4;
                net_mc_meta.start_device =
                                        (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 24) |
                                        (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3]) << 16) |
                                        (AsciiHexToUint8(frame_buff[offset + 4], frame_buff[offset + 5]) << 8) |
                                        AsciiHexToUint8(frame_buff[offset + 6], frame_buff[offset + 7]);
                offset += 8;
                data = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
                offset += 2;

                MELSEC_DEBUG("软元件:%c%c[%d]=%04X(%d)\r\n",
                                net_mc_meta.device_name>>8,
                                net_mc_meta.device_name,
                                net_mc_meta.start_device,
                                data,data);

                MELSEC_FX_Build_E7_E8_ForceCmd(Sour_Sock, Dest_Sock,
                                                net_mc_meta.device_name,
                                                net_mc_meta.start_device , 
                                                (data & 0x01));
            }
            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
            break;
        }
        case MC_CMD_WORD_RANDOM_WRITE:
        {
            offset = 8;
            uint16_t random_num = AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]);
            MELSEC_DEBUG("字单位的随机写入 数量=%d \r\n", random_num);
            if( random_num > (frame_len-12)/16 )    // 16字节表示 随机位 1个点数
            {
                MELSEC_DEBUG("设置的数据点数过多 (len=%d, min=4)\r\n", frame_len);
                //报错 处理 命令字节长度不是规定长度     57H  
                ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_ILLEGAL_POINT_COUNT ); 
                return -1;
            } 
            offset += 4;

            for (uint16_t i = 0; i < random_num; i++) {
  
                uint16_t data;

                net_mc_meta.device_name =
                                        (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 8) |
                                        (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3])) ;                            
                offset += 4;
                net_mc_meta.start_device =
                                        (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 24) |
                                        (AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3]) << 16) |
                                        (AsciiHexToUint8(frame_buff[offset + 4], frame_buff[offset + 5]) << 8) |
                                        AsciiHexToUint8(frame_buff[offset + 6], frame_buff[offset + 7]);
                offset += 8;
                data =
                    (AsciiHexToUint8(frame_buff[offset], frame_buff[offset + 1]) << 8) |
                     AsciiHexToUint8(frame_buff[offset + 2], frame_buff[offset + 3]);
                offset += 4;
                MELSEC_DEBUG("软元件 %c%c[%d]=%04X(%d)\r\n", 
                                net_mc_meta.device_name>>8, 
                                net_mc_meta.device_name, 
                                net_mc_meta.start_device,
                                 data, data);
                uint8_t Ascii_parse_buf[8] = {0};
                Ascii_parse_buf[0] = data >> 8;
                Ascii_parse_buf[1] = data;
                MELSEC_DEBUG("%02X %02X \r\n", Ascii_parse_buf[0], Ascii_parse_buf[1]);

                if (net_mc_meta.device_name == MC_FX_R)  {
                    MELSEC_FX_Build_E16_write_R_Cmd(Sour_Sock, Dest_Sock,
                                                    net_mc_meta.start_device,
                                                    (uint16_t *)Ascii_parse_buf, 1);
                } else {
                    MELSEC_FX_BuildE10WriteParamCmd(Sour_Sock, Dest_Sock,
                                                    net_mc_meta.start_device, 
                                                    (uint16_t *)Ascii_parse_buf, 1);
                }
            }

            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
            break;
        }
        case MC_CMD_REMOTE_RUN:
            MELSEC_DEBUG("请求远程RUN \r\n");
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2360); //0x6023=M8035 强制RUN模式
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2460); //0x6024=M8036 强制RUN指令
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'8',0x2560); //0x6025=M8037 强制STOP指令
 
            // 直接发送网口响应
            ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                                        sub_hdr,
                                        MC_END_NORMAL ); 
            break;
        case MC_CMD_REMOTE_STOP:
            MELSEC_DEBUG("请求远程STOP \r\n");
            BuildRemoteControlCommand(Sour_Sock , Dest_Sock,'7',0x2560); //M8037 强制STOP指令 
            break;
        case MC_CMD_PLC_MODEL:
 
            MELSEC_DEBUG(" PLC 的型号名称 fx3u= 0xF3 \r\n");
            MC_Net_BuildModelResp(Sour_Sock , Dest_Sock,1 );
            break;
        case MC_CMD_ECHO_TEST:
            MELSEC_DEBUG("直接返回到其他节点 \r\n");
            MC_Net_Build_EchoTest_Resp(Sour_Sock , Dest_Sock,frame_buff,frame_len,1 );
            break;

    default:
        MELSEC_DEBUG("未知的MC命令: 0x%02X\r\n", net_mc_meta.sub_header);
        break;
    }
 
    eth_socket[Sour_Sock].Error_Code = MC_END_NORMAL;
    return 0;
}

/**
 * @brief 构造MC协议发送响应帧 (ASCII) - 使用全局变量
 * @param frame_buff 输出：构造的数据帧缓冲区
 * @param frame_len 输出：数据帧长度
 * @return 0-成功，-1-参数无效，-2-帧长度不足
 *
 * @note 帧格式：副标题(2) + 结束代码(2) + 响应数据(4*N)
 *   - offset 0-1: 副标题 (2字节ASCII)
 *   - offset 2-3: 结束代码 (2字节ASCII)
 *   - offset 4~(3+N*4): 响应数据 (4*N字节ASCII，每项4字节)
 *   - 总长度: 4 + N*4 字节
 *
 * 示例：3830 3030 31303331 31303331
 *       副    结束代码  响应数据1 响应数据2
 */
int MC_Net_ASCII_BuildSendResp(uint8_t *Resp_data,  uint8_t* frame_buff, uint16_t* frame_len)
{
    // 参数验证
    if (frame_buff == NULL || frame_len == NULL) {
        return -1;
    }
    /* 读操作返回位软元件(X、Y、M、S、T、C)状态 */
    if (Resp_data == NULL) {
        MELSEC_DEBUG("错误: Resp_data为NULL\r\n");
        return -2;
    }
    uint16_t offset = 0;
    // 副标题 | 0x80   b7: 0:命令/ 1:响应标志位
    uint8_t header = uart_mc_meta.sub_header | 0x80;
    // 副标题   (2字节) 
    byte_to_hex_chars(header, &frame_buff[offset] );
    offset += 2;
    // 结束代码 (2字节) 00H: 正常结束
    byte_to_hex_chars(0x00, &frame_buff[offset] );
    offset += 2;

    // 根据解析得到的数据开始发送指令到PLC获取/设置数据
    switch (uart_mc_meta.sub_header) {

        /* ─── 写入/控制类：仅返回确认 ─── */
        case MC_CMD_WORD_BATCH_WRITE:   // 字单位的成批写入
        case MC_CMD_REMOTE_STOP:        // 针对可编程控制器请求远程STOP
        {
            MELSEC_DEBUG("写入操作都是返回确认，不需要额外数据 \r\n");
        }break;
        case MC_CMD_REMOTE_RUN:         // 针对可编程控制器请求远程RUN
        {
            MELSEC_DEBUG("写入操作都是返回确认，不需要额外数据 \r\n");
            offset = 0;
        }break;
        case MC_CMD_BIT_RANDOM_WRITE:    //位单位的随机写入
        case MC_CMD_WORD_RANDOM_WRITE:   //字单位的随机写入
        {
            MELSEC_DEBUG("(位/字)单位随机写入\r\n");
            // 检测下一个发送数据是否也是随机写入并且是连续的
            offset = 0;
        }break;
        case MC_CMD_BIT_BATCH_WRITE:    // 位单位的成批写入
        {
            if(Resp_data[0] == 0x06 ){
                MELSEC_DEBUG("0x06 写入操作完成，不需要额外数据 \r\n");
                break;  
            }
            uint8_t  bit_offset    = (uint8_t)(uart_mc_meta.start_device % 8);
            uint8_t  aligned_start = 0;
            uint8_t  aligned_end   = 0;   /* 尾16对齐(结束位为16的倍数) */
            MC_Net_BitBatch_GetAlign(uart_mc_meta.start_device, uart_mc_meta.device_count,
                                     &aligned_start, &aligned_end);

            /*
            * 分支1: 单点写入 / 起始8位对齐且结束16位对齐
            */
            if (uart_mc_meta.device_count == 1 ||( aligned_end && aligned_start )  ) {
                MELSEC_DEBUG("单点写入或者(起始8位对齐且结束16位对齐) → 不需要额外数据ack \r\n");
                break;
            }
            /* 分支2:  起始/结束未达对齐条件 → 读-改-写 (Resp_data 为原始PLC字节状态) */
            else {
                MELSEC_DEBUG("起始/结束未达对齐条件, 读回状态合并后写回 \r\n");
                MC_Net_BitBatchWrite_RMW_Merge(Resp_data+1, bit_offset, uart_mc_meta.device_count);
            }
            break;
        }
        case MC_CMD_BIT_BATCH_READ: {   // 位单位的成批读出
            

            /* 检测起始地址是否满足8位对齐
             * 若 start_device 非8的倍数，首字节需跳过 bit_offset 位 */
            uint8_t bit_offset = uart_mc_meta.start_device % 8;
            if( bit_offset > 0){
                MELSEC_DEBUG("ASCII 起始地址不满足8位对齐，首字节需跳过 %d位 \r\n", bit_offset);
            }
            /* 
             * 编码: 每 1 点数 → 1 个 ASCII 字符('0'/'1')
             *   Resp_data = ASCII hex (如 "020000...")
             *   Step1: AsciiHexToUint8 → 1字节 = 8点位状态 (bit0=点0, ...)
             *   Step2: 按bit提取，逐点展开为 '0'/'1'
             *   数据长度: dev_cnt 点 = dev_cnt ASCII 字符
             *   注意: 软元件点数为奇数时，末尾附加 1 个空数据 '0'(30H)
             */
            uint16_t dev_cnt = uart_mc_meta.device_count;
            MELSEC_DEBUG("ASCII 获取点数 =%d\r\n", dev_cnt);
   
            const uint8_t *src = Resp_data+1;   /* ASCII hex: "02", "00", "00", ... */
            uint8_t *dst = &frame_buff[offset];
            uint16_t remaining = dev_cnt;

            while (remaining > 0) {
                /* 1个hex字节(2 ASCII char) → 1二进制字节 → 最多8个点位 */
                uint8_t bin_byte = AsciiHexToUint8(src[0], src[1]);
                src += 2;

                /* 首字节从 bit_offset 开始，后续字节从 0 开始 */
                uint8_t start_bit = bit_offset;
                bit_offset = 0;                     /* 仅首字节生效 */

                uint8_t available = 8 - start_bit;  // 剩余点数
                uint8_t points_in_byte = (remaining < available) ? (uint8_t)remaining : available;

                for (uint8_t i = 0; i < points_in_byte; i++) {
                    uint8_t bit = start_bit + i;
                    uint8_t bit_status = (bin_byte >> bit) & 1;   /* bit0=点0, ... */
                    *dst++ = '0' + bit_status;
                }
                remaining -= points_in_byte;
            }

            /* 软元件点数为奇数时，末尾附加 1 个空数据 '0'(30H) */
            if (dev_cnt & 1) {
                *dst++ = '0';
            }

            offset = (uint16_t)(dst - frame_buff);
            break;
        }

        case MC_CMD_WORD_BATCH_READ: {  // 字单位的成批读出
            /* 读操作返回字软元件状态 */
            uint16_t device_count = uart_mc_meta.device_count;
            if (offset + device_count * 4 > 512) {
                MELSEC_DEBUG("错误: 字软元件缓冲区空间不足 offset=%u count=%u\r\n",
                      offset, device_count);
                *frame_len = offset;
                return -3;
            }
 
            MELSEC_DEBUG("ASCII 读出 %c%c[%d]  字软元件-- 数量:%d\r\n",
                  uart_mc_meta.device_name, uart_mc_meta.device_name>>8,
                  uart_mc_meta.start_device, device_count);
            /**
             * ASCII 字数据格式：每个字 = 2 字节 = 4 个 ASCII 字符
             *   Resp_data[0] 为前缀，数据从 Resp_data[1] 起
             *   输入: 第 w 个字 Resp_data[1+w*4 .. 4+w*4] (高字节在前, 低字节在后)
             *   输出: 高8位与低8位互换 → [低字节][高字节]
             */
            for (uint16_t w = 0; w < device_count; w++) {
                uint16_t idx = (uint16_t)(1 + w * 4);   /* 第 w 个字的起始位置 */
                MELSEC_DEBUG(" %C%C%C%C",Resp_data[idx+2], Resp_data[idx+3],
                                Resp_data[idx], Resp_data[idx+1] );
                /* 高8位与低8位互换（字节序翻转）: */
                frame_buff[offset + 0] = Resp_data[idx + 2];
                frame_buff[offset + 1] = Resp_data[idx + 3];
                frame_buff[offset + 2] = Resp_data[idx + 0];
                frame_buff[offset + 3] = Resp_data[idx + 1];
                offset += 4;
            }
            MELSEC_DEBUG("\r\n");
            break;
        }
 
        case MC_CMD_PLC_MODEL: {       // PLC可编程控制器的型号名称
            MELSEC_DEBUG(" PLC 的型号名称 \r\n");
 
            break;
        }

        case MC_CMD_ECHO_TEST: {       // 从其他节点接收的字符直接返回到其他节点
 
            MELSEC_DEBUG("实现ECHO测试响应 \r\n");
            break;
        }

        default:
            MELSEC_DEBUG("未知的MC命令: 0x%02X\r\n", net_mc_meta.sub_header);
            break;
    }

    *frame_len = offset;

    // 打印构造结果 
    MELSEC_DEBUG("=== MC发送 ASCII 响应构造 ===\r\n");
    MELSEC_DEBUG("帧长度: %u 字节\r\n", offset);
    MELSEC_DEBUG("副标题: 0x%02X\r\n", frame_buff[0]);
    MELSEC_DEBUG("结束代码: 0x%02X\r\n", frame_buff[1]);
    return 0;
}

int MC_Net_BuildModelResp(uint8_t Sour_Sock , uint8_t Dest_Sock,uint8_t type)
{
    if( Sour_Sock > 8 || Dest_Sock > 20) {
        return 1;
    }
    /* 局部发送缓冲区: 二进制 4B / ASCII 8B */
    uint8_t tx_buf[16];
    uint16_t offset = 0;
    // 打包二进制格式数据
    if( type  == 0x00)
    {
       // 副标题 | 0x80 (1字节)  b7: 0:命令/ 1:响应标志位
       tx_buf[offset++] = 0x15 | 0x80;
       // 空代码 (1字节)
       tx_buf[offset++] = 0x00;
       //设备型号 FX3U/FX3UC :0xF3
       tx_buf[offset++] = 0xF3;
       // 结束代码 (1字节)
       tx_buf[offset++] = 0x00;
   
    }
    else{ // 打包ASCII格式数据
       
        // 副标题 | 0x80 (2字节)  b7: 0:命令/ 1:响应标志位
        byte_to_hex_chars ( (0x15 | 0x80 ), &tx_buf[offset] );
        offset += 2;
        // 空代码 (2字节)
        tx_buf[offset++] = 0x30;
        tx_buf[offset++] = 0x30;
        //设备型号 FX3U/FX3UC :0xF3 (2字节)
        byte_to_hex_chars( 0xF3,&tx_buf[offset] );
        offset += 2;
        // 结束代码 (2字节)
        tx_buf[offset++] = 0x30;
        tx_buf[offset++] = 0x30;
    }

    // 根据串口发送时的配置socket ID，来发送网络数据
    ETH_SOCKET *eth_ptr = &eth_socket[Sour_Sock];
    // 直接返回设备型号
    ethernet_send( Sour_Sock, Dest_Sock, tx_buf,offset,eth_ptr->destip, eth_ptr->destport);

    return 0;
}

// 折返测试 响应
int MC_Net_Build_EchoTest_Resp(uint8_t Sour_Sock , uint8_t Dest_Sock,uint8_t *Resp_data, uint16_t Resp_len, uint8_t type)
{
    if( Sour_Sock > 8 || Dest_Sock > 20) {
        return 1;
    }
    uint16_t offset = 0;
    // 打包二进制格式数据
    if( type  == 0x00)
    {
       // 副标题 | 0x80 (1字节)  b7: 0:命令/ 1:响应标志位
       Resp_data[2] = (Resp_data[0] | 0x80);
       // 结束代码 (1字节)
       Resp_data[3] = 0x00;
       offset = 2;
   
    }
    else{ // 打包ASCII格式数据
       
        // 副标题 | 0x80 (1字节)  b7: 0:命令/ 1:响应标志位
        Resp_data[4] = ( Resp_data[0] + 8);
        // 结束代码 (2字节)
        Resp_data[6] = 0x30;
        Resp_data[7] = 0x30;
        offset = 4;
    }
    // 根据串口发送时的配置socket ID，来发送网络数据
    ETH_SOCKET *eth_ptr = &eth_socket[Sour_Sock];
    // 直接返回设备型号
    ethernet_send( Sour_Sock,Dest_Sock,Resp_data+offset,Resp_len-offset,eth_ptr->destip, eth_ptr->destport);

    return 0;
}

//异常响应 (异常结束时)
int MC_Net_BuildSendAbnormalResp(uint8_t Sour_Sock , uint8_t Dest_Sock )
{
    if( Sour_Sock > 8 || Dest_Sock > 20) {
        return 1;
    }
    // 打包二进制格式数据
    uint16_t offset = 0;
    // 根据socket ID 找到对应的协议类型
    uint8_t pro_t = eth_socket[Sour_Sock].Pro_Type ;
    // 根据串口发送时的配置socket ID，来发送网络数据
    ETH_SOCKET *eth_ptr = &eth_socket[Sour_Sock];
    // 0xA6: TCP MC协议 0xA7: UDP MC协议
    if( pro_t == PRO_TCPC_MC  || pro_t ==  PRO_UDPC_MC )
    {
        /* 局部发送缓冲区: 二进制 3B / ASCII 6B */
        uint8_t tx_buf[16];
        // 重新打包成MC协议格式的数据
        if( uart_mc_meta.Format_Code == 0x00 ){
            // 副标题 | 0x80 (1字节)  b7: 0:命令/ 1:响应标志位
            tx_buf[offset++] = uart_mc_meta.sub_header | 0x80;
            // 结束代码 (1字节) 
            tx_buf[offset++] = 0x5B;  //(异常结束时)
            // 异常代码
            tx_buf[offset++] = 0x12;   
        }else{
            // 打包ASCII格式数据
            // 副标题 | 0x80 (2字节)  b7: 0:命令/ 1:响应标志位
            byte_to_hex_chars( (uart_mc_meta.sub_header | 0x80 ),tx_buf);
            offset+=2;
            // 结束代码 (2字节) 
            byte_to_hex_chars( 0x5B,tx_buf+offset);
            offset+=2;
            // 异常代码  (2字节) 
            byte_to_hex_chars( 0x12,tx_buf+offset);
            offset+=2;
        }
        // 直接返回设备型号
        ethernet_send(Sour_Sock,Dest_Sock,tx_buf,offset,eth_ptr->destip, eth_ptr->destport);

        MELSEC_DEBUG("=== MC异常响应 ===\r\n");
    }
    return 0;
}


// ==================== MC协议数据   转化 PLC 通信时的格式 ====================
/**
 * @brief 将响应数据重新打包为三菱MC协议格式并发送
 *
 * 根据当前接收响应的格式码（uart_mc_meta.Format_Code）选择对应的打包方式：
 * - 格式码 0x00：调用二进制格式打包函数
 * - 其他：调用ASCII格式打包函数
 *
 * @param Resp_data  指向待打包的响应数据缓冲区
 * @param txbuf      指向发送缓冲区，用于存放打包后的MC协议帧数据
 * @param size       输出：打包后的数据总长度（字节）
 * @return 固定返回0，表示打包成功
 */
int MC_Net_BuildSendResp( uint8_t *Resp_data,  uint8_t *txbuf, uint16_t * size)
{
    // 重新打包成MC协议格式的数据
    if( uart_mc_meta.Format_Code == 0x00 ){
        // 打包二进制格式数据
        MC_Net_binary_BuildSendResp(Resp_data, txbuf, size);
    }else{
        // 打包ASCII格式数据
        MC_Net_ASCII_BuildSendResp(Resp_data, txbuf, size);
    }

    return 0;
}


