#ifndef MELSEC_FX_TABLES_H
#define MELSEC_FX_TABLES_H

#include "ch32v30x.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* USER CODE END Private defines */


#ifdef _TABLES_DEBUG
    #define TABLES_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define TABLES_DEBUG(format, ...)
#endif

#define     MC_FX_D   0x4420     // 数据寄存器  D 
#define     MC_FX_R   0x5220     // 扩展寄存器  R 
#define     MC_FX_TN  0x544E     // 定时器  当前值  TN 
#define     MC_FX_TS  0x5453     // 定时器  触点   TS 
#define     MC_FX_CN  0x434E     // 计数器  当前值 CN 
#define     MC_FX_CS  0x4353     // 计数器  触点   CS 
#define     MC_FX_X   0x5820     // 输入    触点    X 
#define     MC_FX_Y   0x5920     // 输出    触点    Y
#define     MC_FX_M   0x4D20     // 辅助继电器 触点  M 
#define     MC_FX_S   0x5320     // 状态  触点     S

#define     MC_FX_E0  0x4530     // PLC系统参数    E0
#define     MC_FX_E1  0x4531     // PLC系统参数    E1
#define     MC_FX_E2  0x4532     // PLC系统参数    E2
#define     MC_FX_E3  0x4533     // PLC系统参数    E3
#define     MC_FX_E4  0x4534     // PLC系统参数+网口配置      E4 Sim_EE_4xxxx 
#define     MC_FX_E5  0x4535     // PLC网口诊断               E5 Sim_EE_5xxxx 
#define     MC_FX_E6  0x4536     // 读取R寄存器     E6
#define     MC_FX_E7  0x4537     // 强制置位        E7
#define     MC_FX_E8  0x4538     // 强制复位        E8
#define     MC_FX_E9  0x4539     // PLC系统参数     E9 Sim_EE_9xxxx 
#define     MC_FX_EE  0x4545     // PLC程序 源代码    EE 


//元件类型枚举  
typedef enum
{
    type_NULL,    //无效
    //位元件 X、Y、S、M、CO、CE、TO、TE    
    type_X  ,       //X0~X377
    type_Y  ,       //Y0~Y377
    type_S  ,       //S0~S4096
    type_M  ,       //M0~M8511    
    
    type_CO ,       // C触点 
    type_CE ,       // C线圈
    type_CF,        // C标志 //当[RST C0]前面母线成立时，CF[0] 置1，否则置0

    type_TO ,       // T触点
    type_TE ,       // T线圈
    type_TF ,       // T标志 //当[RST T0]前面母线成立时，TF[0] 置1，否则置0
    // 字元件D、T、R、C、C32
    type_D  ,       //D0~D8511
    type_T  ,       //T0~T511 当前值
    type_T_set,     //T0~T511 设定值
    type_C  ,       //C0~C255 当前值    //注意C0~C199是16位，C200~C255是32位
    type_C_set ,    //C0~C95 设定值     //注意C0~C199是16位，C200~C255是32位 
    type_R  ,       //R0~R32767
    //
    type_V  ,       //V偏移量(32位数操作时，用于16-31位)
    type_Z  ,       //Z偏移量(32位数操作时，用于0-15位)
    type_K  ,       //10进制数
    type_H  ,       //16进制数
    type_E  ,       //浮点数
    type_UG ,       //扩展模块   F0:读取UG扩展模块的状态 F1:写入UG扩展模块的状态
    type_E1 ,       //PLC 系统参数
    type_E6,        //E6清除PLC存储器
    type_EE,        //PLC 内部EEPROM
    type_EB,        //解除关键字(密码)
    type_F5,        //F5 读寄存器、状态
    type_F7,        //F7 备用未知功能
    type_F8,        //F8 查看程序梯形图结束地址
    type_FC,        //FC 在线写入程序
    type_END        //结束标记  
}element_type_enum;


typedef struct
{
    uint8_t  eBitNum;   // 元件位宽
    uint8_t  eType;     // 元件类型
    uint16_t e_code;    // 软元件代码
    uint16_t eStart;    // 元件开始索引
    uint16_t eEnd;      // 元件结束索引

    uint16_t simStart;  // 仿真开始地址
    uint16_t simEnd;    // 仿真结束地址

} MELSEC_FX_TABLES_T;
 
/***  批量 30读取/40写入 指令通道 ***/
extern const MELSEC_FX_TABLES_T FX_30_40_Tables[14];
#define TABLE_30_40_SIZE  14

/***  批量 E0读取/E1写入 指令通道 ***/
extern const MELSEC_FX_TABLES_T FX_E0_E1_Tables[13];
#define TABLE_E0_E1_SIZE  13

/***  强制位 E7置位/E8复位 指令通道 ***/
extern const MELSEC_FX_TABLES_T FX_E7_E8_Tables[7];
#define TABLE_E7_E8_SIZE  7

