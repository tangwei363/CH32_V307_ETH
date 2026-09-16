/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_flash.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2024/07/18
* Description        : Flash 配置读写模块
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


#include "debug.h"

/* 全局宏定义 */

#include "bsp_flash.h"
#include "HTTPS.h"

/* 全局变量 */
volatile FLASH_Status FLASHStatus = FLASH_COMPLETE;
 

Web_Page_State_t Web_Page_State[4] = {0};
/* 定义三个结构体数组, 分别保存基本设置、端口设置、登录设置
 * TODO: 数组长度 4/4/2 为硬编码, 建议改为宏统一管理 */
Parameter Para_Basic[4], Para_Port[4], Para_Login[2];

Basic_Cfg_t Basic_CfgBuf;
Port_Cfg_t  Port_CfgBuf;
Login_Cfg_t Login_CfgBuf;

/* WCHNET 网络参数默认配置
 * 格式: [0~1]校验码 0x57AB, [2~7]MAC, [8~11]IP, [12~15]掩码, [16~19]网关 */
const u8 Basic_Default[BASIC_CFG_LEN] = {
0x57, 0xAB,
01, 02, 03, 04, 05, 06, 192, 168, 1, 250, 255, 255, 255, 0, 192, 168, 1, 1};

/* WCHNET 登录默认参数: 用户名 admin, 密码 123
 * 格式: [0~1]校验码 0x57AB, [2~12]用户名(11字节), [13~23]密码(11字节)
 * TODO: 生产环境应修改默认密码 */
const u8 Login_Default[LOGIN_CFG_LEN] = {
0x57, 0xAB,
'a', 'd', 'm', 'i', 'n', 0, 0, 0, 0, 0, 0, '1', '2', '3', 0, 0, 0, 0, 0, 0, 0, 0 };

/* WCHNET 端口默认配置: TCP客户端模式
 * 格式: [0~1]校验码, [2]模式, [3~4]源端口(大端), [5~8]目标IP, [9~10]目标端口(大端) */
const u8 Port_Default[PORT_CFG_LEN] = {
0x57, 0xAB,
MODE_TCPCLIENT, 1000 / 256, 1000 % 256, 192, 168, 1, 100, 1000 / 256, 1000 % 256 };

/*********************************************************************
 * @fn      BSP_FLASH_RestoreDefaults
 *
 * @brief   恢复网络配置为出厂默认值, 逐项写入 Flash 后软件复位.
 *
 * @return  无 (本函数不返回, 最终调用 NVIC_SystemReset)
 *
 * @note    使用 BSP_FLASH_WriteConfig (擦除→写入→校验), 每项配置独占 256B 快速页.
 *          任一阶段失败会打印详��且最终仍复位, 确保不残留半写数据.
 */
void BSP_FLASH_RestoreDefaults(void)
{
    FLASH_Status status;

    WCHNET_GetMacAddr((u8*)&Basic_Default[2]);

    status = BSP_FLASH_WriteConfig(BASIC_CFG_ADDR, Basic_Default, BASIC_CFG_LEN);
    BSP_DEBUG("Restore BASIC: %d\r\n", status);
    status = BSP_FLASH_WriteConfig(PORT_CFG_ADDR,  Port_Default,  PORT_CFG_LEN);
    BSP_DEBUG("Restore PORT:  %d\r\n", status);
    status = BSP_FLASH_WriteConfig(LOGIN_CFG_ADDR, Login_Default, LOGIN_CFG_LEN);
    BSP_DEBUG("Restore LOGIN: %d\r\n", status);

    Delay_Ms(10);
    NVIC_SystemReset();  /* 软件复位使新配置生效 */
}

/*********************************************************************
 * @fn	RecalculateCRC
 *
 * @brief The function RecalculateCRC calculates the CRC value of a given buffer and sets the ID register to a
 * 		specific value.
 *
 * @param SRC_BUF SRC_BUF is the source buffer, which is a pointer to the start of the data that needs
 * 		to be used for CRC calculation.
 *        suze The parameter "suze" is likely a typo and should be "size". It represents the size of
 * 		the source buffer in bytes.
 *
 * @return the calculated CRC value as a uint32_t.
 */
uint32_t RecalculateCRC(uint32_t *SRC_BUF, uint32_t suze)
{
	uint32_t temp;
	if (CRC_GetIDRegister() == 0xaa)
		CRC_ResetDR();
	temp = CRC_CalcBlockCRC((u32 *)SRC_BUF, suze);
	CRC_SetIDRegister(0xaa);
	return temp;
}

