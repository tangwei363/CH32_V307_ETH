#include "melsec_fx_tables.h"
#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include <stdio.h>
 

/***  批量 30读取/40写入 指令通道 ***/
const MELSEC_FX_TABLES_T FX_30_40_Tables[] = {
    {16, type_D,    0x4420,     0000,   7999,   0x1000,    0x17FF},    //数据寄存器  D0000~D7999
    {16, type_D,    0x4420,     8000,   8511,   0x0E00,    0x0FFF},    //数据寄存器  D8000~D8511
    {16, type_R,    0x5220,     0000,   32767,  0x0000,    0x7FFF},    //扩展寄存器  R0～R32767
    {16, type_T,    0x544E,     0000,   511,    0x0800,    0x09FF},    //定时器  当前值  TN:T0～T511
    {1,  type_TO,   0x5453,     0000,   511,    0x00C0,    0x00DF},    //定时器  触点   TS:T0～T511
    {16, type_C,    0x434E,     0000,   199,    0x0A00,    0x0B8F},    //计数器  当前值 CN:C0～C199
    {32, type_C,    0x434E,     200,    255,    0x0C00,    0x0CDF},    //计数器  当前值 CN:C200～C255
    {1,  type_CO,   0x4353,     0000,   255,    0x01C0,    0x01DF},    //计数器  触点   CS:C0～C255
    {1,  type_X,    0x5820,     0000,   255,    0x0080,    0x009F},    //输入  X000～X377 -->10进制:X000～X255
    {1,  type_Y,    0x5920,     0000,   255,    0x00A0,    0x00BF},    //输出  Y000～Y377 -->10进制:Y000～Y255
    {1,  type_M,    0x4D20,     0000,   7679,   0x0100,    0x01BF},    //辅助继电器   M0～M7679
    {1,  type_M,    0x4D20,     8000,   8511,   0x01E0,    0x01FF},    //辅助继电器   M8000～M8511
    {1,  type_S,    0x5320,     0000,   4095,   0x0000,    0x01FF},    //状态  触点   S0～S4095
};

/***  批量 E0读取/E1写入 指令通道 ***/
const MELSEC_FX_TABLES_T FX_E0_E1_Tables[] = {
    {16, type_D,    0x4420,     0000,   7999,   0x4000,    0x7E7F},    //数据寄存器  D0000~D7999
    {16, type_D,    0x4420,     8000,   8511,   0x8000,    0x83FF},    //数据寄存器  D8000~D8511
    {16, type_R,    0x5220,     0000,   32767,  0x0000,    0x7FFF},    //扩展寄存器  R0～R32767
    {16, type_T,    0x544E,     0000,   511,    0x1000,    0x13FF},    //定时器  当前值  TN:T0～T511
    {1,  type_TO,   0x5453,     0000,   511,    0x8C60,    0x8C9F},    //定时器  触点   TS:T0～T511
    {16, type_C,    0x434E,     0000,   199,    0x0A00,    0x0B8F},    //计数器  当前值 CN:C0～C199
    {32, type_C,    0x434E,     200,    255,    0x0C00,    0x0CDF},    //计数器  当前值 CN:C200～C255
    {1,  type_CO,   0x4353,     0000,   255,    0x8C40,    0x8C5F},    //计数器  触点   CS:C0～C255
    {1,  type_X,    0x5820,     0000,   255,    0x8CA0,    0x8CBF},    //输入  X000～X377 -->10进制:X000～X255
    {1,  type_Y,    0x5920,     0000,   255,    0x8BC0,    0x8BDF},    //输出  Y000～Y377 -->10进制:Y000～Y255
    {1,  type_M,    0x4D20,     0000,   7679,   0x8800,    0x8BBF},    //辅助继电器   M0～M7679
    {1,  type_M,    0x4D20,     8000,   8511,   0x8C00,    0x8C3F},    //辅助继电器   M8000～M8511
    {1,  type_S,    0x5320,     0000,   4095,   0x8CE0,    0x8EDF},    //状态  触点   S0～S4095
};

