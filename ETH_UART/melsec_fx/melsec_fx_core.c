#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"

#include "ethernet_app.h"
#include "bsp_uart.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>


// uartTxWithSocketID 在帧尾追加 11 字节元数据，帧缓冲区需预留空间
#define FX_CMD_META_PAD  11

static const uint8_t hex_table[256] = {
    ['0'] = 0, ['1'] = 1, ['2'] = 2, ['3'] = 3, ['4'] = 4,
    ['5'] = 5, ['6'] = 6, ['7'] = 7, ['8'] = 8, ['9'] = 9,
    ['A'] = 10, ['B'] = 11, ['C'] = 12, ['D'] = 13, ['E'] = 14, ['F'] = 15,
    ['a'] = 10, ['b'] = 11, ['c'] = 12, ['d'] = 13, ['e'] = 14, ['f'] = 15
};
/* 16进制字节→ASCII字符查找表（256×2字节，位于 .rodata 段） */
static const uint8_t hex_byte_table[512] = {
    '0','0','0','1','0','2','0','3','0','4','0','5','0','6','0','7',
    '0','8','0','9','0','A','0','B','0','C','0','D','0','E','0','F',
    '1','0','1','1','1','2','1','3','1','4','1','5','1','6','1','7',
    '1','8','1','9','1','A','1','B','1','C','1','D','1','E','1','F',
    '2','0','2','1','2','2','2','3','2','4','2','5','2','6','2','7',
    '2','8','2','9','2','A','2','B','2','C','2','D','2','E','2','F',
    '3','0','3','1','3','2','3','3','3','4','3','5','3','6','3','7',
    '3','8','3','9','3','A','3','B','3','C','3','D','3','E','3','F',
    '4','0','4','1','4','2','4','3','4','4','4','5','4','6','4','7',
    '4','8','4','9','4','A','4','B','4','C','4','D','4','E','4','F',
    '5','0','5','1','5','2','5','3','5','4','5','5','5','6','5','7',
    '5','8','5','9','5','A','5','B','5','C','5','D','5','E','5','F',
    '6','0','6','1','6','2','6','3','6','4','6','5','6','6','6','7',
    '6','8','6','9','6','A','6','B','6','C','6','D','6','E','6','F',
    '7','0','7','1','7','2','7','3','7','4','7','5','7','6','7','7',
    '7','8','7','9','7','A','7','B','7','C','7','D','7','E','7','F',
    '8','0','8','1','8','2','8','3','8','4','8','5','8','6','8','7',
    '8','8','8','9','8','A','8','B','8','C','8','D','8','E','8','F',
    '9','0','9','1','9','2','9','3','9','4','9','5','9','6','9','7',
    '9','8','9','9','9','A','9','B','9','C','9','D','9','E','9','F',
    'A','0','A','1','A','2','A','3','A','4','A','5','A','6','A','7',
    'A','8','A','9','A','A','A','B','A','C','A','D','A','E','A','F',
    'B','0','B','1','B','2','B','3','B','4','B','5','B','6','B','7',
    'B','8','B','9','B','A','B','B','B','C','B','D','B','E','B','F',
    'C','0','C','1','C','2','C','3','C','4','C','5','C','6','C','7',
    'C','8','C','9','C','A','C','B','C','C','C','D','C','E','C','F',
    'D','0','D','1','D','2','D','3','D','4','D','5','D','6','D','7',
    'D','8','D','9','D','A','D','B','D','C','D','D','D','E','D','F',
    'E','0','E','1','E','2','E','3','E','4','E','5','E','6','E','7',
    'E','8','E','9','E','A','E','B','E','C','E','D','E','E','E','F',
    'F','0','F','1','F','2','F','3','F','4','F','5','F','6','F','7',
    'F','8','F','9','F','A','F','B','F','C','F','D','F','E','F','F'
};

/**
 * @brief 将MC协议设备代码转换为元件类型枚举（O(1) 查表版本）
 * @param dev_code 双字节设备代码（高字节=元件字母，低字节=子类型）
 *                 如 0x4420="D ", 0x544E="TN", 0x4353="CS"
 * @return 对应的 element_type_enum 枚举值，未知设备返回 type_NULL
 *
 * @note 支持的设备代码：
 *   "D "=0x4420 → type_D    "R "=0x5220 → type_R
 *   "TN"=0x544E → type_T    "TS"=0x5453 → type_TO
 *   "CN"=0x434E → type_C    "CS"=0x4353 → type_CO
 *   "X "=0x5820 → type_X    "Y "=0x5920 → type_Y
 *   "M "=0x4D20 → type_M    "S "=0x5320 → type_S
 *
 * @note 优化说明：
 *   - switch-case 替代线性搜索，无需循环，无需 device_map 表
 *   - 仅 T(0x54) 和 C(0x43) 需二次判断低字节区分 TN/TS、CN/CS
 *   - 最坏情况 2 次比较（原 10 次），省去 30 字节 flash 表
 */
uint8_t MC_Net_DeviceCodeToType(uint16_t dev_code)
{
    uint8_t high = (uint8_t)(dev_code >> 8);   /* 高字节：元件字母 'D','T','C'... */
    uint8_t low  = (uint8_t)(dev_code & 0xFF); /* 低字节：子类型 ' ','N','S'...  */

    switch (high)
    {
        case 0x44: return type_D;    /* "D " 数据寄存器 */
        case 0x52: return type_R;    /* "R " 扩展寄存器 */
        case 0x58: return type_X;    /* "X " 输入继电器 */
        case 0x59: return type_Y;    /* "Y " 输出继电器 */
        case 0x4D: return type_M;    /* "M " 辅助继电器 */
        case 0x53: return type_S;    /* "S " 状态继电器 */

        case 0x54:  /* T 系列：TN(当前值"0x4E) / TS(触点"0x53) */
            if (low == 0x4E) return type_T;
            if (low == 0x53) return type_TO;
            break;

        case 0x43:  /* C 系列：CN(当前值"0x4E) / CS(触点"0x53) */
            if (low == 0x4E) return type_C;
            if (low == 0x53) return type_CO;
            break;

        default: break;
    }

    MELSEC_DEBUG("未知的设备代码 (0x%04X)\r\n", dev_code);
    return type_NULL;
}

/**
 * @brief 判断设备代码是否为字软元件（D、R、T、C）
 * @param dev_code 双字节设备代码（高字节=元件字母，低字节=子类型）
 *                 如 0x4420="D "、0x5220="R "、0x544E="TN"、0x434E="CN"
 * @return 1-是字软元件（type_D / type_R / type_T / type_C，即 D、R、T当前值、C当前值）
 *         0-非字软元件（位元件 X/Y/M/S/T触点/C触点 或 未知）
 *
 * @note 说明：
 *   - T、C 仅"当前值"(TN/CN) 为字软元件，对应 type_T / type_C；
 *     其触点版本(TS/CS) 映射为 type_TO / type_CO 属于位元件，不计入。
 *   - 内部直接复用 MC_Net_DeviceCodeToType 获取元件类型，再做字/位判定。
 */
uint8_t MC_Net_DeviceCodeIsWord(uint16_t dev_code)
{
    uint8_t type = MC_Net_DeviceCodeToType(dev_code);

    /* 字软元件集合: D(type_D) / R(type_R) / T当前值(type_T) / C当前值(type_C) */
    if (type == type_D || type == type_R || type == type_T || type == type_C) {
        return 1u;
    }
    return 0u;
}

 

