#ifndef MELSEC_FX_CORE_H
#define MELSEC_FX_CORE_H

#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"
#include "debug.h"

#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_FX_TX_Subtitle       0x80       // 副标?? 发??

/* USER CODE END Private defines */
#ifdef _MELSEC_FX_DEBUG
    #define MELSEC_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define MELSEC_DEBUG(format, ...)
#endif

// ==================== 常量定义 ====================

// 协议控制字符
#define STX         0x02        // 起始字符
#define ETX         0x03        // 结束字符
#define ACK         0x06        // 正确应答
#define NAK         0x15        // 错误应答

// 命令代码
#define MELSEC_FX_CMD_READ            '0'     // 读命令
#define MELSEC_FX_CMD_WRITE           '1'     // 写命令
#define MELSEC_FX_CMD_FORCE_ON        '7'     // 强制ON命令
#define MELSEC_FX_CMD_FORCE_OFF       '8'     // 强制OFF命令
#define MELSEC_FX_CMD_REMOTE          'E'     // 远程命令（E7）
#define MELSEC_FX_CMD_REMOTE_RUN      '7'     // 远程RUN命令（E7）
#define MELSEC_FX_CMD_REMOTE_STOP     '8'     // 远程STOP命令（E8）

#define MELSEC_FX_CMD_PLC_FX          'F'     // F通道 （F）
#define MELSEC_FX_CMD_PLC_F0          '0'     // F0通道 （F0）
#define MELSEC_FX_CMD_PLC_F1          '1'     // F1通道 （F1）
#define MELSEC_FX_CMD_PLC_F5          '5'     // F5通道 （F5）

// 设备代码（参考三菱FX协议手册）
#define DEV_X  'X'   // 输入继电器
#define DEV_Y  'Y'   // 输出继电器
#define DEV_M  'M'   // 辅助继电器
#define DEV_S  'S'   // 状态继电器
#define DEV_T  'T'   // 定时器
#define DEV_C  'C'   // 计数器
#define DEV_D  'D'   // 数据寄存器
#define DEV_F  'F'   // 变址寄存器（V/Z）


#define MELSEC_FX_MAX_DATA_LEN      512     // 最大数据长度（缓冲区大小）

/* ==================== MC协议结束代码（对方设备返回）====================
 * 参考三菱MC协议手册10.3.3项: A互连E结构通信中附加于响应的异常代码。
 * 正常结束与各类异常原因及建议解决方法见下方枚举注释。
 * ====================================================================== */
typedef enum {
    /* ---- 正常结束 ---- */
    MC_END_NORMAL               = 0x00,   /* 00H: 正常结束 */

    /* ---- 命令/参数格式错误 (50H ~ 58H) ---- */
    MC_END_ILLEGAL_SUBTITLE     = 0x50,   /* 50H: 副标题的命令/响应种类不是规定范围
                                           *       (00~05H, 13~16H)的代码
                                           * 解决: 确认/修改对方设备中设置的命令/响应种类,
                                           *       确认并修改数据长度 */
    MC_END_CANNOT_CONV_BIN      = 0x54,   /* 54H: 操作设置的参数[通信数据代码设置]中选择了ASCII码通信,
                                           *       但对方发回无法转为二进制码的ASCII数据
                                           * 解决: 确认并修改对方设备的发送数据 */
    MC_END_ILLEGAL_DEVICE       = 0x56,   /* 56H: 对方设备指定的软元件有误
                                           * 解决: 修改软元件的指定 */
    MC_END_ILLEGAL_POINT_COUNT  = 0x57,   /* 57H: 以下任一情况:
                                           *       ① 指定点数超过该处理的最大处理点数(单次通信上限)
                                           *       ② 起始元件号+指定点数 超过最大地址(软元件号)
                                           *       ③ C200~C255成批读/写时, 点数指定为奇数
                                           *       ④ 命令字节长度不是规定长度
                                           *       ⑤ 写入时, 数据实际写入点数与指定值不一致
                                           * 解决: 确认命令的数据长度, 并重新设置数据;
                                           *       修改指定点数或软元件号 */
    MC_END_OUT_OF_RANGE         = 0x58,   /* 58H: 以下任一情况:
                                           *       ① 命令起始软元件号超出可指定范围
                                           *       ② 位软元件用命令中指定了字软元件
                                           *       ③ 字软元件用命令中, 位软元件起始字号非16的倍数
                                           * 解决: 改为各处理中可指定的范围内的数值;
                                           *       修改命令或指定软元件 */

    /* ---- 通信/超时错误 (5BH, 60H) ---- */
    MC_END_UNABLE_TO_COMM       = 0x5B,   /* 5BH: 可编程控制器和以太网适配器无法通信,
                                           *       或对于对方设备的要求PLC无法给予处理
                                           * 解决: 根据结束代码后附带的异常代码(参考10.3.3项),
                                           *       修复异常部位 */
    MC_END_TIMEOUT              = 0x60,   /* 60H: 以太网适配器和可编程控制器的通信时间
                                           *       超过监视定时器值
                                           * 解决: 延长监视定时器值 */
} MC_EndingCode;

