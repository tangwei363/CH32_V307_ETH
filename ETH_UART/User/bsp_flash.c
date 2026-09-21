/********************************** (C) COPYRIGHT *******************************
* File Name          : bsp_flash.c
* Author             : WCH
* Version            : V1.0.0
* Date               : 2024/07/18
* Description        : Flash ÅäÖÃ¶ÁĞ´Ä£¿é
*********************************************************************************
* Copyright (c) 2021 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for 
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/


#include "debug.h"

/* È«¾Öºê¶¨Òå */

#include "bsp_flash.h"
#include "HTTPS.h"

/* È«¾Ö±äÁ¿ */
volatile FLASH_Status FLASHStatus = FLASH_COMPLETE;
 

Web_Page_State_t Web_Page_State[4] = {0};
/* ¶¨ÒåÈı¸ö½á¹¹ÌåÊı×é, ·Ö±ğ±£´æ»ù±¾ÉèÖÃ¡¢¶Ë¿ÚÉèÖÃ¡¢µÇÂ¼ÉèÖÃ
 * TODO: Êı×é³¤¶È 4/4/2 ÎªÓ²±àÂë, ½¨Òé¸ÄÎªºêÍ³Ò»¹ÜÀí */
Parameter Para_Basic[4], Para_Port[4], Para_Login[2];

Basic_Cfg_t Basic_CfgBuf;
Port_Cfg_t  Port_CfgBuf;
Login_Cfg_t Login_CfgBuf;

/* WCHNET ÍøÂç²ÎÊıÄ¬ÈÏÅäÖÃ
 * ¸ñÊ½: [0~1]Ğ£ÑéÂë 0x57AB, [2~7]MAC, [8~11]IP, [12~15]ÑÚÂë, [16~19]Íø¹Ø */
const u8 Basic_Default[BASIC_CFG_LEN] = {
0x57, 0xAB,
01, 02, 03, 04, 05, 06, 192, 168, 1, 250, 255, 255, 255, 0, 192, 168, 1, 1};

/* WCHNET µÇÂ¼Ä¬ÈÏ²ÎÊı: ÓÃ»§Ãû admin, ÃÜÂë 123
 * ¸ñÊ½: [0~1]Ğ£ÑéÂë 0x57AB, [2~12]ÓÃ»§Ãû(11×Ö½Ú), [13~23]ÃÜÂë(11×Ö½Ú)
 * TODO: Éú²ú»·¾³Ó¦ĞŞ¸ÄÄ¬ÈÏÃÜÂë */
const u8 Login_Default[LOGIN_CFG_LEN] = {
0x57, 0xAB,
'a', 'd', 'm', 'i', 'n', 0, 0, 0, 0, 0, 0, '1', '2', '3', 0, 0, 0, 0, 0, 0, 0, 0 };

/* WCHNET ¶Ë¿ÚÄ¬ÈÏÅäÖÃ: TCP¿Í»§¶ËÄ£Ê½
 * ¸ñÊ½: [0~1]Ğ£ÑéÂë, [2]Ä£Ê½, [3~4]Ô´¶Ë¿Ú(´ó¶Ë), [5~8]Ä¿±êIP, [9~10]Ä¿±ê¶Ë¿Ú(´ó¶Ë) */
const u8 Port_Default[PORT_CFG_LEN] = {
0x57, 0xAB,
MODE_TCPCLIENT, 1000 / 256, 1000 % 256, 192, 168, 1, 100, 1000 / 256, 1000 % 256 };