/**
 * @brief 将字符转换为十六进制整型
 * @param c 输入的字符
 * @param value 输出的整型值
 * @return 1: 转换成功; 0: 转换失败（非法字符）
 */
uint8_t char_to_hex(uint8_t c, uint8_t* value) {
    if (c >= '0' && c <= '9') {
        *value = c - '0';
    } else if (c >= 'A' && c <= 'F') {
        *value = c - 'A' + 10;
    } else if (c >= 'a' && c <= 'f') {
        *value = c - 'a' + 10;
    } else {
        return 0; // 非法字符
    }
    return 1;
}

/**
 * @brief ASCII十六进制字符转换为对应的数值（查表法）
 * @param Ascii ASCII字符（'0'-'9', 'A'-'F', 'a'-'f'）
 * @return 对应的数值（0-15），非法字符返回0
 *
 * @note 实现方式：
 *   - 使用全局静态查找表hex_table实现O(1)时间复杂度
 *   - 对于非法字符，表中对应位置为0，返回值也为0
 *   - 建议在调用前进行字符有效性检查
 *
 * 示例：
 *   Ascii_Hex_table('0') -> 0
 *   Ascii_Hex_table('A') -> 10
 *   Ascii_Hex_table('f') -> 15
 *   Ascii_Hex_table('X') -> 0 (非法字符)
 *
 * @see AsciiHexToUint8() - 将2个ASCII字符转换为1字节
 */
uint8_t Ascii_Hex_table(uint8_t Ascii) {
    return hex_table[Ascii];
}
/**
 * @brief 将2个ASCII十六进制字符转换为8位数值
 * @param high 高位ASCII字符（'0'-'9', 'A'-'F', 'a'-'f'）
 * @param low 低位ASCII字符（'0'-'9', 'A'-'F', 'a'-'f'）
 * @return 转换后的8位数值
 *
 * 示例：
 *   AsciiHexToUint8('A', '3') -> 0xA3
 *   AsciiHexToUint8('2', 'F') -> 0x2F
 */
uint8_t AsciiHexToUint8(uint8_t high, uint8_t low) {

    return (hex_table[high] << 4) | hex_table[low];
}


/**
 * @brief 将十六进制字符串转换为整型
 * @param str 输入的十六进制字符串
 * @param length 字符串长度（最多16个字符）
 * @return 转换后的整型值；如果转换失败，返回0
 */
uint32_t hex_str_to_int(const uint8_t* str, uint16_t length) 
{
    // 参数检查
    if (str == NULL || length == 0) {
        return 0;
    }
    uint32_t result = 0;
 
    // 限制最大长度为16
    if (length > 16) length = 16;

    for (uint16_t i = 0; i < length; i++) 
    {
        result = (result << 8) | AsciiHexToUint8(str[i*2], str[i*2+1]);
    }

    return result;
}


/**
 * @brief 将ASCII十六进制字符串转换为二进制数据
 * @param str 输入的ASCII十六进制字符串（每2字符对应1字节）
 * @param strleng 输入字符串长度（字符数，必须是偶数）
 * @param outhex 输出的二进制数据缓冲区（长度至少为 strleng/2）
 * @return 成功返回1，失败返回0（参数无效或非法字符）
 * 
 * 示例：
 *   输入: "A3F2", strleng=4
 *   输出: outhex[0]=0xA3, outhex[1]=0xF2
 */
uint8_t hex_str_to_intlend(const uint8_t* str, uint16_t strleng, uint8_t* outvalue) {
    // 参数验证
    if (!str || !outvalue) {
        return 0;
    }
    // 长度验证
    if (strleng == 0 || (strleng % 2) != 0) {
        return 0;  // 长度必须是非零偶数
    }
    for (uint16_t i = 0; i < strleng ; i++) 
    {
        outvalue[i] = AsciiHexToUint8(str[i*2], str[i*2+1]);
    }
    return 1;
}
 
 
/**
 * @brief 将8位字节转换为2个ASCII字符（查表优化版，避免条件分支和函数调用）
 * @param byte 字节值
 * @param c 输出缓冲区（至少2字节）
 */
void byte_to_hex_chars(uint8_t byte, uint8_t* c) {
    const uint8_t *tab = &hex_byte_table[byte * 2];
    c[0] = tab[0];
    c[1] = tab[1];
}
/**
 * @brief 将16位十六进制整型值转换为4个ASCII字符
 * @param value 输入的16位整型值
 * @param c 输出的字符指针（至少4字节）
 * @return 1: 转换成功; 0: 转换失败
 * @note 输出4个ASCII字符，表示16位十六进制值（例如0x12AB → "12AB"）
 */
uint8_t Uint16ToAscii(uint16_t value, uint8_t* c) {
 
    byte_to_hex_chars((value >> 8) & 0xFF, &c[0]);
    byte_to_hex_chars(value & 0xFF, &c[2]);

    return 1;
}
/**
 * @brief 将16位十六进制整型值转换为4个ASCII字符 (高8位和低8位交换)
 * @param value 输入的16位整型值
 * @param c 输出的字符指针（至少4字节）
 * @return 1: 转换成功; 0: 转换失败
 * @note 输出4个ASCII字符，表示16位十六进制值（例如0x12AB → "12AB"）
 */
uint8_t Uint16SwapToAscii(uint16_t value, uint8_t* c) {
    if (!c) return 0;

    byte_to_hex_chars((value >> 8) & 0xFF, &c[2]);
    byte_to_hex_chars(value & 0xFF, &c[0]);

    return 1;
}
/**
 * @brief 将32位十六进制整型值转换为8个ASCII字符
 * @param value 输入的32位整型值
 * @param c 输出的字符指针（至少8字节）
 * @return 1: 转换成功; 0: 转换失败
 * @note 输出8个ASCII字符，表示32位十六进制值（例如0x1234ABCD → "1234ABCD"）
 */
uint8_t Uint32ToAscii(uint32_t value, uint8_t* c) {
    if (!c) return 0;

    for (int i = 0; i < 4; i++) {
        uint8_t byte = (value >> (24 - i * 8)) & 0xFF;
        byte_to_hex_chars(byte, &c[i * 2]);
    }

    return 1;
}

/**
 * @brief 将数据转换为ASCII格式（每字节2个字符）
 * @param out 输出缓冲区
 * @param data 输入数据
 * @param len 数据长度
 * @note 用于写命令中的数据转换
 *
 * 示例：
 *   数据: 0x12 0x34
 *   输出: '1' '2' '3' '4'
 */
void DataToAscii(const uint8_t *data, uint16_t len,uint8_t *outAscii) {
    for (uint16_t i = 0; i < len; i++) {
        byte_to_hex_chars(data [i] , &outAscii[i * 2]);
    }
}

/**
 * @brief 查找帧头或帧尾的位置
 * @param start 起始位置指针
 * @param end 结束位置指针
 * @param target 目标字符（帧头或帧尾）
 * @return 找到的位置指针；若未找到，返回 end
 */
uint8_t* find_frame_marker(uint8_t* start, uint8_t* end, uint8_t target) {
    while (start < end && *start != target) {
        start++;
    }
    return start;
}

/**
 * @brief 在数组中查找子数组（字节级比较）
 * @param main_array 主数组指针
 * @param main_length 主数组长度(字节数)
 * @param sub_array 子数组指针
 * @param sub_length 子数组长度(字节数)
 * @return 找到返回起始索引(字节偏移)，未找到返回-1
 */