/***  批量 F5读取  指令通道 ***/
const MELSEC_FX_TABLES_T FX_F5_Read_Tables[] = {
    {16, type_D,   0x4420,    0000,   7999,   0x4000,    0x7E7F},    //数据寄存器  D0000~D7999
    {16, type_D,   0x4420,    8000,   8511,   0x8000,    0x83FF},    //数据寄存器  D8000~D8511
    {16, type_R,   0x5220,    0000,   32767,  0x0000,    0x7FFF},    //扩展寄存器  R0～R32767
    {16, type_T,   0x544E,    0000,   511,    0x0900,    0x0AFF},    //定时器  当前值  TN:T0～T511
    {1,  type_TO,  0x5453,    0000,   511,    0x9800,    0x99FF},    //定时器  触点   TS:T0～T511
    {16, type_C,   0x434E,    0000,   199,    0x0A00,    0x0B8F},    //计数器  当前值 CN:C0～C199
    {32, type_C,   0x434E,    200,    255,    0x0C00,    0x0CDF},    //计数器  当前值 CN:C200～C255
    {1,  type_CO,  0x4353,    0000,   255,    0x6200,    0x62FF},    //计数器  触点   CS:C0～C255
    {1,  type_X,   0x5820,    0000,   255,    0x6500,    0x65FF},    //输入  X000～X377 -->10进制:X000～X255
    {1,  type_Y,   0x5920,    0000,   255,    0x5E00,    0x5EFF},    //输出  Y000～Y377 -->10进制:Y000～Y255
    {1,  type_M,   0x4D20,    0000,   1535,   0x4000,    0x5DFF},    //辅助继电器   M0～M7679
    {1,  type_M,   0x4D20,    8000,   8511,   0x6000,    0x61FF},    //辅助继电器   M8000～M8511
    {1,  type_S,   0x5320,    0000,   4095,   0x8CE0,    0x8EDF},    //状态  触点   S0～S4095
};
 
/***  强制位 E7置位/E8复位 指令通道 ***/ 
const MELSEC_FX_TABLES_T FX_E7_E8_Tables[] = {
    {1, type_T,   0x5453,     0000,   511,    0x6300,    0x64FF},    //定时器  触点   TS:T0～T511
    {1, type_C,   0x4353,     0000,   255,    0x6200,    0x62FF},    //计数器  触点   CS:C0～C255
    {1, type_X,   0x5820,     0000,   255,    0x6500,    0x65FF},    //输入  X000～X377 -->10进制:X000～X255
    {1, type_Y,   0x5920,     0000,   255,    0x5E00,    0x5EFF},    //输出  Y000～Y377 -->10进制:Y000～Y255
    {1, type_M,   0x4D20,     0000,   7679,   0x4000,    0x5DFF},    //辅助继电器   M0～M7679
    {1, type_M,   0x4D20,     8000,   8511,   0x6000,    0x61FF},    //辅助继电器   M8000～M8511
    {1, type_S,   0x5320,     0000,   4095,   0x6700,    0x76FF},    //状态  触点   S0～S4095
};
 
/**
 * @brief 在映射表中查找匹配的条目（内部辅助函数）
 *
 * 线性扫描映射表，匹配软元件代码 e_code 且 eIndex 落在 [eStart, eEnd] 区间内的条目。
 * 同一代码可能有多条记录（如 D0~D7999, D8000~D8511），需全部遍历。
 *
 * @param table   映射表数组首元素指针
 * @param count   映射表条目数量
 * @param e_code  软元件代码（如 MC_FX_D=0x4420, MC_FX_M=0x4D20）
 * @param eIndex  软元件索引编号（如 D100 → 100, X10 → 10）
 * @param point_count 连续访问的元件点数(从 eIndex 起算, 1 表示单点)
 * @param error   输出：故障信息(MC_EndingCode 枚举), 直接复用 MC 协议结束代码
 *                  MC_END_NORMAL             (0x00) - 正常：起点在范围内且 起点+点数 不超界
 *                  MC_END_ILLEGAL_DEVICE     (0x56) - 代码不匹配：全表未出现 e_code 相同的条目
 *                                                    (对方设备指定的软元件有误)
 *                  MC_END_OUT_OF_RANGE       (0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何
 *                                                    [eStart,eEnd] 内(命令起始软元件号超出范围)
 *                  MC_END_ILLEGAL_POINT_COUNT(0x57) - 点数超限：起点在范围内, 但 起点+点数-1
 *                                                    超过该条目 eEnd(起始元件号+指定点数 超最大地址)
 * @return 指向匹配条目的指针，未找到返回 NULL（error 反映具体 MC 结束代码）
 */