/*********************************************************************
 * @fn CRC_Is_Used
 *
 * @brief The function checks if the CRC module is being used by comparing the ID register value to 0xaa and
 * returns 1 if it is being used, otherwise it returns 0.
 *
 * @return a value of type uint8_t, which is an 8-bit unsigned integer. The function is checking if the
 * value returned by the CRC_GetIDRegister() function is equal to 0xaa. If it is, the function returns
 * 1. If it is not, the function returns 0.
 */
uint8_t CRC_Is_Used()
{
	if (CRC_GetIDRegister() == 0xaa)
		return 1;
	else
		return 0;
}

/*********************************************************************
 * @fn      BSP_FLASH_ERASE
 *
 * @brief   擦除 Data-Flash 扇区, 最小擦除单位为 4K 字节(FLASH_ErasePage).
 *          如需 256B 精细擦除, 使用 BSP_FLASH_ERASE_Fast.
 *
 * @param   Page_Address - 起始页地址 (需 4K 对齐)
 *          Length       - 擦除长度(字节, 建议为 FLASH_PAGE_SIZE 整数倍)
 *
 * @return  无
 *
 * @note    Length 若不是 FLASH_PAGE_SIZE(4KB) 整数倍则尾部不足一页部分不会被擦除.
 *          循环中若某一页擦除失败, 会 Lock Flash 后提前返回, 后续页面不再擦除.
 */
void BSP_FLASH_ERASE(uint32_t Page_Address, u32 Length) {
    u32 NbrOfPage, EraseCounter;

    FLASH_Unlock();
    /* TODO: Length 非页对齐时尾部被截断, 建议改为向上取整:
     *       NbrOfPage = (Length + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE; */
    NbrOfPage = Length / FLASH_PAGE_SIZE;
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
    for (EraseCounter = 0; EraseCounter < NbrOfPage; EraseCounter++) {
        /* BUGFIX: 使用局部变量而非全局 FLASHStatus, 避免被其他操作覆盖 */
        FLASH_Status status = FLASH_ErasePage(Page_Address + (FLASH_PAGE_SIZE * EraseCounter));
        if (status != FLASH_COMPLETE) {
            printf("FLASH Erase Fail at 0x%08lX\r\n", Page_Address + (FLASH_PAGE_SIZE * EraseCounter));
            FLASH_Lock();  /* BUGFIX: 提前返回前必须 Lock, 否则 Flash 保持解锁 */
            return;
        }
    }
    FLASH_Lock();
}

/*********************************************************************
 * @fn      BSP_FLASH_ERASE_Fast
 *
 * @brief   按 256 字节页快速擦除 Data-Flash.
 *          底层调用 FLASH_ErasePage_Fast, 需要 FLASH_Unlock_Fast 进入快速模式.
 *
 * @param   Page_Address - 起始页地址 (须 256 字节对齐, 内部自动 & 0xFFFFFF00 对齐)
 *          Length       - 擦除长度(字节, 需为 256 的整数倍)
 *
 * @return  FLASH_Status - FLASH_COMPLETE 表示成功; FLASH_TIMEOUT 表示等待忙超时
 *
 * @note    FLASH_ErasePage_Fast 无返回值, 通过等待 BSY 标志位判断完成. 若超时返回
 *          FLASH_TIMEOUT. 相比标准擦除(4KB/22ms), 快速擦除(256B/1.4ms)更精细,
 *          适合只擦除小范围配置区. 注意: FLASH_ErasePage_Fast 擦除后自动退出 BSY,
 *          写入前仍需 FLASH_Unlock (标准解锁) 而非 FLASH_Unlock_Fast.
 */
FLASH_Status BSP_FLASH_ERASE_Fast(uint32_t Page_Address, u32 Length) {
    u32 NbrOfPage, EraseCounter;
    u32 timeout;

    /* 向上取整确保覆盖所有数据 */
    NbrOfPage = (Length + 255) / 256;

    FLASH_Unlock_Fast();  /* 进入快速擦除模式 */
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);

    for (EraseCounter = 0; EraseCounter < NbrOfPage; EraseCounter++) {
        FLASH_ErasePage_Fast(Page_Address + (256 * EraseCounter));

        /* FLASH_ErasePage_Fast 无返回值, 手动等待 BSY 位清除 */
        timeout = 0x100000;
        while (FLASH->STATR & (1 << 0)) {  /* SR_BSY */
            if (--timeout == 0) {
                printf("FLASH Erase Fast Timeout at 0x%08lX\r\n",
                       Page_Address + (256 * EraseCounter));
                FLASH_Lock_Fast();  /* 超时退出快速模式 */
                return FLASH_TIMEOUT;
            }
        }
    }

    FLASH_Lock_Fast();  /* 退出快速擦除模式 */
    return FLASH_COMPLETE;
}