int32_t find_subarray( const void* main_array,uint32_t main_length,const void* sub_array,uint32_t sub_length)
{
    if (!main_array || !sub_array || sub_length == 0 || main_length < sub_length) {
        return -1;
    }

    const uint8_t* main_bytes = (const uint8_t*)main_array;
    const uint8_t* sub_bytes = (const uint8_t*)sub_array;
    uint32_t max_pos = main_length - sub_length;

    for (uint32_t i = 0; i <= max_pos; i++) {
        if (memcmp(main_bytes + i, sub_bytes, sub_length) == 0) {
            return i;
        }
    }

    return -1;
}

/**
 * @brief 计算数据的校验和（简单累加）
 * @param data 数据指针
 * @param size 数据大小
 * @return 计算得到的校验和
 */
static uint8_t calculate_checksum(const uint8_t* data, uint16_t size) {
    uint8_t checksum = 0;
    for (uint16_t i = 0; i < size; i++) {
        checksum += data[i];
    }
    return checksum;
}
/**
 * @brief 计算并验证校验和
 * @param data_ptr 数据指针
 * @param data_length 数据长度（不包括校验和部分）
 * @return 校验通过返回1，否则返回0
 */
uint8_t verify_checksum(const uint8_t* data_ptr, uint32_t data_length) 
{
    uint8_t checksum_high, checksum_low;
    
    // 提取校验和高位和低位（校验和位于数据末尾的2个字节）
    const uint8_t* checksum_ptr = data_ptr + data_length - 2;
    if (!char_to_hex(checksum_ptr[0], &checksum_high) || 
        !char_to_hex(checksum_ptr[1], &checksum_low)) {
        MELSEC_DEBUG("\n[MELSEC_DEBUG] 非法字符: 0x%02X 0x%02X \r\n",
            checksum_ptr[0], checksum_ptr[1]);
        return 0; // 非法字符
    }

    // 计算校验和
    uint8_t expected_checksum = (checksum_high << 4) | checksum_low;
    uint8_t actual_checksum = calculate_checksum(data_ptr, data_length-2 );

    // 验证校验和
    if (expected_checksum != actual_checksum) {
        MELSEC_DEBUG("[MELSEC_DEBUG] Checksum error: 0x%02X != 0x%02X \r\n", 
               expected_checksum, actual_checksum);
        return 0;
    }
    return 1;
}


/**
 * @brief 检查数据帧的校验和并返回帧头指针
 * @param frame_lend 帧长度指针（输出参数）
 * @param data_ptr 数据指针
 * @param data_len 数据长度
 * @return 校验通过返回帧头指针，校验失败或无效输入返回NULL
 *
 * @note 帧格式: STX + 命令码 + 地址(4B) + 字节数(2B) + 数据(NB) + ETX + 校验和(2B)
 *       校验和计算: 从命令码开始，到ETX结束的所有字节的和，用2个ASCII字符表示
 */
uint8_t* check_frame_checksum(uint16_t* frame_lend, uint8_t* data_ptr,uint16_t data_len)
{
    if (data_ptr == NULL || frame_lend == NULL || data_len < 4) {
        return NULL; // 无效输入
    }
    uint8_t* header_pos = data_ptr;       // 帧头位置(STX)

    // 查找帧尾位置（从第2个字节开始搜索）
    uint8_t* tail_pos = find_frame_marker(header_pos + 1, data_ptr + data_len, FRAME_END);
    if (tail_pos >= data_ptr + data_len) {
        return NULL; // 未找到帧尾或超出缓冲区
    }

    // 计算数据长度：从命令码(跳过STX)到ETX之间的字节数
    uint16_t data_size = tail_pos - (header_pos + 1);

    // 校验和位于ETX之后的2个字节
    uint8_t* checksum_pos = tail_pos + 1;

    // 检查是否有足够的校验和数据(2字节)
    if (checksum_pos + 2 > data_ptr + data_len) {
        return NULL; // 校验和区域超出缓冲区
    }

    // 设置输出参数：STX(1) + 数据 + ETX(1) + 校验和(2)
    *frame_lend = data_size + 4;

    // 验证校验和
    // verify_checksum函数期望: data_length包含校验和的2个ASCII字符
    // 它会计算data_ptr到data_ptr+data_length-2，然后与最后2个ASCII校验字符比较
    // 所以传入: header_pos+1(命令码), 长度=data_size+1(ETX)+2(校验和ASCII)
    uint16_t verify_len = data_size + 3;  // 数据 + ETX + 校验和ASCII(2)

    if ( verify_checksum(header_pos + 1, verify_len) == 0) {
 
        MELSEC_DEBUG("[MELSEC_DEBUG] Checksum verify failed: data_len=%d\r\n checksum=0x%02X 0x%02X %c%c\r\n",
               data_size, checksum_pos[0], checksum_pos[1], checksum_pos[0], checksum_pos[1]);
 
        return NULL; // 校验失败
    }

    return header_pos; // 返回帧头指针
}

/**
 * @brief 解析元件数据（每字节拆分为高4bit和低4bit）
 * @param data 原始数据
 * @param len 数据长度
 * @param out 输出缓冲区（每个元素存储高4bit和低4bit）
 * @return int 解析状态（0-成功，非0-错误码）
 */
int MELSEC_FX_ParseElementData(const uint8_t* data, uint16_t len, MELSEC_FX_Element* out) 
{
    if (!data || !out || len == 0) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    for (uint16_t i = 0; i < len; i++) {
        out[i].high_nibble = (data[i] >> 4) & 0x0F;
        out[i].low_nibble = data[i] & 0x0F;
    }

    return MELSEC_FX_SUCCESS;
}

// ==================== 批量位读取处理函数 ====================
/**
 * @brief 构造批量位读取命令（30/40指令通道）
 *
 * 帧结构：STX + '0' + 地址ASCII(4B) + 点数ASCII(2B) + ETX + 校验和(2B) = 11字节
 * 例：读取X0，8位 → 02 30 0080 08 03 XX
 *
 * @param Sour_Sock   源Socket ID
 * @param Dest_Sock   目标Socket ID
 * @param e_code      软元件代码（如 MC_FX_X=0x5820, MC_FX_M=0x4D20）
 * @param address     元件索引编号（如 M100 → 100）
 * @param point_count 点数（1-255，协议限制：2位ASCII最大0xFF）
 * @return MELSEC_FX_SUCCESS(0)成功，其他-错误码
 */
int MELSEC_FX_BuildBitReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                              uint16_t e_code, uint16_t address, uint8_t point_count)
{
    /* 地址映射 */
    uint16_t map_addr = 0;
    uint8_t map_ret = sim_find_tables_idx_bit_element(FX_30_40_Tables, TABLE_30_40_SIZE,
                                              e_code, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("BitRead address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }

    /* 固定帧长 11 字节：STX(1) + '0'(1) + addr(4) + count(2) + ETX(1) + cksum(2)*/
    uint8_t Build_buff[ FX_CMD_META_PAD + 4 ] = {0};

    /* 固定偏移直写，无中间数组、无 memcpy
     * [0]=STX [1]='0' [2..5]=addr [6..7]=len [8]=ETX [9..10]=cksum */
    Build_buff[0] = STX;
    Build_buff[1] = MELSEC_FX_CMD_READ;  
    (void)Uint16ToAscii(map_addr, Build_buff + 2 );            // [2..5] 地址ASCII（高字节在前）
    byte_to_hex_chars(point_count, Build_buff + 6);            // [6..7] 点数ASCII
    Build_buff[8] = ETX;                                       // [8] ETX
    /* 校验和覆盖 [1]~[8]（不含STX），直写 [9..10] */
    uint8_t checksum = calculate_checksum(Build_buff + 1, 8);
    byte_to_hex_chars(checksum, Build_buff + 9);               // [9..10] 校验和ASCII

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, Build_buff, 11);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, Build_buff, 11);
#endif
    return MELSEC_FX_SUCCESS;
}