static inline const MELSEC_FX_TABLES_T *sim_find_matching_entry(const MELSEC_FX_TABLES_T *table,
                                                                 uint8_t count, uint16_t e_code,
                                                                 uint16_t eIndex, uint16_t point_count,
                                                                 MC_EndingCode *error)
{
    const MELSEC_FX_TABLES_T *entry = table;
    const MELSEC_FX_TABLES_T *end   = table + count;
    uint8_t code_matched = 0;   /* 扫描过程中是否出现过代码(e_code)匹配的条目 */

    for (; entry < end; entry++) {
        /* 代码不匹配则跳过 */
        if (entry->e_code != e_code) continue;
        code_matched = 1;
        /* 索引落在区间内即命中(起点合法) */
        if (eIndex >= entry->eStart && eIndex <= entry->eEnd) {
            /* 起点在范围内, 但 起点+点数-1 超过该条目最大地址 → 点数超限(0x57) */
            if (point_count > 0u &&
                (uint32_t)eIndex + point_count - 1u > (uint32_t)entry->eEnd) {
                if (error) *error = MC_END_ILLEGAL_POINT_COUNT;
                TABLES_DEBUG("点数超限: idx=%d cnt=%d eEnd=%d\r\n",
                             eIndex, point_count, entry->eEnd);
                return NULL;
            }
            if (error) *error = MC_END_NORMAL;  /* 正常 */
            TABLES_DEBUG("eStart:%d,eEnd:%d simStart:%04X,simEnd:%04X\r\n",
                            entry->eStart,entry->eEnd,entry->simStart,entry->simEnd);
            return entry;
        }
    }


    /* 未命中: 区分 代码不匹配 / 代码匹配但索引超区间, 直接填 MC 协议结束代码 */
    if (error) *error = (code_matched != 0u) ? MC_END_OUT_OF_RANGE : MC_END_ILLEGAL_DEVICE;
    return NULL;
}

/**
 * @brief 根据软元件代码和索引计算仿真地址偏移量（字/双字/位打包模式）
 *
 * 地址换算规则（按元件位宽区分）：
 *   - 1 位元件（如 M/X/Y）：8 个位打包为 1 字节 → offset = simStart + (eIndex - eStart) << 3
 *   - 16位元件（如 D/T/C 当前值）：2 字节/点 → offset = simStart + (eIndex - eStart) >> 1
 *   - 32位元件（如 C200~C255 当前值）：4 字节/点 → offset = simStart + (eIndex - eStart) >> 2
 *
 * @param table     映射表数组指针（如 FX_E0_E1_Tables）
 * @param count     映射表条目数量
 * @param e_code    软元件代码（uint16_t，如 MC_FX_D=0x4420）
 * @param eIndex    软元件索引编号（如 D100 → 100, X10 → 10）
 * @param mapIndex  输出：仿真地址偏移量
 * @return 0(MC_END_NORMAL)-成功；非0-查表失败，返回 find_err 中的 MC_EndingCode 错误码
 *         (MC_END_ILLEGAL_DEVICE / MC_END_OUT_OF_RANGE / MC_END_ILLEGAL_POINT_COUNT 等)
 */
int sim_find_tables_idx_element(const MELSEC_FX_TABLES_T *table,
                                uint8_t count, uint16_t e_code,
                                uint16_t eIndex, uint16_t *mapIndex)
{
    /* 参数有效性校验 */
    if (table == NULL || mapIndex == NULL || count == 0) return 1;

    MC_EndingCode find_err = MC_END_NORMAL;
    const MELSEC_FX_TABLES_T *entry = sim_find_matching_entry(table, count, e_code, eIndex, 1, &find_err);
    if (entry == NULL) {
        TABLES_DEBUG("E00 查表失败 err=0x%02X\r\n", find_err);
        return (int)find_err;   /* 直接返回 MC 结束代码(0x56/0x57/0x58...) */
    }

    uint16_t base = eIndex - entry->eStart;      /* 区间内相对偏移量 */

    // 根据元件位数计算映射地址
    switch (entry->eBitNum) {
        case 1:
            *mapIndex = entry->simStart + base / 8;   /* 每字节打包 8 个位 */
            break;
        case 16:
            *mapIndex = entry->simStart + base * 2;  /* 每字占 2 字节地址*/
            break;
        case 32:
            *mapIndex = entry->simStart + base * 4;   /* 每双字占 4 字节地址 */
            break;
        default:
        TABLES_DEBUG("E00 address=0x%04X(%d) 映射失败\r\n", eIndex, eIndex);
            return 1;  /* 未知位数，返回错误 */
    } 
    TABLES_DEBUG("E00 addr=%d -> map=0x%04X(%d)\r\n", eIndex, *mapIndex, *mapIndex);
    return 0;
}