/*********************************************************************
 * @fn      BSP_FLASH_RestoreDefaults
 *
 * @brief   »Ö¸´ÍøÂçÅäÖÃÎª³ö³§Ä¬ÈÏÖµ, ÖğÏîĞ´Èë Flash ºóÈí¼ş¸´Î».
 *
 * @return  ÎŞ (±¾º¯Êı²»·µ»Ø, ×îÖÕµ÷ÓÃ NVIC_SystemReset)
 *
 * @note    Ê¹ÓÃ BSP_FLASH_WriteConfig (²Á³ı¡úĞ´Èë¡úĞ£Ñé), Ã¿ÏîÅäÖÃ¶ÀÕ¼ 256B ¿ìËÙÒ³.
 *          ä»»ä¸€é˜¶æ®µå¤±è´¥ä¼šæ‰“å°è¯¦ï¿½ï¿½ä¸”æœ€ç»ˆä»å¤ä½, ç¡®ä¿ä¸æ®‹ç•™åŠå†™æ•°æ®.
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
    NVIC_SystemReset();  /* Èí¼ş¸´Î»Ê¹ĞÂÅäÖÃÉúĞ§ */
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
 * @brief   ²Á³ı Data-Flash ÉÈÇø, ×îĞ¡²Á³ıµ¥Î»Îª 4K ×Ö½Ú(FLASH_ErasePage).
 *          ÈçĞè 256B ¾«Ï¸²Á³ı, Ê¹ÓÃ BSP_FLASH_ERASE_Fast.
 *
 * @param   Page_Address - ÆğÊ¼Ò³µØÖ· (Ğè 4K ¶ÔÆë)
 *          Length       - ²Á³ı³¤¶È(×Ö½Ú, ½¨ÒéÎª FLASH_PAGE_SIZE ÕûÊı±¶)
 *
 * @return  ÎŞ
 *
 * @note    Length Èô²»ÊÇ FLASH_PAGE_SIZE(4KB) ÕûÊı±¶ÔòÎ²²¿²»×ãÒ»Ò³²¿·Ö²»»á±»²Á³ı.
 *          Ñ­»·ÖĞÈôÄ³Ò»Ò³²Á³ıÊ§°Ü, »á Lock Flash ºóÌáÇ°·µ»Ø, ºóĞøÒ³Ãæ²»ÔÙ²Á³ı.
 */
void BSP_FLASH_ERASE(uint32_t Page_Address, u32 Length) {
    u32 NbrOfPage, EraseCounter;

    FLASH_Unlock();
    /* TODO: Length ·ÇÒ³¶ÔÆëÊ±Î²²¿±»½Ø¶Ï, ½¨Òé¸ÄÎªÏòÉÏÈ¡Õû:
     *       NbrOfPage = (Length + FLASH_PAGE_SIZE - 1) / FLASH_PAGE_SIZE; */
    NbrOfPage = Length / FLASH_PAGE_SIZE;
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);
    for (EraseCounter = 0; EraseCounter < NbrOfPage; EraseCounter++) {
        /* BUGFIX: Ê¹ÓÃ¾Ö²¿±äÁ¿¶ø·ÇÈ«¾Ö FLASHStatus, ±ÜÃâ±»ÆäËû²Ù×÷¸²¸Ç */
        FLASH_Status status = FLASH_ErasePage(Page_Address + (FLASH_PAGE_SIZE * EraseCounter));
        if (status != FLASH_COMPLETE) {
            printf("FLASH Erase Fail at 0x%08lX\r\n", Page_Address + (FLASH_PAGE_SIZE * EraseCounter));
            FLASH_Lock();  /* BUGFIX: ÌáÇ°·µ»ØÇ°±ØĞë Lock, ·ñÔò Flash ±£³Ö½âËø */
            return;
        }
    }
    FLASH_Lock();
}

/*********************************************************************
 * @fn      BSP_FLASH_ERASE_Fast
 *
 * @brief   °´ 256 ×Ö½ÚÒ³¿ìËÙ²Á³ı Data-Flash.
 *          µ×²ãµ÷ÓÃ FLASH_ErasePage_Fast, ĞèÒª FLASH_Unlock_Fast ½øÈë¿ìËÙÄ£Ê½.
 *
 * @param   Page_Address - ÆğÊ¼Ò³µØÖ· (Ğë 256 ×Ö½Ú¶ÔÆë, ÄÚ²¿×Ô¶¯ & 0xFFFFFF00 ¶ÔÆë)
 *          Length       - ²Á³ı³¤¶È(×Ö½Ú, ĞèÎª 256 µÄÕûÊı±¶)
 *
 * @return  FLASH_Status - FLASH_COMPLETE ±íÊ¾³É¹¦; FLASH_TIMEOUT ±íÊ¾µÈ´ıÃ¦³¬Ê±
 *
 * @note    FLASH_ErasePage_Fast ÎŞ·µ»ØÖµ, Í¨¹ıµÈ´ı BSY ±êÖ¾Î»ÅĞ¶ÏÍê³É. Èô³¬Ê±·µ»Ø
 *          FLASH_TIMEOUT. Ïà±È±ê×¼²Á³ı(4KB/22ms), ¿ìËÙ²Á³ı(256B/1.4ms)¸ü¾«Ï¸,
 *          ÊÊºÏÖ»²Á³ıĞ¡·¶Î§ÅäÖÃÇø. ×¢Òâ: FLASH_ErasePage_Fast ²Á³ıºó×Ô¶¯ÍË³ö BSY,
 *          Ğ´ÈëÇ°ÈÔĞè FLASH_Unlock (±ê×¼½âËø) ¶ø·Ç FLASH_Unlock_Fast.
 */