// ==================== 批量 写入处理函数 ====================

/**
 * @brief 构造批量位写入命令（核心函数）
 *
 * 命令格式：STX + '1' + 地址ASCII(4B) + 字节数ASCII(2B) + 数据ASCII + ETX + 校验和(2B)
 * 帧结构: [0]=STX [1]='1' [2..5]=addr [6..7]=len [8..7+N]=data [8+N]=ETX [9+N..10+N]=cksum
 *
 * @param Sour_Sock  源Socket ID
 * @param Dest_Sock  目标Socket ID
 * @param map_addr   仿真地址偏移量（已通过 sim_find_tables_idx_element 映射）
 * @param bit_data   位数据数组（每点1字节，0=OFF, 1=ON）
 * @param point_count 点数（1-256）
 * @return MELSEC_FX_SUCCESS(0)成功，其他-错误码
 */
int MELSEC_FX_BuildWrite_mode_Cmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                                uint16_t map_addr, const uint8_t* bit_data, uint16_t point_count)
{
    if (!bit_data || point_count == 0 || point_count > 256) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }
    const size_t required_size = 11 + (size_t)point_count; // STX+CMD+addr(4)+len(2)+data+ETX+cksum(2)
    if (required_size > MELSEC_FX_MAX_DATA_LEN) {
        return MELSEC_FX_ERR_DATA_LENGTH;
    }

    uint8_t cmd_buf[MELSEC_FX_MAX_DATA_LEN + FX_CMD_META_PAD];

    /* 固定偏移写入，无中间数组、无 memcpy */
    cmd_buf[0] = STX;
    cmd_buf[1] = MELSEC_FX_CMD_WRITE;                         // '1'
    Uint16ToAscii(map_addr, cmd_buf + 2);                     // [2..5] 地址ASCII（高字节在前）
    byte_to_hex_chars((uint8_t)point_count, cmd_buf + 6);     // [6..7] 字节数ASCII

    /* 数据区：每bit转为 '0'/'1'，位运算替代查表 */
    for (uint16_t i = 0; i < point_count; i++) {
        cmd_buf[8 + i] = '0' + (bit_data[i] & 1);
    }

    uint16_t tail = 8 + point_count;
    cmd_buf[tail] = ETX;

    /* 校验和覆盖 [1]~[tail] 共 tail 字节，直写目标位置 */
    byte_to_hex_chars(calculate_checksum(cmd_buf + 1, tail), cmd_buf + tail + 1);

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, tail + 3);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, tail + 3);
#endif
    return MELSEC_FX_SUCCESS;
}

/**
 * @brief 构造批量位写入命令（30/40指令通道）
 *
 * @param Sour_Sock  源Socket ID
 * @param Dest_Sock  目标Socket ID
 * @param e_code     软元件代码（如 MC_FX_X=0x5820, MC_FX_M=0x4D20, MC_FX_TS=0x5453）
 * @param address    元件索引编号（如 M100 → 100）
 * @param bit_data   位数据数组（每点1字节，0=OFF, 1=ON）
 * @param point_count 点数（1-256）
 * @return MELSEC_FX_SUCCESS(0)成功，其他-错误码
 */
int MELSEC_FX_BuildBitWriteCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                uint16_t e_code, uint16_t address, const uint8_t* bit_data, uint16_t point_count)
{
    if (!bit_data || point_count == 0 || point_count > 256) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    uint16_t map_addr = 0;
 
    uint8_t map_ret = sim_find_tables_idx_bit_element(FX_30_40_Tables, TABLE_30_40_SIZE,
                                              e_code, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("BitWrite address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }

    MELSEC_DEBUG("位写入 e_code=0x%04X addr=%d -> map=0x%04X(%d)\r\n",
                 e_code, address, map_addr, map_addr);

    return MELSEC_FX_BuildWrite_mode_Cmd(Sour_Sock, Dest_Sock, map_addr, bit_data, point_count);
}

/**
 * @brief 构造批量字写入命令（30/40指令通道）
 *
 * @param Sour_Sock  源Socket ID
 * @param Dest_Sock  目标Socket ID
 * @param e_code     软元件代码（如 MC_FX_D=0x4420, MC_FX_R=0x5220, MC_FX_TN=0x544E）
 * @param address    元件索引编号（如 D100 → 100）
 * @param word_data  字数据数组（每字2字节）
 * @param word_count 字数（1-256）
 * @return MELSEC_FX_SUCCESS(0)成功，其他-错误码
 */
int MELSEC_FX_BuildWordWriteCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
            uint16_t e_code, uint16_t address, const uint16_t* word_data, uint16_t word_count)
{
    if (!word_data || word_count == 0 || word_count > 256) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    uint16_t map_addr = 0;
 
    uint8_t map_ret = sim_find_tables_idx_bit_element(FX_30_40_Tables, TABLE_30_40_SIZE,
                                              e_code, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("WordWrite address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    MELSEC_DEBUG("字写入 e_code=0x%04X addr=%d -> map=0x%04X(%d)\r\n",
                 e_code, address, map_addr, map_addr);

    return MELSEC_FX_BuildWrite_mode_Cmd(Sour_Sock, Dest_Sock, map_addr,
                                          (const uint8_t*)word_data, word_count);
}

// ==================== 强制 ON/OFF 处理函数 ====================
/**
 * @brief 构造强制ON/OFF命令（E7置位/E8复位）
 *
 * 命令格式：STX + 'E' + '7'/'8' + 地址ASCII(4B, 高/低字节交换) + ETX + 校验和(2B)
 * 设置Y20:   02 45 37 31 30 35 45 03 35 41     \STX E 7 1 0 5 E \ETX 5 A
 *
 * @param Sour_Sock  源Socket ID
 * @param Dest_Sock  目标Socket ID
 * @param e_code     软元件代码（如 MC_FX_X=0x5820, MC_FX_M=0x4D20, MC_FX_TS=0x5453）
 * @param address    元件索引编号（如 M100 → 100）
 * @param force_data 0=强制OFF(E8), 非0=强制ON(E7)
 * @return MELSEC_FX_SUCCESS(0)成功，MELSEC_FX_ERR_ADDR_RANGE 地址映射失败
 */
int MELSEC_FX_Build_E7_E8_ForceCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint16_t e_code,
                                    uint16_t address, uint8_t force_data)
{
    uint16_t map_addr = 0;

    /* 在E7/E8映射表中查找仿真地址偏移量 */
    uint8_t map_ret = sim_find_tables_idx_bit_element(FX_E7_E8_Tables, TABLE_E7_E8_SIZE,
                                                        e_code, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("E7_E8 address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }

    MELSEC_DEBUG("强制ON/OFF %c%c[%d]-> mapaddr=0x%04X(%d)\r\n",
                 e_code>>8,e_code, address, map_addr, map_addr);

    /*
     * 构造命令帧（共10字节，固定偏移，无 cnt 变量）：
     *   [0]=STX, [1]='E', [2]='7'/'8', [3..6]=地址ASCII(高低字节交换),
     *   [7]=ETX, [8..9]=校验和ASCII
     */
    uint8_t cmd_buf[10];
    cmd_buf[0] = STX;
    cmd_buf[1] = MELSEC_FX_CMD_REMOTE;                       // 'E'
    cmd_buf[2] = force_data ? MELSEC_FX_CMD_FORCE_ON          // '7' 强制ON
                             : MELSEC_FX_CMD_FORCE_OFF;        // '8' 强制OFF
    Uint16SwapToAscii(map_addr, cmd_buf + 3);                 // 高/低字节交换写入[3..6]
    cmd_buf[7] = ETX;
    byte_to_hex_chars(calculate_checksum(cmd_buf + 1, 7), cmd_buf + 8);  // 直接写入[8..9]

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, 10);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, 10);
#endif
    return MELSEC_FX_SUCCESS;
}

// ==================== PLC控制命令函数 ====================

/**
 * @brief 通用远程控制命令构造函数（支持自定义功能码）
 *
 * 命令格式：
 *   STX + 'E' + 子命令码 + 功能码(4B) + ETX + 校验和(2B)
 *
 * @param buf 输出缓冲区（至少12字节）
 * @param sub_cmd 子命令码（'7'=RUN, '8'=STOP, 其他=自定义）
 * @param func_code 功能码（默认2560）
 * @return 命令帧长度
 *
 *   输出: 02 45 38 32 35 36 30 03 4C
 *   解析: STX  'E' '8' '2' '5' '6' '0' ETX  '4' 'C'
 *
 * @note 支持的自定义子命令：
 *   - '7': 远程RUN     addr = 2560
 *   - '8': 远程STOP    addr = 2560
 *   - 其他: 自定义远程控制命令（参考PLC手册）
 *
 * @see BuildRemoteRunCommand()
 * @see BuildRemoteStopCommand()
 */
int BuildRemoteControlCommand(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint8_t sub_cmd, uint16_t addr) 
{
    /* 局部缓冲区: STX(1)+CMD(1)+sub(1)+addr(4)+ETX(1)+cksum(2)+meta(11)=21 */
    uint8_t cmd_buf[21];
    uint16_t cnt = 0;
    cmd_buf[cnt++] = STX;
    cmd_buf[cnt++] = MELSEC_FX_CMD_REMOTE;
    cmd_buf[cnt++] = sub_cmd;
    Uint16ToAscii(addr, &cmd_buf[cnt]);
    cnt += 4;
    cmd_buf[cnt++] = ETX;

    MELSEC_DEBUG("E%c add=0x%04x(%d)\r\n", sub_cmd, addr);

    uint8_t sum_dat = calculate_checksum(cmd_buf + 1, cnt - 1);
    byte_to_hex_chars(sum_dat, &cmd_buf[cnt]);
    cnt += 2;

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, cnt);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, cnt);
#endif
    return MELSEC_FX_SUCCESS;
}