/**
 * @brief 根据软元件代码和索引计算位元件仿真地址偏移量（逐字节寻址模式）
 *
 * 与 sim_find_tables_idx_element 的区别：
 *   本函数用于位强制/位读写场景，每个位软元件独占 1 字节地址空间（不打包）。
 *   换算公式：offset = simStart + (eIndex - eStart)
 *
 * @param table     映射表数组指针（如 FX_30_40_Tables）
 * @param count     映射表条目数量
 * @param e_code    软元件代码（uint16_t，如 MC_FX_X=0x5820）
 * @param eIndex    软元件索引编号
 * @param mapIndex  输出：仿真地址偏移量
 * @return 0(MC_END_NORMAL)-成功；非0-查表失败，返回 find_err 中的 MC_EndingCode 错误码
 *         (MC_END_ILLEGAL_DEVICE / MC_END_OUT_OF_RANGE / MC_END_ILLEGAL_POINT_COUNT 等)
 */
uint8_t sim_find_tables_idx_bit_element(const MELSEC_FX_TABLES_T *table,
                                    uint8_t count, uint16_t e_code,
                                    uint16_t eIndex, uint16_t *mapIndex)
{
    /* 参数有效性校验 */
    if (table == NULL || mapIndex == NULL || count == 0) return 1;

    MC_EndingCode find_err = MC_END_NORMAL;
    const MELSEC_FX_TABLES_T *entry = sim_find_matching_entry(table, count, e_code, eIndex, 1, &find_err);
    if (entry == NULL) {
        TABLES_DEBUG("E00 查表失败 err=0x%02X\r\n", find_err);
        return (uint8_t)find_err;   /* 直接返回 MC 结束代码(0x56/0x57/0x58...) */
    }

    /* 位元件逐字节寻址：每个位独占 1 字节 */
    *mapIndex = (eIndex - entry->eStart) + entry->simStart;
    return 0;
}



// ==================== 表1a：位元元件位址（GROUP ADDRESS）算法函数 ====================
/**
 * @brief 计算表1a中指定地址和列的位范围
 *
 * 地址映射规则：
 * - 0000-007F: M继电器，起始号 = row * 128 + col_idx * 8
 * - 0080-00BF: 特殊继电器M8000+，起始号 = 8000 + row * 200 + col_idx * 10
 * - 00C0-00FF: T/C状态位
 *
 * @param address PLC位地址（如 0x0000, 0x0080 等）
 * @param col_idx 列索引(0-15)，对应每列的8位
 * @return BitRange_t 结构体
 */
BitRange_t CalcTable1ABitRange(uint16_t address, uint8_t col_idx) {
    BitRange_t invalid = {0xFFFF, 0xFFFF};

    if (col_idx >= 16) {
        return invalid;
    }

    // 普通M继电器区域：0000 ~ 007F
    if (address <= 0x007F) {
        uint16_t row = (address & 0xF0) >> 4;  // 0-7
        uint16_t start = row * 128 + col_idx * 8;
        return (BitRange_t){start, start + 7};
    }
    // 特殊继电器区域：0080 ~ 00BF
    else if (address >= 0x0080) {
        uint16_t row = (address & 0xF0) >> 4;  // 8-11
        uint16_t row_offset = row - 8;  // 0-3
        uint16_t start = 8000 + row_offset * 200 + col_idx * 10;
        return (BitRange_t){start, start + 7};
    }
    // T/C状态位区域：00C0 ~ 00FF
    else if (address >= 0x00C0 && address <= 0x00FF) {
        uint16_t row = (address & 0xF0) >> 4;  // 12-16
        uint16_t start;

        switch (row) {
            case 12: start = 0 + col_idx * 8; break;        // T0-T127
            case 13: start = 128 + col_idx * 8; break;      // C0-C127
            case 14: start = 0 + col_idx * 8; break;        // T128-T199 (相对值)
            case 15: start = 128 + col_idx * 8; break;      // T200-T255 (相对值)
            case 16: start = 128 + col_idx * 8; break;      // C128-C255 (相对值)
            default: return invalid;
        }
        return (BitRange_t){start, start + 7};
    }

    return invalid;
}