FLASH_Status BSP_FLASH_ERASE_Fast(uint32_t Page_Address, u32 Length) {
    u32 NbrOfPage, EraseCounter;
    u32 timeout;

    /* ÏòÉÏÈ¡ÕûÈ·±£¸²¸ÇËùÓĞÊı¾İ */
    NbrOfPage = (Length + 255) / 256;

    FLASH_Unlock_Fast();  /* ½øÈë¿ìËÙ²Á³ıÄ£Ê½ */
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_WRPRTERR);

    for (EraseCounter = 0; EraseCounter < NbrOfPage; EraseCounter++) {
        FLASH_ErasePage_Fast(Page_Address + (256 * EraseCounter));

        /* FLASH_ErasePage_Fast ÎŞ·µ»ØÖµ, ÊÖ¶¯µÈ´ı BSY Î»Çå³ı */
        timeout = 0x100000;
        while (FLASH->STATR & (1 << 0)) {  /* SR_BSY */
            if (--timeout == 0) {
                printf("FLASH Erase Fast Timeout at 0x%08lX\r\n",
                       Page_Address + (256 * EraseCounter));
                FLASH_Lock_Fast();  /* ³¬Ê±ÍË³ö¿ìËÙÄ£Ê½ */
                return FLASH_TIMEOUT;
            }
        }
    }

    FLASH_Lock_Fast();  /* ÍË³ö¿ìËÙ²Á³ıÄ£Ê½ */
    return FLASH_COMPLETE;
}

/*********************************************************************
 * @fn      BSP_FLASH_WriteConfig
 *
 * @brief   ÅäÖÃÊı¾İĞ´Èë Flash Ô­×Ó²Ù×÷: ²Á³ı(256B) ¡ú Ğ´Èë(Êı¾İ+CRC) ¡ú CRCĞ£Ñé.
 *          Êı¾İ²¼¾Ö: [0..Length-1] ÓÃ»§Êı¾İ, [Length..251] 0xFFÌî³ä,
 *                    [252..255] CRC32Ğ£ÑéÖµ (Ó²¼şCRC, RecalculateCRC).
 *
 * @param   Addr   - Flash Ä¿±êµØÖ· (Ğè 256B ¶ÔÆë)
 *          Buffer - Êı¾İÔ´»º³åÇø
 *          Length - Êı¾İ³¤¶È(×Ö½Ú, Èô²»×ã4µÄ±¶ÊıÔòÎ²²¿×Ô¶¯²¹0¶ÔÆë)
 *
 * @return  FLASH_COMPLETE  È«²¿³É¹¦
 *          ÆäËûÖµ          Ê§°Ü (²Á³ı³¬Ê±/Ğ´Èë´íÎó/CRC²»Æ¥Åä)
 *
 * @note    Ã¿´ÎĞ´ÈëÍêÕû 256B Ò³: ÓÃ»§Êı¾İ + Ìî³ä + CRC32.
 *          »Ø¶ÁĞ£ÑéÊ±ÖØËã CRC ²¢ÓëÎ²²¿´æ´¢Öµ±È¶Ô, µ¥´Î±È½Ï¼´¿ÉÑéÖ¤ÍêÕûĞÔ.
 */