/*********************************************************************
 * @fn      BSP_FLASH_WriteConfig
 *
 * @brief   配置数据写入 Flash 原子操作: 擦除(256B) → 写入(数据+CRC) → CRC校验.
 *          数据布局: [0..Length-1] 用户数据, [Length..251] 0xFF填充,
 *                    [252..255] CRC32校验值 (硬件CRC, RecalculateCRC).
 *
 * @param   Addr   - Flash 目标地址 (需 256B 对齐)
 *          Buffer - 数据源缓冲区
 *          Length - 数据长度(字节, 若不足4的倍数则尾部自动补0对齐)
 *
 * @return  FLASH_COMPLETE  全部成功
 *          其他值          失败 (擦除超时/写入错误/CRC不匹配)
 *
 * @note    每次写入完整 256B 页: 用户数据 + 填充 + CRC32.
 *          回读校验时重算 CRC 并与尾部存储值比对, 单次比较即可验证完整性.
 */
FLASH_Status BSP_FLASH_WriteConfig(u32 Addr, const u8 *Buffer, u32 Length)
{
    FLASH_Status status;
    u32 crc_calc, crc_stored;
    u32 data_words, i;
    /* 写缓冲区: [0..data_words-1]=用户数据, [63]=CRC32, 其余 0xFFFFFFFF */
    u32 write_buf[FLASH_PAGE_SIZE_FAST / 4];

    /* 数据向上取整到 u32 边界, 不足一字的部分补 0 后参与 CRC */
    data_words = (Length + 3) / 4;

    /* 初始化整个缓冲区为 0xFFFFFFFF (Flash 擦除态, 减少编程磨损) */
    for (i = 0; i < FLASH_PAGE_SIZE_FAST / 4; i++) {
        write_buf[i] = 0xFFFFFFFF;
    }
    /* 复制用户数据 (按字拷贝, 不足一字的部分自动由 0xFFFFFFFF 补位 → 需修正) */
    for (i = 0; i < Length; i++) {
        ((u8 *)write_buf)[i] = Buffer[i];
    }
    /* 尾部不足一字的字节清零 (避免 0xFF 污染 CRC) */
    for (i = Length; i < data_words * 4; i++) {
        ((u8 *)write_buf)[i] = 0x00;
    }

    /* 阶段0: 计算数据区 CRC32 (覆盖 Length 向上对齐后的 data_words 个字) */
    crc_calc = RecalculateCRC(write_buf, data_words);
    /* CRC 存入 256B 页尾部最后一个 u32 */
    write_buf[(FLASH_PAGE_SIZE_FAST / 4) - 1] = crc_calc;

    /* 阶段1: 256B 快速页擦除 */
    status = BSP_FLASH_ERASE_Fast(Addr, FLASH_PAGE_SIZE_FAST);
    if (status != FLASH_COMPLETE) {
        BSP_DEBUG("WriteCfg Erase Fail at 0x%08lX: %d\r\n", Addr, status);
        return status;
    }

    /* 阶段2: 写入完整 256B 页 (数据 + 填充 + CRC) */
    status = BSP_FLASH_WRITE_Word(Addr, (u8 *)write_buf, FLASH_PAGE_SIZE_FAST);
    if (status != FLASH_COMPLETE) {
        BSP_DEBUG("WriteCfg Write Fail at 0x%08lX: %d\r\n", Addr, status);
        return status;
    }

    /* 阶段3: CRC 校验 — 回读数据区, 重算 CRC 与尾部存储值比对 */
    BSP_FLASH_READ(Addr, (u8 *)write_buf, FLASH_PAGE_SIZE_FAST);
    crc_calc = RecalculateCRC(write_buf, data_words);
    crc_stored = write_buf[(FLASH_PAGE_SIZE_FAST / 4) - 1];

    if (crc_calc != crc_stored) {
        BSP_DEBUG("WriteCfg CRC Mismatch at 0x%08lX: Calc=%08lX Stored=%08lX\r\n",
                  Addr, crc_calc, crc_stored);
        return (FLASH_Status)(-1);  /* CRC 校验失败 */
    }

    return FLASH_COMPLETE;
}

/*********************************************************************
 * @fn      BSP_FLASH_WRITE_Word
 *
 * @brief   按字(4字节)写入 Data-Flash.
 *          底层调用 FLASH_ProgramWord, 该函数内部拆为两次半字写入
 *          (先写低16位到 Address, 再写高16位到 Address+2).
 *
 * @param   StartAddr - 写入起始地址 (需 4 字节对齐)
 *          Buffer    - 数据缓冲区指针 (需 4 字节对齐)
 *          Length    - 数据长度(字节, 需为 4 的整数倍)
 *
 * @return  FLASH_Status - FLASH_COMPLETE 表示成功, 其他值表示失败
 *
 * @note    对齐要求: Cortex-M3 内核, u8* → u32* 非对齐可能触发 UsageFault.
 *          Length 非 4 的倍数时尾部不足一字的数据会被丢弃.
 */