/**
 * @brief 根据地址和元件号计算位范围
 * @param address PLC位地址
 * @param element_num 元件号（如 M100, T50 等）
 * @return BitRange_t 结构体
 */
BitRange_t GetBitRangeByElement(uint16_t address, uint16_t element_num) {
    BitRange_t invalid = {0xFFFF, 0xFFFF};

    // 计算该元件所在的列
    uint8_t col_idx;

    // 普通M继电器区域：0000 ~ 007F
    if (address <= 0x007F) {
        col_idx = (element_num % 128) / 8;
        return CalcTable1ABitRange(address, col_idx);
    }
    // 特殊继电器区域：0080 ~ 00BF
    else if (address >= 0x0080) {
        uint16_t row = (address & 0xF0) >> 4;
        uint16_t row_offset = row - 8;
        uint16_t base_number = 8000 + row_offset * 200;
        if (element_num < base_number || element_num >= base_number + 160) {
            return invalid;
        }
        col_idx = (element_num - base_number) / 10;
        return CalcTable1ABitRange(address, col_idx);
    }

    return invalid;
}

// ==================== 表1b：T/C 位元地址映射算法函数 ====================

/**
 * @brief 计算表1b中指定地址和列的T/C位范围
 *
 * 地址映射规则：
 * - 02A0: T0-T127，起始号 = col_idx * 8
 * - 02B0: T128-T255，起始号 = 128 + col_idx * 8
 * - 02C0: C0-C127，起始号 = col_idx * 8
 * - 02D0: C128-C255，起始号 = 128 + col_idx * 8
 *
 * @param address PLC位地址（02A0/02B0/02C0/02D0）
 * @param col_idx 列索引(0-15)
 * @return BitRange_t 结构体
 */
BitRange_t CalcTable1BBitRange(uint16_t address, uint8_t col_idx) {
    BitRange_t invalid = {0xFFFF, 0xFFFF};

    if (col_idx >= 16) {
        return invalid;
    }

    uint16_t start;

    switch (address) {
        case 0x02A0:  // T0-T127
            start = col_idx * 8;
            break;
        case 0x02B0:  // T128-T255
            start = 128 + col_idx * 8;
            break;
        case 0x02C0:  // C0-C127
            start = col_idx * 8;
            break;
        case 0x02D0:  // C128-C255
            start = 128 + col_idx * 8;
            break;
        default:
            return invalid;
    }

    return (BitRange_t){start, start + 7};
}

/**
 * @brief 根据定时器/计数器号获取位范围
 * @param is_timer 1=定时器, 0=计数器
 * @param tc_number T/C编号(0-255)
 * @return BitRange_t 结构体
 */
BitRange_t GetTCBitRange(uint8_t is_timer, uint16_t tc_number) {
    BitRange_t invalid = {0xFFFF, 0xFFFF};

    if (tc_number > 255) {
        return invalid;
    }

    uint16_t address;
    uint16_t col_idx = (tc_number % 128) / 8;

    if (is_timer) {
        if (tc_number < 128) {
            address = 0x02A0;  // T0-T127
        } else {
            address = 0x02B0;  // T128-T255
        }
    } else {  // Counter
        if (tc_number < 128) {
            address = 0x02C0;  // C0-C127
        } else {
            address = 0x02D0;  // C128-C255
        }
    }

    return CalcTable1BBitRange(address, col_idx);
}

// ==================== 表1c：BIT IMAGES GROUP ADDRESS算法函数 ====================

/**
 * @brief 计算表1c中指定地址和列的位映像范围
 *
 * 地址映射规则：
 * - 02C0: 位映像组1，起始号 = col_idx * 8
 * - 02D0: 位映像组2，起始号 = 128 + col_idx * 8
 *
 * @param address PLC位地址（02C0/02D0）
 * @param col_idx 列索引(0-15)
 * @return BitRange_t 结构体
 */
BitRange_t CalcTable1CBitRange(uint16_t address, uint8_t col_idx) {
    BitRange_t invalid = {0xFFFF, 0xFFFF};

    if (col_idx >= 16) {
        return invalid;
    }

    uint16_t start;

    switch (address) {
        case 0x02C0:  // 位映像组1
            start = col_idx * 8;
            break;
        case 0x02D0:  // 位映像组2
            start = 128 + col_idx * 8;
            break;
        default:
            return invalid;
    }

    return (BitRange_t){start, start + 7};
}