FLASH_Status BSP_FLASH_WriteConfig(u32 Addr, const u8 *Buffer, u32 Length)
{
    FLASH_Status status;
    u32 crc_calc, crc_stored;
    u32 data_words, i;
    /* Ğ´»º³åÇø: [0..data_words-1]=ÓÃ»§Êı¾İ, [63]=CRC32, ÆäÓà 0xFFFFFFFF */
    u32 write_buf[FLASH_PAGE_SIZE_FAST / 4];

    /* Êı¾İÏòÉÏÈ¡Õûµ½ u32 ±ß½ç, ²»×ãÒ»×ÖµÄ²¿·Ö²¹ 0 ºó²ÎÓë CRC */
    data_words = (Length + 3) / 4;

    /* ³õÊ¼»¯Õû¸ö»º³åÇøÎª 0xFFFFFFFF (Flash ²Á³ıÌ¬, ¼õÉÙ±à³ÌÄ¥Ëğ) */
    for (i = 0; i < FLASH_PAGE_SIZE_FAST / 4; i++) {
        write_buf[i] = 0xFFFFFFFF;
    }
    /* ¸´ÖÆÓÃ»§Êı¾İ (°´×Ö¿½±´, ²»×ãÒ»×ÖµÄ²¿·Ö×Ô¶¯ÓÉ 0xFFFFFFFF ²¹Î» ¡ú ĞèĞŞÕı) */
    for (i = 0; i < Length; i++) {
        ((u8 *)write_buf)[i] = Buffer[i];
    }
    /* Î²²¿²»×ãÒ»×ÖµÄ×Ö½ÚÇåÁã (±ÜÃâ 0xFF ÎÛÈ¾ CRC) */
    for (i = Length; i < data_words * 4; i++) {
        ((u8 *)write_buf)[i] = 0x00;
    }

    /* ½×¶Î0: ¼ÆËãÊı¾İÇø CRC32 (¸²¸Ç Length ÏòÉÏ¶ÔÆëºóµÄ data_words ¸ö×Ö) */
    crc_calc = RecalculateCRC(write_buf, data_words);
    /* CRC ´æÈë 256B Ò³Î²²¿×îºóÒ»¸ö u32 */
    write_buf[(FLASH_PAGE_SIZE_FAST / 4) - 1] = crc_calc;

    /* ½×¶Î1: 256B ¿ìËÙÒ³²Á³ı */
    status = BSP_FLASH_ERASE_Fast(Addr, FLASH_PAGE_SIZE_FAST);
    if (status != FLASH_COMPLETE) {
        BSP_DEBUG("WriteCfg Erase Fail at 0x%08lX: %d\r\n", Addr, status);
        return status;
    }

    /* ½×¶Î2: Ğ´ÈëÍêÕû 256B Ò³ (Êı¾İ + Ìî³ä + CRC) */
    status = BSP_FLASH_WRITE_Word(Addr, (u8 *)write_buf, FLASH_PAGE_SIZE_FAST);
    if (status != FLASH_COMPLETE) {
        BSP_DEBUG("WriteCfg Write Fail at 0x%08lX: %d\r\n", Addr, status);
        return status;
    }

    /* ½×¶Î3: CRC Ğ£Ñé ¡ª »Ø¶ÁÊı¾İÇø, ÖØËã CRC ÓëÎ²²¿´æ´¢Öµ±È¶Ô */
    BSP_FLASH_READ(Addr, (u8 *)write_buf, FLASH_PAGE_SIZE_FAST);
    crc_calc = RecalculateCRC(write_buf, data_words);
    crc_stored = write_buf[(FLASH_PAGE_SIZE_FAST / 4) - 1];

    if (crc_calc != crc_stored) {
        BSP_DEBUG("WriteCfg CRC Mismatch at 0x%08lX: Calc=%08lX Stored=%08lX\r\n",
                  Addr, crc_calc, crc_stored);
        return (FLASH_Status)(-1);  /* CRC Ğ£ÑéÊ§°Ü */
    }

    return FLASH_COMPLETE;
}

/*********************************************************************
 * @fn      BSP_FLASH_WRITE_Word
 *
 * @brief   °´×Ö(4×Ö½Ú)Ğ´Èë Data-Flash.
 *          µ×²ãµ÷ÓÃ FLASH_ProgramWord, ¸Ãº¯ÊıÄÚ²¿²ğÎªÁ½´Î°ë×ÖĞ´Èë
 *          (ÏÈĞ´µÍ16Î»µ½ Address, ÔÙĞ´¸ß16Î»µ½ Address+2).
 *
 * @param   StartAddr - Ğ´ÈëÆğÊ¼µØÖ· (Ğè 4 ×Ö½Ú¶ÔÆë)
 *          Buffer    - Êı¾İ»º³åÇøÖ¸Õë (Ğè 4 ×Ö½Ú¶ÔÆë)
 *          Length    - Êı¾İ³¤¶È(×Ö½Ú, ĞèÎª 4 µÄÕûÊı±¶)
 *
 * @return  FLASH_Status - FLASH_COMPLETE ±íÊ¾³É¹¦, ÆäËûÖµ±íÊ¾Ê§°Ü
 *
 * @note    ¶ÔÆëÒªÇó: Cortex-M3 ÄÚºË, u8* ¡ú u32* ·Ç¶ÔÆë¿ÉÄÜ´¥·¢ UsageFault.
 *          Length ·Ç 4 µÄ±¶ÊıÊ±Î²²¿²»×ãÒ»×ÖµÄÊı¾İ»á±»¶ªÆú.
 */
