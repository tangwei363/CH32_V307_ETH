#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"
#include <ctype.h>
#include <stdlib.h>

// ==================== 表2：定时器 T 当前值地址映射 ====================
uint16_t GetTimerCurrentValueAddress_T(uint16_t t_number, uint16_t *group_address) {
    uint16_t addr = GetTimerAddressOffset_T(t_number);
    if (addr == 0) return 0;
    *group_address = addr;
    return 1;
}

// ==================== 计数器 C 当前值地址映射 ====================
// 根据c_number范围自动判断是16位还是32位计数器
// 16位计数器（C0~C199）：占用2个连续字节地址，地址 = 0x0A00 + c_num * 2
// 32位计数器（C200~C255）：占用2个连续字地址（4字节），地址 = 0x0C00 + c_num * 4
// @param c_number 计数器编号
// @param addr_array 输出地址数组（2个元素）
// @return 成功返回地址数量（1），失败返回0
uint16_t GetCounterCurrentValueAddress_C(uint16_t c_number, uint16_t *addr_array) {
    uint16_t addr;

    if (c_number < 200) {
        // 16位计数器：C0~C199
        addr = GetCounter16Address_C(c_number);
        if (addr == 0) return 0;
        *addr_array = addr;
    } else if (c_number >= 200 && c_number <= 255) {
        // 32位计数器：C200~C255
        addr = GetCounter32Address_C(c_number);
        if (addr == 0) return 0;
        *addr_array = addr;
    } else {
        // 超出范围
        return 0;
    }
    return 1;
}

// ==================== 数据寄存器 D 地址映射 ====================
// 根据d_number范围自动判断是普通数据寄存器还是特殊寄存器
// 普通寄存器：D0~D7999，地址 = 0x1000 + d_number*2
// 特殊寄存器：D8000~D8511，地址 = 0x0E00 + (d_number-8000)*2
// @param d_number 寄存器编号
// @param addr 输出地址指针
// @return 成功返回1，失败返回0
uint16_t GetDataRegisterAddress_D(uint16_t d_number, uint16_t *addr) {
    uint16_t result;

    if (d_number <= 7999) {
        // 普通数据寄存器 D0~D7999
        result = GetDataRegisterAddressOffset_D(d_number);
    } else if (d_number >= 8000 && d_number <= 8511) {
        // 特殊寄存器 D8000~D8511
        result = GetSpecialRegisterAddressOffset_D(d_number);
    } else {
        // 超出范围
        return 0;
    }

    if (result == 0) return 0;
    *addr = result;
    return 1;
}

// ==================== 表7a/7b：强制 ON/OFF 位地址映射 ====================
uint16_t GetForceBitAddress(const char *symbol, uint16_t *addr) {
    char dev = toupper(symbol[0]);
    uint16_t num = atoi(symbol + 1);

    if (dev == DEV_X) {
        *addr = GetForceAddress_X(num);
    } else if (dev == DEV_Y) {
        *addr = GetForceAddress_Y(num);
    } else if (dev == DEV_S) {
        *addr = GetForceAddress_S(num);
    } else if (dev == DEV_M) {
        *addr = GetForceAddress_M(num);
    } else if (dev == DEV_T) {
        *addr = GetForceAddress_T(num);
    } else if (dev == DEV_C) {
        *addr = GetForceAddress_C(num);
    } else {
        return 0;
    }

    return (*addr == 0) ? 0 : 1;
}