// ==================== E 指令处理函数 ====================

/**
 * @brief 构造 Exx 读取命令（E00/E01/E06 等通用入口）
 *
 * 帧结构：STX + 'E' + cmd2 + cmd3 + 地址(4B) + 字节数(2B) + ETX + 校验和(2B)
 * 固定 13 字节: [0]=STX [1]='E' [2]=cmd2 [3]=cmd3 [4..7]=addr [8..9]=len [10]=ETX [11..12]=cksum
 *
 * @param cmd2    命令码第2字节（如 '0'）
 * @param cmd3    命令码第3字节（如 '0','1','6'）
 * @param map_addr 仿真地址偏移量（已映射）
 * @param length   数据长度（字数，每字2字节）
 */
int MELSEC_FX_BuildExxReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                                uint8_t cmd2,uint8_t cmd3,
                                uint16_t map_addr,uint16_t length)
{
    uint8_t Build_buff[MELSEC_FX_MAX_DATA_LEN + FX_CMD_META_PAD];

    /* 固定偏移直写，无中间数组、无 memcpy */
    Build_buff[0] = STX;
    Build_buff[1] = MELSEC_FX_CMD_REMOTE;                     // 'E'
    Build_buff[2] = cmd2;
    Build_buff[3] = cmd3;
    Uint16ToAscii(map_addr, Build_buff + 4);                  // [4..7] 地址ASCII
    byte_to_hex_chars((uint8_t)(length * 2), Build_buff + 8); // [8..9] 字节数ASCII
    Build_buff[10] = ETX;

    /* 校验和覆盖 [1]~[10] 共 10 字节（不含STX），直写 [11..12] */
    byte_to_hex_chars(calculate_checksum(Build_buff + 1, 10), Build_buff + 11);

    /* 固定帧长 13 */
    #if UART_USE_FIFO
        uartTxWithSocketID(Sour_Sock, Dest_Sock, Build_buff, 13);
    #else
        uartSendPacketLen(Sour_Sock, Dest_Sock, Build_buff, 13);
    #endif
    return MELSEC_FX_SUCCESS;
}   

/**
 * @brief E0x 读命令公共构造（E00/E01/E06 共用）
 *
 * E00: cmd3='0' 读取PLC系统参数;  E01: cmd3='1' 读取PLC系统参数
 * E06: cmd3='6' 读取R寄存器（无表映射，地址=address*2）
 *
 * @param cmd3     命令码第3字节（'0','1','6'）
 * @param map_addr 仿真地址（E06传入 address*2，其余传入查表结果）
 */
int BuildE0xReadCommon(uint8_t Sour_Sock, uint8_t Dest_Sock,
                    uint16_t map_addr, uint16_t length,uint8_t cmd3 )
{
    MELSEC_FX_BuildExxReadCmd(Sour_Sock, Dest_Sock, '0', cmd3, map_addr, length);
    return MELSEC_FX_SUCCESS;
}

int MELSEC_FX_BuildE00ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                              uint16_t address, uint16_t length )
{
    uint16_t map_addr = 0;
    int map_ret = sim_find_tables_idx_element(FX_E0_E1_Tables, TABLE_E0_E1_SIZE,
                                              net_mc_meta.device_name, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("E00 address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }

    return BuildE0xReadCommon(Sour_Sock, Dest_Sock, map_addr, length, '0' );
}

int MELSEC_FX_BuildE01ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                              uint16_t address, uint16_t length)
{
    uint16_t map_addr = 0;
    int map_ret = sim_find_tables_idx_element(FX_E0_E1_Tables, TABLE_E0_E1_SIZE,
                                              net_mc_meta.device_name, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("E01 address=0x%04X(%d) 映射失败\r\n", address, address);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
     //  以字单位指令写入位软元件(Y、M、S、T、C)
    if(( address % 16 != 0  ) &&  MC_Net_DeviceCodeIsWord( net_mc_meta.device_name ) == 0   )
    {
        MELSEC_DEBUG("以字单位指令写入位软元件 address=0x%04X(%d) 需要==16的倍数  \r\n", address, address);
        eth_socket[Sour_Sock].Error_Code = 2554;   // 软元件的指定有误(向位软元件的字单位访问时，起始软元件编号不是16的倍数)
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)MC_END_OUT_OF_RANGE);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    MELSEC_DEBUG("E01 addr=%d -> map=0x%04X(%d)\r\n", address, map_addr, map_addr);

    return BuildE0xReadCommon(Sour_Sock, Dest_Sock, map_addr, length, '1');
}

int MELSEC_FX_BuildE06ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                              uint16_t address, uint16_t length)
{
    MELSEC_DEBUG("读R寄存器 E06 read\r\n");
    //R0～R32767
    if(net_mc_meta.start_device + net_mc_meta.device_count > 32768 ){
        MELSEC_DEBUG("R0～R32767 软元件点数超出范围,  \n" ); // 32个字 (512点)
        eth_socket[Sour_Sock].Error_Code = 2556;   //读出/写入点数在容许范围外
        //报错 处理  起始元件号+指定点数 超过最大地址(软元件号)      57H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                    net_mc_meta.sub_header,
                    MC_END_ILLEGAL_POINT_COUNT ); 
        return MELSEC_FX_ERR_DEVICE_TYPE;
    }else if( net_mc_meta.start_device > 32767 ){
        eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        //报错 处理  命令起始软元件号超出可指定范围    58H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,
                            net_mc_meta.sub_header,
                            MC_END_OUT_OF_RANGE ); 
        return MELSEC_FX_ERR_ADDR_RANGE; 
    }
    net_mc_meta.start_device = address;
    net_mc_meta.device_count = length;
    /* R寄存器无表映射，仿真地址 = address * 2 */
    return BuildE0xReadCommon(Sour_Sock, Dest_Sock, address * 2, length, '6');
}