FLASH_Status BSP_FLASH_WRITE_Word(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u32 *p_buff = (u32 *) Buffer;  // ÒªÇó Buffer 4 ×Ö½Ú¶ÔÆë
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
 * @brief   °´°ë×Ö(2×Ö½Ú)Ğ´Èë Data-Flash.
 *          µ×²ãµ÷ÓÃ FLASH_ProgramHalfWord.
 *
 * @param   StartAddr - Ğ´ÈëÆğÊ¼µØÖ· (Ğè 2 ×Ö½Ú¶ÔÆë)
 *          Buffer    - Êı¾İ»º³åÇøÖ¸Õë (Ğè 2 ×Ö½Ú¶ÔÆë)
 *          Length    - Êı¾İ³¤¶È(×Ö½Ú, ĞèÎª 2 µÄÕûÊı±¶)
 *
 * @return  FLASH_Status - FLASH_COMPLETE ±íÊ¾³É¹¦, ÆäËûÖµ±íÊ¾Ê§°Ü
 */
FLASH_Status BSP_FLASH_WRITE_HalfWord(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u16 *p_buff = (u16 *) Buffer;  // ÒªÇó Buffer 2 ×Ö½Ú¶ÔÆë
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
 * @brief   ¡¾ÑÏÖØBug¡¿°´×Ö½ÚĞ´ÈëÑ¡Ïî×Ö½ÚÇø (0x1FFFF800), ·ÇÊı¾İ Flash!
 *          µ×²ãµ÷ÓÃ FLASH_ProgramOptionByteData, ¸Ã SDK º¯Êı¹Ì¶¨²Ù×÷Ñ¡Ïî×Ö½ÚÇøÓò,
 *          Address ²ÎÊıÎªÑ¡Ïî×Ö½ÚÄÚµÄÆ«ÒÆ, ¶ø·ÇÊı¾İ Flash µØÖ·.
 *
 * @param   StartAddr - Ñ¡Ïî×Ö½ÚÇøÆ«ÒÆµØÖ·
 *          Buffer    - Êı¾İ»º³åÇøÖ¸Õë
 *          Length    - Êı¾İ³¤¶È(×Ö½Ú)
 *
 * @return  FLASH_Status - FLASH_COMPLETE ±íÊ¾³É¹¦, ÆäËûÖµ±íÊ¾Ê§°Ü
 *
 * @warning ±¾º¯Êı²Ù×÷µÄÊÇÑ¡Ïî×Ö½ÚÇø (Option Bytes), ²»ÊÇ³£¹æ Flash Êı¾İÇø.
 *          ÈôĞè°´×Ö½ÚĞ´Êı¾İ Flash, Çë¸ÄÓÃ BSP_FLASH_WRITE_Word ²¢×ÔĞĞ´¦Àí
 *          ¶Á-¸Ä-Ğ´Á÷³Ì (Flash ²»Ö§³Ö×Ö½Ú¼¶Ğ´Èë).
 */
FLASH_Status BSP_FLASH_WRITE_ByteData(u32 StartAddr, const u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u8 *p_buff = (u8 *) Buffer;
    FLASH_Status FLASHStatus = FLASH_COMPLETE;

    FLASH_Unlock();
    while ((address < (StartAddr + Length)) && (FLASHStatus == FLASH_COMPLETE))
    {
        /* ×¢Òâ: FLASH_ProgramOptionByteData ²Ù×÷µÄÊÇÑ¡Ïî×Ö½ÚÇø, ·ÇÊı¾İ Flash */
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
 * @brief   °´×Ö(4×Ö½Ú)¶ÁÈ¡ Data-Flash (Flash ÊÇÄÚ´æÓ³ÉäµÄ, Ö±½Ó½âÒıÓÃ¼´¿É).
 *
 * @param   StartAddr - ¶ÁÈ¡ÆğÊ¼µØÖ·
 *          Buffer    - Êı¾İ»º³åÇøÖ¸Õë (Ğè 4 ×Ö½Ú¶ÔÆë)
 *          Length    - Êı¾İ³¤¶È(×Ö½Ú, ĞèÎª 4 µÄÕûÊı±¶)
 *
 * @return  ÎŞ
 *
 * @note    Cortex-M3 ¿ÉÖ±½Ó¶ÁÈ¡ Flash Ó³ÉäµØÖ·, ÎŞĞè½âËø.
 *          Length ·Ç 4 µÄ±¶ÊıÊ±Î²²¿²»×ãÒ»×ÖµÄÊı¾İ²»»á±»¶ÁÈ¡.
 */
void BSP_FLASH_READ(u32 StartAddr, u8 *Buffer, u32 Length) {
    u32 address = StartAddr;
    u32 *p_buff = (u32 *) Buffer;  // ÒªÇó Buffer 4 ×Ö½Ú¶ÔÆë

    while (address < (StartAddr + Length))
    {
        *p_buff = (*(u32 *)address);  // Ö±½Ó½âÒıÓÃ Flash Ó³ÉäµØÖ·
        address += 4;
        p_buff++;
    }
}

/*********************************************************************/
 
