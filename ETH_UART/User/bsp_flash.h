 
#ifndef _BSP_FLASH_H_
#define _BSP_FLASH_H_

#include "ch32v30x.h"

/* Global define */

/* CH32V307VC Flash: 512KB, 0x08000000 ~ 0x0807FFFF
 * Bank1区域: 0x08000000 ~ 0x0803FFFF (256KB)
 * 代码区由链接脚本分配, 配置区放在 Bank1 末尾 3 个 256B 快速页.
 *
 * 使用 FLASH_ErasePage_Fast (256B/页) 精细擦除, 每项配置独占一页.
 * 另保留 FLASH_PAGE_SIZE (4KB) 供标准擦除函数使用.
 *
 * 地址布局 (FLASH_PAGE_SIZE_FAST=256):
 *   PAGE_WRITE_START_ADDR = 0x0803FD00  (Bank1鏈熬 - 3脳256B)
 *   BASIC_CFG_ADDR        = 0x0803FD00  第1页 Basic配置 (256B)
 *   PORT_CFG_ADDR         = 0x0803FE00  第2页 Port配置  (256B)
 *   LOGIN_CFG_ADDR        = 0x0803FF00  第3页 Login配置 (256B)
 *   PAGE_WRITE_END_ADDR   = 0x08040000  (越界哨兵, Bank1边界)
 */
#define FLASH_PAGE_SIZE           (4096)
#define FLASH_PAGE_SIZE_FAST      (256)   /* 快速擦除单页大小 256B */
#define FLASH_BANK1_END_ADDR      ((uint32_t)0x0803FFFF)
#define CFG_PAGE_COUNT            (3)
#define PAGE_WRITE_START_ADDR     ((uint32_t)FLASH_BANK1_END_ADDR + 1 - (FLASH_PAGE_SIZE_FAST * CFG_PAGE_COUNT))
#define PAGE_WRITE_END_ADDR       ((uint32_t)PAGE_WRITE_START_ADDR + FLASH_PAGE_SIZE_FAST * CFG_PAGE_COUNT)

#define BASIC_CFG_ADDR            ((uint32_t)PAGE_WRITE_START_ADDR + FLASH_PAGE_SIZE_FAST * 0)
#define PORT_CFG_ADDR             ((uint32_t)PAGE_WRITE_START_ADDR + FLASH_PAGE_SIZE_FAST * 1)
#define LOGIN_CFG_ADDR            ((uint32_t)PAGE_WRITE_START_ADDR + FLASH_PAGE_SIZE_FAST * 2)

#define BASIC_CFG_LEN             (sizeof(Basic_Cfg_t))
#define PORT_CFG_LEN              (sizeof(Port_Cfg_t))
#define LOGIN_CFG_LEN             (sizeof(Login_Cfg_t))

typedef struct Basic_Cfg                        //Basic configuration parameters
{
	u8 flag[2];                                 //Configuration information verification code: 0x57,0xab
	u8 mac[6];
	u8 ip[4];
	u8 mask[4];
	u8 gateway[4];
} Basic_Cfg_t;


typedef struct Port_Cfg                          //Port configuration parameters
{
    u8 flag[2];                                 //Configuration information verification code: 0x57,0xab
    u8 mode;
    u8 src_port[2];
    u8 des_ip[4];
    u8 des_port[2];
} Port_Cfg_t;


typedef struct Login_Cfg                         //Login configuration parameters
{
    u8  flag[2];                                //Configuration information verification code: 0x57,0xab
    u8  user[11];
    u8  pass[11];
} Login_Cfg_t;

extern Basic_Cfg_t Basic_CfgBuf;

extern Login_Cfg_t Login_CfgBuf;

extern Port_Cfg_t  Port_CfgBuf;


extern void BSP_FLASH_ERASE(u32 Page_Address, u32 Length );

extern FLASH_Status BSP_FLASH_ERASE_Fast(uint32_t Page_Address, u32 Length);

extern FLASH_Status BSP_FLASH_WriteConfig(u32 Addr, const u8 *Buffer, u32 Length);

extern FLASH_Status BSP_FLASH_WRITE_Word( u32 StartAddr, const u8 *Buffer, u32 Length );

extern FLASH_Status BSP_FLASH_WRITE_HalfWord(u32 StartAddr, const u8 *Buffer, u32 Length);

extern FLASH_Status BSP_FLASH_WRITE_ByteData(u32 StartAddr, const u8 *Buffer, u32 Length);

extern void BSP_FLASH_READ(u32 StartAddr, u8 *Buffer, u32 Length);

extern void BSP_FLASH_RestoreDefaults(void);

uint32_t RecalculateCRC(uint32_t *SRC_BUF, uint32_t suze);

uint8_t CRC_Is_Used();

#endif /* end of bsp_Flash.h */