/**
 * @brief E1x 写入通用构造（E10/E16 共用）
 *
 * 帧结构：STX + 'E' + cmd2 + cmd3 + 地址(4B) + 字节数(2B) + 数据(N*4B) + ETX + 校验和(2B)
 * 固定帧头 10 字节 + 数据区(word_count*4) + 尾 3 字节
 */
static int BuildE1xWriteCommon(uint8_t Sour_Sock, uint8_t Dest_Sock,
    uint8_t cmd2, uint8_t cmd3,
    uint16_t map_addr, const uint16_t *data, uint16_t word_count, uint16_t max_count)
{
    if (!data || word_count == 0 || word_count > max_count) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    const size_t data_bytes = (size_t)word_count * 4;
    const size_t total_size = 10 + data_bytes + 5;
    if (total_size > MELSEC_FX_MAX_DATA_LEN) {
        return MELSEC_FX_ERR_DATA_LENGTH;
    }

    /* 固定编译期大小: 上方守卫已保证 total_size <= MELSEC_FX_MAX_DATA_LEN,
     * 故该缓冲恒够用, 避免依赖输入的运行时 VLA */
    uint8_t cmd_buf[MELSEC_FX_MAX_DATA_LEN + FX_CMD_META_PAD];

    /* 固定偏移直写帧头: STX(1) 'E'(1) cmd2(1) cmd3(1) addr(4) len(2) = 10B */
    cmd_buf[0] = STX;
    cmd_buf[1] = MELSEC_FX_CMD_REMOTE;                     // 'E'
    cmd_buf[2] = cmd2;
    cmd_buf[3] = cmd3;
    Uint16ToAscii(map_addr, cmd_buf + 4);                  // [4..7] 地址 ASCII
    byte_to_hex_chars((uint8_t)(word_count * 2), cmd_buf + 8); // [8..9] 字节数 ASCII

    /* 数据区: 每字 4 字节 */
    for (uint16_t i = 0; i < word_count; i++) {
        Uint16ToAscii(data[i], cmd_buf + 10 + i * 4);
    }

    const uint16_t etx_pos = 10 + (uint16_t)data_bytes;
    cmd_buf[etx_pos] = ETX;

    /* 校验和覆盖 [1]~[etx_pos]，直写 [etx_pos+1..+2] */
    byte_to_hex_chars(calculate_checksum(cmd_buf + 1, etx_pos),
                      cmd_buf + etx_pos + 1);

    const uint16_t frame_len = etx_pos + 1 + 2;


#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#endif

    return MELSEC_FX_SUCCESS;
}

/**
 * @brief 构造 E10 写入PLC参数命令（需查表映射）
 */
int MELSEC_FX_BuildE10WriteParamCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                        uint16_t address, const uint16_t *data, uint16_t word_count)
{
    uint16_t map_addr = 0;
    int map_ret = sim_find_tables_idx_element(FX_E0_E1_Tables, TABLE_E0_E1_SIZE,
                                              net_mc_meta.device_name, address, &map_addr);
    if (map_ret != 0) {
        MELSEC_DEBUG("E10 address=0x%04X(%d) 映射失败error=0x%02X\r\n", address, address,map_ret);
        if( map_ret == MC_END_ILLEGAL_DEVICE )//(0x56) - 代码不匹配：全表未出现 e_code 相同的条目 (对方设备指定的软元件有误)
        {
            eth_socket[Sour_Sock].Error_Code = 2551;   //软元件的指定有误(软元件种类为预想外)
        }
        else if( map_ret == MC_END_OUT_OF_RANGE)//(0x58) - 起点超区间：代码匹配, 但 eIndex 不在任何   [eStart,eEnd] 内(命令起始软元件号超出范围)
        {
            eth_socket[Sour_Sock].Error_Code = 2552;   //软元件的指定有误(向位软元件以外读出/写入位单位)
        }
        else if( map_ret ==  MC_END_ILLEGAL_POINT_COUNT) //(0x57) - 点数超限：起点在范围内, 但 起点+点数-1 超过该条目 eEnd(起始元件号+指定点数 超最大地址)
        {
            eth_socket[Sour_Sock].Error_Code = 2557;   //超过最大地址的读出/写入请求
        }
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)map_ret);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    //  以字单位指令写入位软元件(Y、M、S、T、C)
    if(( address % 16 != 0  ) &&  MC_Net_DeviceCodeIsWord( net_mc_meta.device_name ) == 0   )
    {
        MELSEC_DEBUG("以字单位指令写入位软元件 address=0x%04X(%d) 需要==16的倍数  \r\n", address, address);
        eth_socket[Sour_Sock].Error_Code = 2554;   //软元件的指定有误(向位软元件的字单位访问时，起始软元件编号不是16的倍数)
        /* 报警: 将查表返回的 MC 结束代码(0x56/0x58...)回送给对方设备 */
        ethernet_error_code_ack(Sour_Sock, Dest_Sock, net_mc_meta.sub_header, (uint8_t)MC_END_OUT_OF_RANGE);
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    return BuildE1xWriteCommon(Sour_Sock, Dest_Sock, '1', '0', map_addr, data, word_count, 128);
}

/**
 * @brief 构造 E16 写入R寄存器命令（无表映射，地址直传）
 */