FLASH_Status BSP_FLASH_WRITE_Word(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u32 *p_buff = (u32 *) Buffer;  // 要求 Buffer 4 字节对齐
    FLASH_Status FLASHStatus = FLASH_COMPLETE;

    FLASH_Unlock();
    while ((address < (StartAddr + Length)) && (FLASHStatus == FLASH_COMPLETE))
    {
        FLASHStatus = FLASH_ProgramWord(address, *p_buff);
        address += 4;
        p_buff++;
    }
    FLASH_Lock();
    return FLASHStatus;
}

/*********************************************************************
 * @fn      BSP_FLASH_WRITE_HalfWord
 *
 * @brief   按半字(2字节)写入 Data-Flash.
 *          底层调用 FLASH_ProgramHalfWord.
 *
 * @param   StartAddr - 写入起始地址 (需 2 字节对齐)
 *          Buffer    - 数据缓冲区指针 (需 2 字节对齐)
 *          Length    - 数据长度(字节, 需为 2 的整数倍)
 *
 * @return  FLASH_Status - FLASH_COMPLETE 表示成功, 其他值表示失败
 */
FLASH_Status BSP_FLASH_WRITE_HalfWord(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u16 *p_buff = (u16 *) Buffer;  // 要求 Buffer 2 字节对齐
    FLASH_Status FLASHStatus = FLASH_COMPLETE;

    FLASH_Unlock();
    while ((address < (StartAddr + Length)) && (FLASHStatus == FLASH_COMPLETE))
    {
        FLASHStatus = FLASH_ProgramHalfWord(address, *p_buff);
        address += 2;
        p_buff++;
    }
    FLASH_Lock();
    return FLASHStatus;
}

/*********************************************************************
 * @fn      BSP_FLASH_WRITE_ByteData
 *
 * @brief   【严重Bug】按字节写入选项字节区 (0x1FFFF800), 非数据 Flash!
 *          底层调用 FLASH_ProgramOptionByteData, 该 SDK 函数固定操作选项字节区域,
 *          Address 参数为选项字节内的偏移, 而非数据 Flash 地址.
 *
 * @param   StartAddr - 选项字节区偏移地址
 *          Buffer    - 数据缓冲区指针
 *          Length    - 数据长度(字节)
 *
 * @return  FLASH_Status - FLASH_COMPLETE 表示成功, 其他值表示失败
 *
 * @warning 本函数操作的是选项字节区 (Option Bytes), 不是常规 Flash 数据区.
 *          若需按字节写数据 Flash, 请改用 BSP_FLASH_WRITE_Word 并自行处理
 *          读-改-写流程 (Flash 不支持字节级写入).
 */
FLASH_Status BSP_FLASH_WRITE_ByteData(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u8 *p_buff = (u8 *) Buffer;
    FLASH_Status FLASHStatus = FLASH_COMPLETE;

    FLASH_Unlock();
    while ((address < (StartAddr + Length)) && (FLASHStatus == FLASH_COMPLETE))
    {
        /* 注意: FLASH_ProgramOptionByteData 操作的是选项字节区, 非数据 Flash */
        FLASHStatus = FLASH_ProgramOptionByteData(address, *p_buff);
        address++;
        p_buff++;
    }
    FLASH_Lock();
    return FLASHStatus;
}

/*********************************************************************
 * @fn      BSP_FLASH_READ
 *
 * @brief   按字(4字节)读取 Data-Flash (Flash 是内存映射的, 直接解引用即可).
 *
 * @param   StartAddr - 读取起始地址
 *          Buffer    - 数据缓冲区指针 (需 4 字节对齐)
 *          Length    - 数据长度(字节, 需为 4 的整数倍)
 *
 * @return  无
 *
 * @note    Cortex-M3 可直接读取 Flash 映射地址, 无需解锁.
 *          Length 非 4 的倍数时尾部不足一字的数据不会被读取.
 */
void BSP_FLASH_READ(u32 StartAddr, u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u32 *p_buff = (u32 *) Buffer;  // 要求 Buffer 4 字节对齐

    while (address < (StartAddr + Length))
    {
        *p_buff = (*(u32 *)address);  // 直接解引用 Flash 映射地址
        address += 4;
        p_buff++;
    }
}

/*********************************************************************/
 