/***  批量 F5读取  指令通道 ***/
extern const MELSEC_FX_TABLES_T FX_F5_Read_Tables[13];
#define TABLE_F5_Read_SIZE 13
// ==================== 元件映射地址查找函数 ====================
// 根据软元件代码和索引在指定映射表中查找仿真地址偏移量
// 参数: table   - 映射表数组指针（如 FX_E0_E1_Tables）
//       count   - 映射表条目数量
//       e_code  - 软元件代码（2字节，如 "D ""TN""X " 等）
//       eIndex  - 软元件索引编号
//       mapIndex - 输出：仿真地址偏移量
// 返回值: 0-成功找到，1-未找到
int sim_find_tables_idx_element(const MELSEC_FX_TABLES_T *table, 
                                uint8_t count, uint16_t e_code, 
                                uint16_t eIndex, uint16_t *mapIndex);
uint8_t sim_find_tables_idx_bit_element(const MELSEC_FX_TABLES_T *table,
                                    uint8_t count, uint16_t e_code,
                                    uint16_t eIndex, uint16_t *mapIndex);
// 表2：定时器 T 当前值地址映射 (0800 ~ 09E0)
// 返回值：成功返回地址，失败返回0
uint16_t GetTimerAddressOffset_T(uint16_t t_number);

// 表3：16位计数器 C 当前值地址映射
// C0 -> 0A00(lower), 0A01(upper)
// 返回值：低字节地址，高字节地址 = 低字节地址 + 1
uint16_t GetCounter16Address_C(uint16_t c_number);
// 表4：32位计数器 C 当前值地址映射
// C0 -> 0A00(lower), 0A01(upper)
// 返回值：低字节地址，高字节地址 = 低字节地址 + 1
uint16_t GetCounter32Address_C(uint16_t c_number);

// 表6：特殊寄存器 D 地址映射 (0E00 ~ 0F00)
// D8000及以上有效
// 返回值：成功返回地址，失败返回0
uint16_t GetSpecialRegisterAddressOffset_D(uint16_t d_number);
uint16_t GetDataRegisterAddressOffset_D(uint16_t d_number);
// 表7a：S/X/Y/T 强制地址 (S0~S559)
// 返回值：S0~S559对应的强制地址
uint16_t GetForceAddress_S(uint16_t s_number);

uint16_t GetForceAddress_X(uint16_t x_number);

uint16_t GetForceAddress_Y(uint16_t y_number);

uint16_t GetForceAddress_T(uint16_t t_number);

// 表7b：M 强制地址 (M0~M1023)
// 返回值：M0~M1023对应的强制地址
uint16_t GetForceAddress_M(uint16_t m_number);

// 表7b：C 强制地址 (C0~C255)
// 返回值：C0~C255对应的强制地址
uint16_t GetForceAddress_C(uint16_t c_number);

// ==================== 表1a/1b/1c：位元件映射表 ====================

// 位范围结构体
typedef struct {
    uint16_t start;
    uint16_t end;
} BitRange_t;

// ==================== 表1a：位元元件位址（GROUP ADDRESS）算法函数 ====================
// 地址范围：0000 ~ 0100，共17行，每行16列
// 参数: address - PLC位地址（如 0x0000, 0x0080 等）
//       col_idx - 列索引(0-15)，对应每列的8位
// 返回值: BitRange_t 结构体，如果地址无效则返回 {0xFFFF, 0xFFFF}
BitRange_t CalcTable1ABitRange(uint16_t address, uint8_t col_idx);

// 根据地址和元件号计算位范围
// 参数: address - PLC位地址
//       element_num - 元件号（如 M100, T50 等）
// 返回值: BitRange_t 结构体
BitRange_t GetBitRangeByElement(uint16_t address, uint16_t element_num);

// ==================== 表1b：T/C 位元地址映射算法函数 ====================
// 地址范围：02A0 ~ 02D0，共4行，每行16列
// 参数: address - PLC位地址（02A0/02B0/02C0/02D0）
//       col_idx - 列索引(0-15)
// 返回值: BitRange_t 结构体
BitRange_t CalcTable1BBitRange(uint16_t address, uint8_t col_idx);

// 根据定时器/计数器号获取位范围
// 参数: is_timer - 1=定时器, 0=计数器
//       tc_number - T/C编号(0-255)
// 返回值: BitRange_t 结构体
BitRange_t GetTCBitRange(uint8_t is_timer, uint16_t tc_number);

// ==================== 表1c：BIT IMAGES GROUP ADDRESS算法函数 ====================
// 地址范围：02C0 ~ 02D0，共2行，每行16列
// 参数: address - PLC位地址（02C0/02D0）
//       col_idx - 列索引(0-15)
// 返回值: BitRange_t 结构体
BitRange_t CalcTable1CBitRange(uint16_t address, uint8_t col_idx);


#ifdef __cplusplus
}
#endif

#endif // MELSEC_FX_TABLES_H