typedef enum {
    MELSEC_FX_SUCCESS = 0,          // 成功  
    MELSEC_FX_ERR_INVALID_PARAM,    // 无效参数
    MELSEC_FX_ERR_INVALID_FRAME,    // 无效帧
    MELSEC_FX_ERR_CHECKSUM,         // 校验和错误
    MELSEC_FX_ERR_DATA_LENGTH,      // 数据长度错误
    MELSEC_FX_ERR_PLC_STATUS,       // PLC状态错误
    MELSEC_FX_ERR_UNKNOWN_CMD,      // 未知命令
    MELSEC_FX_ERR_TIMEOUT,          // 超时
    MELSEC_FX_ERR_DEVICE_TYPE,      // 设备类型错误
    MELSEC_FX_ERR_ADDR_RANGE        // 地址范围错误
} MELSEC_FX_ErrorCode;



// 元件数据结构（每字节拆分为高4bit和低4bit）
typedef struct {
    uint8_t high_nibble;  // 高4位（0-15）
    uint8_t low_nibble;   // 低4位（0-15）
} MELSEC_FX_Element;

// ==================== 全局变量声明 ====================
// (MELSEC_FX_DataBuffer 已改为各模块内部 static 缓冲区)



// ==================== 函数声明 ====================
uint8_t Ascii_Hex_table(uint8_t Ascii);
uint8_t AsciiHexToUint8(uint8_t high, uint8_t low);
uint8_t char_to_hex(uint8_t c, uint8_t* value);
uint32_t hex_str_to_int(const uint8_t* str, uint16_t length) ;
uint8_t hex_str_to_intlend(const uint8_t* str, uint16_t strleng, uint8_t* outvalue);

void byte_to_hex_chars(uint8_t byte, uint8_t* c);
uint8_t Uint16ToAscii(uint16_t value, uint8_t *c);
uint8_t Uint16SwapToAscii(uint16_t value, uint8_t *c);
uint8_t Uint32ToAscii(uint32_t value, uint8_t *c);
void DataToAscii(const uint8_t *data, uint16_t len,uint8_t *outAscii) ;

uint8_t* find_frame_marker(uint8_t* start, uint8_t* end, uint8_t target) ;
int32_t find_subarray( const void* main_array,uint32_t main_length,const void* sub_array,uint32_t sub_length);
uint8_t verify_checksum(const uint8_t* data_ptr, uint32_t data_length) ;

uint8_t* check_frame_checksum(uint16_t* frame_lend, uint8_t* data_ptr,uint16_t data_len);

 
uint8_t int_to_hex(uint8_t value, uint8_t *c);

int MELSEC_FX_BuildBitReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                              uint16_t e_code, uint16_t address, uint8_t point_count);
                              
int MELSEC_FX_ParseBitReadResp(uint8_t *frame, uint16_t frame_len);


// ==================== 批量 字写入处理函数 ====================
int MELSEC_FX_BuildWrite_mode_Cmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint16_t map_addr, const uint8_t *bit_data, uint16_t point_count);
int MELSEC_FX_BuildBitWriteCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint16_t e_code, uint16_t address, const uint8_t* bit_data, uint16_t point_count);
int MELSEC_FX_BuildWordWriteCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint16_t e_code, uint16_t address, const uint16_t* word_data, uint16_t word_count);
int MELSEC_FX_ParseBitWriteAck(const uint8_t *frame, uint16_t frame_len);

// ==================== E指令处理函数 ====================

int MELSEC_FX_BuildExxReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                                uint8_t cmd2,uint8_t cmd3,
                                uint16_t address, uint16_t length ) ;
// E06 - 读R寄存器
int MELSEC_FX_BuildE06ReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint16_t address, uint16_t length);
int BuildE0xReadCommon(uint8_t Sour_Sock, uint8_t Dest_Sock,
                       uint16_t map_addr, uint16_t length, uint8_t cmd3);