// ==================== 表2：定时器 T 当前值地址映射 ====================
// 地址范围：0800 ~ 09E0
// 数学规律：地址 = 0x0800 + t_number * 2
uint16_t GetTimerAddressOffset_T(uint16_t t_number) {
    if (t_number > 255) {
        return 0;
    }
    return 0x0800 + (t_number * 2) ;
}

// ==================== 表3：16位计数器 C 当前值地址映射 ====================
// C0 -> 0A00(lower), 0A01(upper)
// 数学规律：地址 = 0x0A00 + C_num * 2
uint16_t GetCounter16Address_C(uint16_t c_number) {
    if (c_number >= 200) {
        return 0;
    }
    return 0x0A00 + c_number * 2;
}
// ==================== 表4：32位计数器 C 当前值地址映射 ====================
// C0 -> 0A00(lower), 0A01(upper)
// 数学规律：地址 = 0x0A00 + C_num * 4
uint16_t GetCounter32Address_C(uint16_t c_number) {
    if (c_number < 200 || c_number > 255) {
        return 0;
    }
    return 0x0C00 + ((c_number-200) * 4);   //一个寄存器占用四个字节
}

// ==================== 表5a：数据寄存器 D 地址映射 ====================
// D0~D7999，地址 = D_num
uint16_t GetDataRegisterAddressOffset_D(uint16_t d_number) {
    if (d_number > 7999) {
        return 0;
    }
    return 0x1000 + (d_number*2);  //一个寄存器占用两个字节
}
// ==================== 表6：特殊寄存器 D 地址映射 ====================
// D8000~D8255: 地址 = 0x0E00 + (D_num - 8000)*2 (连续映射)
// D8256~D8511: 稀疏映射，每16个寄存器映射到10字节区域
// @param d_number 特殊寄存器编号 (8000~8511)
// @return 成功返回地址，失败返回0
uint16_t GetSpecialRegisterAddressOffset_D(uint16_t d_number) {
    const uint16_t offset = d_number - 8000;

    // 检查有效范围
    if (d_number < 8000 || d_number > 8511) {
        return 0;
    }
    // D8000~D8255: 连续映射区域
    if (offset <= 255) {
        return 0x0E00 + offset * 2;
    }
    // D8256~D8511: 稀疏映射区域 (每16个寄存器占10字节)
    // 256~511 (对应D8256~D8511)
    return 0x8000 + offset * 2;
}

// ==================== 表7a：S/X/Y/T 强制地址 ====================
// S0~S559 对应强制地址 0x0000~0x022F
// 数学规律：地址 = S_num (直接对应)
uint16_t GetForceAddress_S(uint16_t s_number) {
    if (s_number > 559) {
        return 0;
    }
    return s_number;
}

// X0~X177 对应强制地址 0x0400~0x047F
// 数学规律：地址 = 0x0400 + X_num
uint16_t GetForceAddress_X(uint16_t x_number) {
    if (x_number > 177) {
        return 0;
    }
    return 0x0400 + x_number;
}


// Y0~Y177 对应强制地址 0x0500~0x057F
// 数学规律：地址 = 0x0500 + Y_num
uint16_t GetForceAddress_Y(uint16_t y_number) {
    if (y_number > 177) {
        return 0;
    }
    return 0x0500 + y_number;
}

// T0~T255 对应强制地址 0x0600~0x06FF
// 数学规律：地址 = 0x0600 + T_num
uint16_t GetForceAddress_T(uint16_t t_number) {
    if (t_number > 255) {
        return 0;
    }
    return 0x0600 + t_number;
}


// ==================== 表7b：M 强制地址 ====================
// M0~M1023 对应强制地址 0x0800~0x0BFF
// 数学规律：地址 = 0x0800 + m_num
// M8000~M8255 对应强制地址 0x0F00~0x0FFF
// 数学规律：地址 = 0x0F00 + (m_num - 8000)
uint16_t GetForceAddress_M(uint16_t m_number) {
    if (m_number > 1023) {
        return 0x0800 + m_number ;
    }
    else if (m_number >= 8000) {
        return 0x0F00 + (m_number - 8000);
    }
    return 0;
}
// ==================== 表7b：C 强制地址 ====================
// C0~C255 对应强制地址 0x0E00~0x0EFF
// 数学规律：地址 = 0x0E00 + C_num
uint16_t GetForceAddress_C(uint16_t c_number) {
    if (c_number > 255) {
        return 0;
    }
    return 0x0E00 + c_number;
}