int MELSEC_FX_Build_E16_write_R_Cmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                    uint16_t address, const uint16_t *data, uint16_t word_count)
{
    //R0～R32767
    if(net_mc_meta.start_device+net_mc_meta.device_count > 32768 ){
        eth_socket[Sour_Sock].Error_Code = 2557;   
        MELSEC_DEBUG("R0～R32767 软元件点数超出范围,  \n" ); // 32个字 (512点)
        //报错 处理  起始元件号+指定点数 超过最大地址(软元件号)      57H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,net_mc_meta.sub_header,MC_END_ILLEGAL_POINT_COUNT ); 
        return MELSEC_FX_ERR_DEVICE_TYPE;
    }else if( net_mc_meta.start_device > 32768 ){
         eth_socket[Sour_Sock].Error_Code = 2556;  
        //报错 处理  命令起始软元件号超出可指定范围    58H  
        ethernet_error_code_ack(Sour_Sock,Dest_Sock,net_mc_meta.sub_header,MC_END_OUT_OF_RANGE ); 
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    /* R寄存器无表映射，仿真地址 = address * 2 */
    return BuildE1xWriteCommon(Sour_Sock, Dest_Sock, '1', '6', address*2, data, word_count, 128);
}

// ==================== EE 读写内部寄存器处理函数 ====================

/**
 * @brief 构造 EE 读取内部存储器命令
 *
 * 命令格式：STX + 'E' + 'E' + 指令(2B) + 地址(7B) + 长度(4B) + ETX + 校验和(2B)
 *
 * 示例：
 *   读取地址0x40000，长度254字节（无数据位）
 *   \STX EE 00 0040000 00FE \ETX 2C
 *
 * PLC回应数据：
 *   \STX 0004 0004 0004 0004 \ETX 2C
 *   每个数据4字节ASCII，表示一个16位数据
 *
 * @param address PLC内部存储器地址（7位十六进制）
 * @param length 数据长度（4字节ASCII，表示要读取的数据字数）
 * @param socket_ID Socket ID编号
 * @return 0成功，非0-错误码
 */
int MELSEC_FX_BuildEEReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock, uint32_t address, uint16_t length) {
    if (length == 0 || length > 1024) {
        return MELSEC_FX_ERR_ADDR_RANGE;
    }
    /*
     * 帧结构(19B): STX(1) + "EE0"(3) + addr_ascii(8) + len_ascii(4) + ETX(1) + chk(2)
     * 所有偏移均为编译期常量，无需运行时 cnt 追踪
     */
    uint8_t buf[19 + FX_CMD_META_PAD];

    buf[0] = STX;
    buf[1] = 'E';
    buf[2] = 'E';
    buf[3] = '0';
    Uint32ToAscii(address, buf + 4);                            /* [4..11] 地址 8 位 ASCII */
    Uint16ToAscii(length,  buf + 12);                           /* [12..15] 长度 4 位 ASCII */
    buf[16] = ETX;
    byte_to_hex_chars(calculate_checksum(buf + 1, 16), buf + 17); /* [17..18] 校验和 */

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, buf, 19);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, buf, 19);
#endif
    return MELSEC_FX_SUCCESS;
}

/**
 * @brief 构造 EE 写入内部存储器命令
 *
 * 命令格式：STX + 'E' + 'E' + 指令(2B) + 地址(7B) + 长度(4B) + 数据(N*4B) + ETX + 校验和(2B)
 *
 * 示例：
 *   写入地址0x40000，数据长度4字（4个16位数据）
 *   \STX EE 00 0040000 0004 0004 0004 0004 0004 \ETX 2C
 *
 * PLC应答：
 *   \ACK 0x06 - 正确应答
 *   \NAK 0x15 - 错误应答
 *
 * @param address PLC内部存储器地址（7位十六进制）
 * @param data 要写入的16位数据数组
 * @param word_count 数据字数（每字2字节，用4个ASCII字符表示）
 * @param socket_ID Socket ID编号
 * @return 0成功，非0-错误码
 */
int MELSEC_FX_BuildEEWriteCmd(uint8_t Sour_Sock, uint8_t Dest_Sock, uint32_t address, const uint16_t *data, uint16_t word_count)
{
    if (!data || word_count == 0 || word_count > 512) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    const size_t data_bytes = (size_t) word_count * 2;
    const uint16_t total_size = 16 + (uint16_t)data_bytes + 1 + 2;   /* 帧头16 + 数据 + ETX + chk */
    if (total_size > MELSEC_FX_MAX_DATA_LEN) {
        return MELSEC_FX_ERR_DATA_LENGTH;
    }

    /* 固定编译期大小: 上方守卫已保证 total_size <= MELSEC_FX_MAX_DATA_LEN,
     * 故该缓冲恒够用, 避免依赖输入的运行时 VLA */
    uint8_t cmd_buf[MELSEC_FX_MAX_DATA_LEN + FX_CMD_META_PAD];

    MELSEC_DEBUG("EEWriteCmd address=0x%08X(%u) word_count=%d\r\n", address, address, word_count);

    /*
     * 帧头固定偏移直写 (16B):
     *   [0] STX
     *   [1..2] "EE"
     *   [3] '0' → 与 [4]=0x31 组成指令 "01"
     *   [4..11] 地址 8 位 ASCII（[4] 被 0x31 覆盖为 '1'）
     *   [12..15] 字数 4 位 ASCII
     */
    cmd_buf[0] = STX;
    cmd_buf[1] = MELSEC_FX_CMD_REMOTE;                  /* 'E' */
    cmd_buf[2] = 'E';
    cmd_buf[3] = '0';
    Uint32ToAscii(address, cmd_buf + 4);                /* [4..11] */
    cmd_buf[4] = 0x31;                                  /* 覆盖 → 指令 "01" */
    Uint16ToAscii(word_count, cmd_buf + 12);            /* [12..15] */

    /* 数据区: 每字 4 字节，起始偏移 16 */
    for (uint16_t i = 0; i < word_count; i++) {
        Uint16ToAscii(data[i], cmd_buf + 16 + i * 4);
    }

    const uint16_t etx_pos = 16 + (uint16_t)data_bytes;

    cmd_buf[etx_pos] = ETX;
    byte_to_hex_chars(calculate_checksum(cmd_buf + 1, etx_pos),
                      cmd_buf + etx_pos + 1);

    const uint16_t frame_len = etx_pos + 1 + 2;

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#endif
    return MELSEC_FX_SUCCESS;
}

// ==================== F5 读写内部寄存器处理函数 ====================
/**
 * @brief 构造 F5 读取内部存储器命令
 *
 * 命令格式：STX + 'F' + '5' + 读取存储器数量(2B ASCII) + [ 读取类型( 1B/2B ASCII ) +  存储器地址(4B ASCII ) ] * 读取数量 + ETX + 校验和(2B ASCII)
 *
 * addr_data[i] 编码: 高16位=simType(读取类型), 低16位=存储器地址
 *   simType==0x00 (位元件): 读取类型1字节ASCII + 地址4字节ASCII = 5字节/元件
 *   simType!=0x00 (其他):   读取类型2字节ASCII + 地址4字节ASCII = 6字节/元件
 *
 * 示例：
 *   读取1个D元件:  addr_data[0]=0x1004261  -> STX+"F5"+"01"+"104261"+ETX+checksum  (类型0x10,地址0x0426)
 *   读取1个位元件:  addr_data[0]=0x000060   -> STX+"F5"+"01"+"0060"+ETX+checksum    (类型0x00,地址0x0060)
 *   读取9个元件:  addr_data混合位/字元件
 *
 * PLC回应数据：
 *   STX + 长度(2B) + 数据(N*4B) + ETX + 校验和(2B)
 *   每个数据4字节ASCII，表示一个16位数据
 *
 * @param Sour_Sock 源Socket ID
 * @param Dest_Sock 目标Socket ID
 * @param addr_data 元件地址数组, 每个uint32_t: 高16位=simType, 低16位=地址
 * @param count     读取元件数量
 * @return 0成功，非0-错误码
 */