// E00 - 读取元件
int MELSEC_FX_BuildE00ReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint16_t address, uint16_t length);

int MELSEC_FX_BuildE01ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock,uint16_t address, uint16_t length);

int MELSEC_FX_ParseReadResp(const uint8_t *frame, uint16_t frame_len);

// E10 - 写入PLC参数
int MELSEC_FX_BuildE10WriteParamCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint16_t address, const uint16_t *data, uint16_t data_count);
int MELSEC_FX_Build_E16_write_R_Cmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint16_t address, const uint16_t *data, uint16_t word_count);
int MELSEC_FX_ParseE10WriteResp(const uint8_t *frame, uint16_t frame_len);

// EE - 读写内部存储器数据

int MELSEC_FX_BuildEEReadCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint32_t address, uint16_t length);

int MELSEC_FX_BuildEEWriteCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint32_t address, const uint16_t *data, uint16_t word_count);
int MELSEC_FX_ParseEEWriteResp(const uint8_t *frame, uint16_t frame_len);

// F5 - 读写内部寄存器(变长地址)
int MELSEC_FX_BuildF5ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock, const uint32_t *addr_data, uint16_t count);
// F0/F1 - 外扩模块读写
int MELSEC_FX_BuildF0ReadCmd(uint8_t Sour_Sock, uint8_t Dest_Sock, uint8_t cmd, uint16_t address, uint8_t length);
int MELSEC_FX_BuildF1WriteCmd(uint8_t Sour_Sock, uint8_t Dest_Sock, uint8_t cmd, uint16_t address, uint8_t length, const uint8_t *data);
// 通用E命令

// ==================== 通信帧构造 ====================

uint16_t BuildReadCommand(uint8_t *buf, char dev_code, uint16_t address, uint16_t bytes);
uint16_t BuildWriteCommand(uint8_t *buf, char dev_code, uint16_t address, uint16_t bytes, const uint8_t *data);
uint16_t BuildForceOnCommand(uint8_t *buf, uint16_t plc_address);
uint16_t BuildForceOffCommand(uint8_t *buf, uint16_t plc_address);

// ==================== 强制控制ON/OFF命令 ====================
int MELSEC_FX_Build_E7_E8_ForceCmd(uint8_t Sour_Sock ,uint8_t  Dest_Sock, uint16_t e_code,
                                    uint16_t address, uint8_t force_data);
// ==================== 远程控制命令 ====================
int BuildRemoteControlCommand(uint8_t Sour_Sock ,uint8_t  Dest_Sock,
                                     uint8_t sub_cmd, uint16_t addr) ;

// ==================== 地址转换 ====================

// 定时器 T 当前值地址映射（表2）
uint16_t GetTimerCurrentValueAddress_T(uint16_t t_number, uint16_t *group_address);

// 计数器 C 当前值地址映射（根据c_number范围自动判断）
// 16位计数器（C0~C199）：占用2个连续字节地址，地址 = 0x0A00 + c_num * 2
// 32位计数器（C200~C255）：占用2个连续字地址（4字节），地址 = 0x0C00 + c_num * 4
// @param c_number 计数器编号
// @param addr_array 输出地址数组（2个元素）
// @return 成功返回地址数量（2），失败返回0
uint16_t GetCounterCurrentValueAddress_C(uint16_t c_number, uint16_t *addr_array);

// 数据寄存器 D 地址映射（支持普通寄存器和特殊寄存器）
// 普通寄存器：D0~D7999，地址 = 0x1000 + d_number*2
// 特殊寄存器：D8000~D8599，地址 = 0x0E00 + (d_number-8000)*2
// @param d_number 寄存器编号
// @param addr 输出地址指针
// @return 成功返回2，失败返回0
uint16_t GetDataRegisterAddress_D(uint16_t d_number, uint16_t *group_address);

// 强制 ON/OFF 位地址映射（表7a、7b）
uint16_t GetForceBitAddress(const char *symbol, uint16_t *plc_address);

// 符号地址解析
int ParseSymbolAddress(const char *symbol, char *dev_type, uint16_t *number);

uint8_t sim_EE_unknown(uint32_t ee_addr, uint8_t *frame_buff, uint16_t frame_len);

// MC协议设备代码转换函数
uint8_t MC_Net_DeviceCodeToType(uint16_t dev_code);
/* 判断设备代码是否为字软元件(D/R/T/C), 返回 1=是 0=否 */
uint8_t MC_Net_DeviceCodeIsWord(uint16_t dev_code);


#endif