int MELSEC_FX_BuildF5ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                              const uint32_t *addr_data, uint16_t count)
{
    if (!addr_data || count == 0 || count > 99) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    /* 缓冲区: 最大帧 = 1(STX)+2("F5")+2(count)+99*6(元件)+1(ETX)+2(checksum) = 602B */
    uint8_t Build_buff[602];

    MELSEC_DEBUG("F5ReadCmd count=%d\r\n", count);

    /* 帧头固定偏移: STX(1) + "F5"(2) + count_ascii(2) = 5B */
    Build_buff[0] = STX;
    Build_buff[1] = 'F';
    Build_buff[2] = '5';
    byte_to_hex_chars((uint8_t)count, Build_buff + 3);      /* [3..4] */

    net_mc_meta.device_count = count;

    /*
     * 元件区: 每个位元件 5B (type 1B+addr 4B)，字元件 6B (type 2B+addr 4B)
     * 使用偏移追踪，但每个元件内部为固定偏移
     */
    uint16_t offset = 5;

    for (int i = 0; i < count; i++) {
        uint16_t simType = (uint16_t)(addr_data[i] >> 16);
        uint16_t address = (uint16_t)(addr_data[i] & 0xFFFF);
        if (i == 0) net_mc_meta.start_device = address;

        if (simType == 0x00) {
            Build_buff[offset++] = '0';                     /* 位元件: 单个 '0' */
        } else {
            byte_to_hex_chars((uint8_t)simType, Build_buff + offset);
            offset += 2;                                    /* 字元件: 2 字节 */
        }
        Uint16ToAscii(address, Build_buff + offset);        /* 地址 4B */
        offset += 4;
    }

    Build_buff[offset] = ETX;
    byte_to_hex_chars(calculate_checksum(Build_buff + 1, offset),
                      Build_buff + offset + 1);

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, Build_buff, offset + 3);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, Build_buff, offset + 3);
#endif
    return MELSEC_FX_SUCCESS;
}

/**
 * @brief 构造F0 读取外扩模块命令
 *
 * 命令格式：STX + "F0" + cmd(1B ASCII) + 地址(4B ASCII) + 长度(2B ASCII) + ETX + 校验和(2B ASCII)
 *
 * 示例： ?F003F7C04?FE
 *   STX + "F0" + '0' + "3F7C" + "04" + ETX + checksum
 *   说明: cmd='0', 读取地址0x3F7C, 长度0x04字节
 *
 * PLC回应数据： ?581B0000?A3
 *   STX + 数据(长度*2B ASCII, 每字节2字符) + ETX + 校验和(2B ASCII)
 *
 * @param Sour_Sock 源Socket ID
 * @param Dest_Sock 目标Socket ID
 * @param cmd       子命令(1字节ASCII, 如'0')
 * @param address   读取地址(16位, 转为4字节ASCII)
 * @param length    读取字节长度(8位, 转为2字节ASCII)
 * @return 0成功，非0-错误码
 */
int MELSEC_FX_BuildF0ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                                uint8_t cmd, uint16_t address, uint8_t length)
{
    if (length == 0) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    MELSEC_DEBUG("F0ReadCmd cmd=%c addr=0x%04X len=%d\r\n", cmd, address, length);

    /*
     * 固定帧 13B: STX(1)+"F0"(2)+cmd(1)+addr(4)+len(2)+ETX(1)+chk(2)
     * 所有偏移编译期常量，无需运行时 cnt 追踪
     */
    uint8_t Build_buff[13 + FX_CMD_META_PAD];

    Build_buff[0] = STX;
    Build_buff[1] = 'F';
    Build_buff[2] = '0';
    Build_buff[3] = cmd;
    Uint16ToAscii(address,  Build_buff + 4);                /* [4..7] 地址 */
    byte_to_hex_chars(length, Build_buff + 8);              /* [8..9] 长度 */
    Build_buff[10] = ETX;
    byte_to_hex_chars(calculate_checksum(Build_buff + 1, 10),
                      Build_buff + 11);                     /* [11..12] 校验和 */

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, Build_buff, 13);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, Build_buff, 13);
#endif
    return MELSEC_FX_SUCCESS;
}

/**
 * @brief 构造 F1 写入外扩模块命令
 *
 * 命令格式：STX + "F1" + cmd(1B ASCII) + 地址(4B ASCII) + 长度(2B ASCII)
 *          + 数据(2B*length ASCII) + ETX + 校验和(2B ASCII)
 *
 * 示例： ?F110002021100?90
 *   STX + "F1" + '0' + "0002" + "02" + "1100" + ETX + checksum
 *   说明: 写入cmd='0', 地址0x0002, 长度0x02字节, 数据0x11,0x00
 *
 * PLC应答格式：
 *   ACK 0x06 - 正确应答
 *   NAK 0x15 - 错误应答
 *
 * @param Sour_Sock 源Socket ID
 * @param Dest_Sock 目标Socket ID
 * @param cmd       子命令(1字节ASCII, 如'0')
 * @param address   写入地址(16位, 转为4字节ASCII)
 * @param length    写入字节长度(8位, 转为2字节ASCII)
 * @param data      要写入的8位数据数组(length个字节)
 * @return 0成功，非0-错误码
 */
int MELSEC_FX_BuildF1WriteCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,
                                uint8_t cmd, uint16_t address, uint8_t length,
                                const uint8_t *data)
{
    if (!data || length == 0) {
        return MELSEC_FX_ERR_INVALID_PARAM;
    }

    const uint16_t data_bytes = (uint16_t)length * 2;
    const uint16_t total_size = 10 + data_bytes + 1 + 2;    /* 帧头10 + 数据 + ETX + chk */
    if (total_size > MELSEC_FX_MAX_DATA_LEN) {
        return MELSEC_FX_ERR_DATA_LENGTH;
    }

    /* 固定编译期大小: 上方守卫已保证 total_size <= MELSEC_FX_MAX_DATA_LEN,
     * 故该缓冲恒够用, 避免依赖输入的运行时 VLA */
    uint8_t cmd_buf[MELSEC_FX_MAX_DATA_LEN + FX_CMD_META_PAD];

    MELSEC_DEBUG("F1WriteCmd cmd=%c addr=0x%04X len=%d\r\n", cmd, address, length);

    /*
     * 帧头固定偏移直写 (10B):
     *   [0] STX
     *   [1..2] "F1"
     *   [3] cmd
     *   [4..7] address (4B ASCII)
     *   [8..9] length (2B ASCII)
     */
    cmd_buf[0] = STX;
    cmd_buf[1] = 'F';
    cmd_buf[2] = '1';
    cmd_buf[3] = cmd;
    Uint16ToAscii(address,  cmd_buf + 4);                   /* [4..7] */
    byte_to_hex_chars(length, cmd_buf + 8);                 /* [8..9] */

    /* 数据区: 每字节 → 2B ASCII，起始偏移 10 */
    for (uint16_t i = 0; i < length; i++) {
        byte_to_hex_chars(data[i], cmd_buf + 10 + i * 2);
    }

    const uint16_t etx_pos = 10 + data_bytes;
    cmd_buf[etx_pos] = ETX;
    byte_to_hex_chars(calculate_checksum(cmd_buf + 1, etx_pos),
                      cmd_buf + etx_pos + 1);

    const uint16_t frame_len = etx_pos + 1 + 2;

#if UART_USE_FIFO
    uartTxWithSocketID(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#else
    uartSendPacketLen(Sour_Sock, Dest_Sock, cmd_buf, frame_len);
#endif
    return MELSEC_FX_SUCCESS;
}








