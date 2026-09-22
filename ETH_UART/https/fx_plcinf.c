/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_plcinf.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP PLC信息页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_plcinf.h"
#include "HTTPS.h"
#include "html_components.h"
#include "index.h"

#include "bsp_rtc.h"
#include "ethernet_app.h"
#include "melsec_fx_tables.h"   /* MC_FX_M：M 软元件编码 */
#include "melsec_fx_core.h"     /* MELSEC_FX_BuildE00ReadCmd：字单位成批读出 */
#include "melsec_fx_net.h"      /* net_mc_meta：MC 协议请求上下文 */
/* 全局变量定义 */
#define FX_PLCINF_TABLE_ROWS  11
static fx_plcinf_info_t g_plc_info;
static fx_plcinf_error_t g_errors[FX_PLCINF_MAX_ERRORS];
 
/* 静态函数声明 */
static char* FX_PLCINF_GetCPUTypeString(fx_plcinf_cpu_type_t cpu_type);
static char* FX_PLCINF_GetMemTypeString(fx_plcinf_mem_type_t mem_type);
static char* FX_PLCINF_GenerateErrorTable(void);

/*********************************************************************
 * @fn      FX_PLCINF_Init
 *
 * @brief   初始化PLC信息模块
 *
 * @param   无
 *
 * @return  无
 */
void FX_PLCINF_Init(void)
{
    uint8_t i;
    
    /* 初始化PLC信息 - 示例值 */
    g_plc_info.cpu_type = FX_PLC_CPU_FX3U;
    g_plc_info.cpu_version = 0x0315;  /* 版本3.15 */
    g_plc_info.mem_type = FX_PLC_MEM_RAM;
    g_plc_info.battery_mode = 0;
    
    /* 设置LED状态 */
    g_plc_info.led_power = FX_PLC_LED_GREEN;   /* POWER灯常亮(绿色) */
    g_plc_info.led_run = FX_PLC_LED_OFF;       /* RUN灯关闭 */
    g_plc_info.led_batt = FX_PLC_LED_OFF;      /* BATT灯关闭 */
    g_plc_info.led_error = FX_PLC_LED_OFF;     /* ERROR灯关闭 */
    
    /* 初始化错误信息 */
    memset(g_errors, 0, sizeof(g_errors));
    for (i = 0; i < FX_PLCINF_MAX_ERRORS; i++) {
        g_errors[i].error_no = i + 1;
        g_errors[i].error_step = 0;
        g_errors[i].error_msg[0] = '\0';
    }
    
    /* 初始化HTML缓冲区 */
    
    HTTPS_DEBUG("PLC信息模块初始化完成\r\n");
}

/*********************************************************************
 * @fn      FX_PLCINF_GetCPUTypeString
 *
 * @brief   获取CPU类型字符串
 *
 * @param   cpu_type - CPU类型
 *
 * @return  CPU类型字符串
 */
static char* FX_PLCINF_GetCPUTypeString(fx_plcinf_cpu_type_t cpu_type)
{
    switch (cpu_type) {
        case FX_PLC_CPU_FX3U:
            return "FX3U/FX3UC";
        case FX_PLC_CPU_FX3UC:
            return "FX3U/FX3UC";
        default:
            return "UNKNOWN";
    }
}

/*********************************************************************
 * @fn      FX_PLCINF_GetMemTypeString
 *
 * @brief   获取存储器类型字符串
 *
 * @param   mem_type - 存储器类型
 *
 * @return  存储器类型字符串
 */
static char* FX_PLCINF_GetMemTypeString(fx_plcinf_mem_type_t mem_type)
{
    switch (mem_type) {
        case FX_PLC_MEM_RAM:
            return "RAM";
        case FX_PLC_MEM_EEPROM:
            return "EEPROM";
        case FX_PLC_MEM_FLASH:
            return "FLASH";
        default:
            return "UNKNOWN";
    }
}

/* ══════════════════════════════════════════════════════════════════
 * 错误类别表：下标 0~7 = D8060~D8067（主错误块），8~11 = D8438/D8487/D8449/D8489（扩展错误块）
 * 每类给出三项：
 *   g_err_cls_name[] —— 类别名（查表未命中时作为错误内容回退显示）
 *   g_err_cls_m[]    —— 动作的特殊继电器 M（页面"No."列直接显示，便于对照手册 38.3.2）
 *   g_err_cls_fix[]  —— 类级"解决方法"（手册 38.4 各表的解决方法栏对同一类是同一条，
 *                       按类别给出即可，省下"每个错误码一份解决方法"的大量 Flash）
 * ══════════════════════════════════════════════════════════════════ */
#define FX_PLCINF_ERR_CLASSES_ALL   12u
static const char *const g_err_cls_name[FX_PLCINF_ERR_CLASSES_ALL] = {
    "I/O构成错误", "PLC硬件错误", "串行通信错误0", "串行通信错误1",
    "参数错误",    "语法错误",    "回路错误",     "运算错误",
    "串行通信错误2", "USB通信错误", "特殊模块/单元错误", "特殊参数错误"
};
static const uint16_t g_err_cls_m[FX_PLCINF_ERR_CLASSES_ALL] = {
    8060u, 8061u, 8062u, 8063u, 8064u, 8065u, 8066u, 8067u,
    8438u, 8487u, 8449u, 8489u
};
static const char *const g_err_cls_fix[FX_PLCINF_ERR_CLASSES_ALL] = {
    "确认未安装I/O的编号后修改程序",
    "检查扩展电缆与单元连接",
    "确认通信参数、电缆接线与设定值",
    "确认通信参数、电缆接线与设定值",
    "停止PLC正确设定参数后重新上电",
    "检查各指令用法后在编程模式下修改",
    "检查回路块的指令组合与相互关系",
    "修改程序或检查应用指令的操作数",
    "确认通信参数与电缆接线",
    "检查编程用连接器与电缆连接",
    "确认扩展电缆与特殊模块的安装",
    "排除故障后重设特殊参数并断电重启"
};

/*********************************************************************
 * @fn      FX_PLCINF_GenerateErrorTable
 *
 * @brief   生成错误信息表格HTML
 *
 * @param   无
 *
 * @return  表格HTML字符串
 */
static char* FX_PLCINF_GenerateErrorTable(void)
{
    char *table_buffer = MITSU_HTTP_GetTableBuffer();
    char *temp_buffer = HtmlBuffer;
    uint8_t i;
    uint8_t has_error = 0;
    
    MITSU_HTTP_ClearTableBuffer();
    
    /* 表格头部：No. 列改放"动作的特殊继电器"(如 M8065)，另加一列"解决方法"
     * （解决方法按错误类别给出，与手册 38.4 各表的"解决方法"栏对应） */
    MITSU_HTTP_Emit(
           "<table border=\"1\" cellspacing=\"1\" style=\"margin:0 auto;\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td>\r\n"
           "<table border=\"1\" cellspacing=\"0\" bgcolor=\"#ffffff\" style=\"table-layout:fixed;text-align:center;font-size:14px\">\r\n"
           "<tbody>\r\n"
           "<tr bgcolor=\"#cccccc\">\r\n"
           "<td width=\"70\">No.</td>\r\n"
           "<td width=\"100\">错误步</td>\r\n"
           "<td width=\"400\">当前错误</td>\r\n"
           "<td width=\"330\">解决方法</td>\r\n"
           "</tr>\r\n");
    
    /* 生成错误行 */
    for (i = 0; i < FX_PLCINF_MAX_ERRORS; i++) {
        if (strlen(g_errors[i].error_msg) > 0) {
            char    no_buf[12];
            uint8_t cls = g_errors[i].cls;

            has_error = 1;
            /* No. 列：有类别信息时显示"动作的特殊继电器 M"（M8060~M8067/M8438/M8449/M8487/M8489），
             * 否则退化为原序号 */
            if (cls < (uint8_t)FX_PLCINF_ERR_CLASSES_ALL) {
                sprintf(no_buf, "M%u", (unsigned)g_err_cls_m[cls]);
            } else {
                sprintf(no_buf, "%u", (unsigned)g_errors[i].error_no);
            }
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%u</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "</tr>\r\n",
                    no_buf,
                    (unsigned)g_errors[i].error_step,
                    g_errors[i].error_msg,
                    (cls < (uint8_t)FX_PLCINF_ERR_CLASSES_ALL) ? g_err_cls_fix[cls] : "");
            MITSU_HTTP_Emit( temp_buffer);
        }
    }
    
    /* 如果没有错误,显示无错误 */
    if (!has_error) {
        MITSU_HTTP_Emit(
               "<tr>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>无错误</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "</tr>\r\n");
    }
    
    /* 填充剩余空行 */
    for (i = 1; i < FX_PLCINF_TABLE_ROWS; i++) {
        MITSU_HTTP_Emit(
               "<tr>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "<td>&nbsp;</td>\r\n"
               "</tr>\r\n");
    }
    
    /* 表格尾部 */
    MITSU_HTTP_Emit(
           "</tbody>\r\n"
           "</table>\r\n"
           "</td>\r\n"
           "</tr>\r\n"
           "</tbody>\r\n"
           "</table>\r\n");
    
    /* 表格已按阈值分批发出，冲刷尾部残留 */
    MITSU_HTTP_FlushTable();

    return table_buffer;
}

 

/*********************************************************************
 * @fn      FX_PLCINF_GetError
 *
 * @brief   获取指定错误信息
 *
 * @param   index - 错误索引(0-9)
 *          error - 错误信息结构体指针
 *
 * @return  无
 */
void FX_PLCINF_GetError(uint8_t index, fx_plcinf_error_t *error)
{
    if (index < FX_PLCINF_MAX_ERRORS && error != NULL) {
        memcpy(error, &g_errors[index], sizeof(fx_plcinf_error_t));
    }
}

 /* LED 状态 -> CSS 类名（原厂配色: 绿=ledg, 红=ledr, 灭=off） */
static const char* FX_PLCINF_LEDClass(fx_plcinf_led_state_t st)
{
    if (st == FX_PLC_LED_GREEN) { return "ledg"; }
    if (st == FX_PLC_LED_RED)   { return "ledr"; }
    return "off";
}

/* ══════════════════════════════════════════════════════════════════
 * PLC 状态位(M8000~M8015 一个字)驱动 LED
 *
 * 位序（三菱约定，与 ethernet_app.c 的 wizchip_Analysis_M_info 一致）：
 *   状态字的 bit j = M(8000 + j)
 *     M8000 -> RUN   灯：1=点亮(绿)，0=熄灭
 *     M8004 -> ERROR 灯：1=点亮(红，错误发生)
 *     M8005 -> BATT  灯：1=点亮(红，电池电压低)
 *     POWER 灯固定常亮(绿)，不依赖 PLC 状态
 *
 * 取数方式：M 是位软元件，按"字单位"读 1 点 = 16 位(正好覆盖 M8000~M8015)，
 *           与软元件监视页(fx_devmon)读 M 的方式完全一致。
 * 刷新时机：页面渲染时(监视执行中)发起一次读；UART 回帧到达后由
 *           HTTPS.c 的 Web_Usart_Handler(HTML_PAGE_PLCINF 分支)调用
 *           FX_PLCINF_OnStatusReply() 更新 LED，浏览器每 5s 元刷新即看到新状态。
 * ══════════════════════════════════════════════════════════════════ */
#define FX_PLCINF_STATUS_ADDR     8000u   /* M8000 */
#define FX_PLCINF_STATUS_POINTS   1u      /* 1 个字 = M8000~M8015 */
#define FX_PLCINF_BIT_RUN         0u      /* M8000 */
#define FX_PLCINF_BIT_ERROR       4u      /* M8004 */
#define FX_PLCINF_BIT_BATT        5u      /* M8005 */

static uint16_t g_plc_status       = 0;   /* 最近一次读到的状态字 */
static uint8_t  g_plc_status_valid = 0;   /* 是否成功读到过 */
static uint8_t  g_plc_req_busy     = 0;   /* 有一次读在途：避免请求堆积 */

/* ══════════════════════════════════════════════════════════════════
 * 错误信息：检测错误用的特殊软元件（《FX3U 编程手册[基本·应用]》38.3 节）
 *
 * 每一"类"错误 = 一个特殊继电器(动作标志) + 一个特殊数据寄存器(错误代码)：
 *     M8060 -> D8060   I/O构成错误（代码是"未安装的 I/O 起始编号"的 BCD 值）
 *     M8061 -> D8061   PLC硬件错误        (6101 ~ 6115)
 *     M8062 -> D8062   串行通信错误0      (62xx)
 *     M8063 -> D8063   串行通信错误1      (63xx)
 *     M8438 -> D8438   串行通信错误2      (38xx)   ← 扩展块，本页暂未读取
 *     M8064 -> D8064   参数错误           (6401 ~ 6421)
 *     M8065 -> D8065   语法错误           (6501 ~ 6510)
 *     M8066 -> D8066   回路错误           (6610 ~ 6632)
 *     M8067 -> D8067   运算错误           (6701 ~ 6773)
 *     M8449 -> D8449   特殊模块/单元错误  ← 扩展块，本页暂未读取
 *     M8487 -> D8487   USB通信错误        ← 扩展块，本页暂未读取
 *     M8489 -> D8489   特殊参数错误       ← 扩展块，本页暂未读取
 *     M8068   运算错误锁存(无对应代码) ;  M8316 I/O未安装的指定错误 ;
 *     M8318   BFM 的初始化失败(无对应代码)
 * 其它相关软元件：
 *     M8004   错误发生时 ON（本页 RUN/BATT/ERROR 三个灯里的 ERROR 就用它同一体系的 M8004）
 *     D8004   保存"发生错误的特 M 的小编号"（例：D8004=8061 → M8061 动作 → PLC硬件错误）
 *     D8069   发生的步编号（32K 步以下；D8068 可确认该编号）
 *     D8312/D8313  初次发生的步编号      D8314/D8315  发生的步编号(32K步)
 *     D8318   发生错误的单元号           D8319        发生错误的 BFM 编号
 *
 * 本页数据来源（页面只读缓存，自己不发任何读命令）：
 *   ① 上电同步(同步状态机 READ_EE 的 case 2)一次性读 D8000+127 点，
 *      其中 D8001(版本)/D8002(容量)/D8003(存储器) 落 g_plc_cache；
 *   ② 运行期由同步状态机 MainTask 每 3 秒轮询三条只读命令(见 SyncStateMachine_PLC 的
 *      case 17/18/19)：M8000(1 点) → LED 状态字、D8060(10 点) → 主错误块、
 *      D8438(52 点) → 扩展错误块；回帧由 sim_Process_local_machine() 分流到本文件的
 *      FX_PLCINF_OnStatusReply / OnErrorReply / OnErrorReplyExt。
 *   代码(D8060~D8067)与"发生的步编号"(D8069)并用即可还原手册 38.4 的错误内容与解决方法。
 * ══════════════════════════════════════════════════════════════════ */
#define FX_PLCINF_ERR_MASK_START   8060u   /* 运行期轮询起始：D8060（由同步机 MainTask 每 3s 发出） */
#define FX_PLCINF_ERR_MASK_POINTS  10u     /* 10 点 = D8060 ~ D8069（20 字节，省串口） */
#define FX_PLCINF_ERR_CLASSES      8u      /* D8060 ~ D8067 共 8 类 */
/* 各寄存器在读取块里的下标（D 寄存器号 - 8060） */
#define FX_PLCINF_ERR_IDX_CODE0    (8060u - FX_PLCINF_ERR_MASK_START)   /* = 0 */
#define FX_PLCINF_ERR_IDX_STEP     (8069u - FX_PLCINF_ERR_MASK_START)   /* = 9 发生的步编号 */

/* 扩展错误块：一条命令读 D8438~D8489（52 点），覆盖
 *   D8438 串行通信错误2 / D8449 特殊模块·单元错误 / D8487 USB通信错误 / D8489 特殊参数错误 */
#define FX_PLCINF_ERR_EXT_START   8438u
#define FX_PLCINF_ERR_EXT_POINTS  52u
#define FX_PLCINF_ERR_EXT_IDX_4438 (8449u - FX_PLCINF_ERR_EXT_START)   /* D8449 */
#define FX_PLCINF_ERR_EXT_IDX_4487 (8487u - FX_PLCINF_ERR_EXT_START)   /* D8487 */
#define FX_PLCINF_ERR_EXT_IDX_4489 (8489u - FX_PLCINF_ERR_EXT_START)   /* D8489 */

static uint8_t  g_plc_err_busy = 0;        /* 主错误块读在途标志 */
static uint8_t  g_plc_err_ext_busy = 0;    /* 扩展错误块读在途标志 */
static uint8_t  g_plc_err_row_cnt = 0;     /* 主块已填的行数(扩展块从此处接着填) */
static uint16_t g_plc_err_step = 0;        /* D8069：发生的步编号 */
static uint16_t g_plc_err_mnum = 0;        /* D8004：动作的特M小编号(如 8061) */

/*********************************************************************
 * @fn      FX_PLCINF_RequestStatus
 *
 * @brief   发起一次 M8000~M8015 读取（仅"监视执行中"；每次页面渲染调一次）
 *
 * @param   Sour_Sock - 请求来源 socket
 * @param   Dest_Sock - 目的 socket（回帧时按此 socket 的页面路由回来）
 *
 * @return  none
 */
void FX_PLCINF_RequestStatus(uint8_t Sour_Sock, uint8_t Dest_Sock)
{
    /* ★ 页面刷新时主动请求（网页实时性要求不高，有请求再更新即可）：
     *   ① M8000~M8015   1 点 -> RUN/BATT/ERROR 指示灯状态字
     *   ② D8060~D8069  10 点 -> 主错误块(8 类错误码 + 发生的步编号)
     *   ③ D8438~D8489  52 点 -> 扩展错误块(串行错误2/USB/特殊模块/特殊参数)
     * 仅在"监视执行中"发出；三个请求各有在途标志，避免堆叠。
     * 回帧由 HTTPS.c 的 HTML_PAGE_PLCINF 分支按 device_name + start_device 三路分流。 */
    if (net_monitor_state != FX_MONITOR_RUNNING) {
        g_plc_req_busy     = 0;                 /* 监视停止/链路断开：清在途标志 */
        g_plc_err_busy     = 0;
        g_plc_err_ext_busy = 0;
        return;
    }

    /* ① M8000~M8015：一个字，驱动 RUN / BATT / ERROR 指示灯 */
    if (!g_plc_req_busy) {
        net_mc_meta.device_name  = MC_FX_M;
        net_mc_meta.start_device = FX_PLCINF_STATUS_ADDR;
        net_mc_meta.device_count = FX_PLCINF_STATUS_POINTS;
        g_plc_req_busy = 1;
        MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                                  FX_PLCINF_STATUS_ADDR, FX_PLCINF_STATUS_POINTS);
    }

    /* ② D8060~D8069：主错误块 */
    if (!g_plc_err_busy) {
        net_mc_meta.device_name  = MC_FX_D;
        net_mc_meta.start_device = FX_PLCINF_ERR_MASK_START;
        net_mc_meta.device_count = FX_PLCINF_ERR_MASK_POINTS;
        g_plc_err_busy = 1;
        MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                                  FX_PLCINF_ERR_MASK_START, FX_PLCINF_ERR_MASK_POINTS);
    }

    /* ③ D8438~D8489：扩展错误块 */
    if (!g_plc_err_ext_busy) {
        net_mc_meta.device_name  = MC_FX_D;
        net_mc_meta.start_device = FX_PLCINF_ERR_EXT_START;
        net_mc_meta.device_count = FX_PLCINF_ERR_EXT_POINTS;
        g_plc_err_ext_busy = 1;
        MELSEC_FX_BuildE00ReadCmd(Sour_Sock, Dest_Sock,
                                  FX_PLCINF_ERR_EXT_START, FX_PLCINF_ERR_EXT_POINTS);
    }
}

/*********************************************************************
 * @fn      FX_PLCINF_OnStatusReply
 *
 * @brief   处理 M8000~M8015 回帧，刷新 LED（POWER 常亮，RUN/BATT/ERROR 由状态位驱动）
 *
 * @param   data - 已由 ASCII 十六进制解码为二进制的数据区(低位字在前)
 *          len  - 数据字节数
 *
 * @return  none
 */
void FX_PLCINF_OnStatusReply(const uint8_t *data, uint16_t len)
{
    uint16_t word;

    g_plc_req_busy = 0;                        /* 无论成败都解除在途标志 */
    if (data == NULL || len < 2u) {
        HTTPS_DEBUG("PLC信息页: 状态回帧过短(len=%u)\r\n", len);
        return;
    }

    word = (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
    g_plc_status       = word;
    g_plc_status_valid = 1;

    /* POWER 固定常亮；RUN 绿；ERROR / BATT 红 */
    g_plc_info.led_power = FX_PLC_LED_GREEN;
    g_plc_info.led_run   = (word & (1u << FX_PLCINF_BIT_RUN))   ? FX_PLC_LED_GREEN : FX_PLC_LED_OFF;
    g_plc_info.led_error = (word & (1u << FX_PLCINF_BIT_ERROR)) ? FX_PLC_LED_RED   : FX_PLC_LED_OFF;
    g_plc_info.led_batt  = (word & (1u << FX_PLCINF_BIT_BATT))  ? FX_PLC_LED_RED   : FX_PLC_LED_OFF;

    HTTPS_DEBUG("PLC信息页: 状态字=0x%04X  RUN=%d ERROR=%d BATT=%d\r\n", (unsigned)word,
           (g_plc_info.led_run   != FX_PLC_LED_OFF),
           (g_plc_info.led_error != FX_PLC_LED_OFF),
           (g_plc_info.led_batt  != FX_PLC_LED_OFF));
}

/* ══════════════════════════════════════════════════════════════════
 * 错误代码 -> 错误内容（查表映射）
 *
 * 设计要点：
 *  ① 固定表项 { 错误代码, 错误类别, 错误内容 }，全部放在 Flash（const），不占 RAM。
 *  ② 串行通信类(D8062/D8063，含扩展的 D8438)的错误码"低 2 位"含义相同
 *     （6201 / 6301 / 3801 都是"奇偶校验错误…"），所以只用一张"原因码表"
 *     同时覆盖 3 个类别，省掉 2/3 的表项 —— 比"每类一张表"更省 Flash 也更好维护。
 *  ③ I/O 构成错误(D8060)的代码是"未安装的 I/O 起始编号"BCD，含义是算出来的，
 *     因此不做表项，改为按 BCD 动态生成文本（见 FX_PLCINF_DecodeIOErr）。
 *  ④ 查不到的代码不做静默处理：回退显示"错误码 + 所属类别名"，
 *     用户仍能拿代码去手册 38.4 查表，不会出现空白行。
 * ══════════════════════════════════════════════════════════════════ */

/* 错误类别表：定义在 FX_PLCINF_GenerateErrorTable() 之前（见文件上部），
 * 这里只做说明 —— 下标 0~7 = D8060~D8067（主块），8~11 = D8438/D8487/D8449/D8489（扩展块）。 */

/* "错误代码 + 错误内容"结构体（固定查表数据） */
typedef struct {
    uint16_t    code;    /* PLC 故障码（D8060~D8067 的值） */
    uint8_t     cls;     /* 所属错误类别下标(0~7) */
    const char *msg;     /* 错误内容 */
} fx_plcinf_errmsg_t;

/* 串行通信类的原因码表：按错误码的"低 2 位"匹配（覆盖 62xx / 63xx / 38xx） */
static const struct {
    uint8_t     sub;     /* 错误码低 2 位 */
    const char *msg;
} g_err_serial_msg[] = {
    { 0x01u, "奇偶校验/溢出/帧错误" },
    { 0x02u, "通信字符错误" },
    { 0x03u, "通信数据和校验不一致" },
    { 0x04u, "数据格式错误" },
    { 0x05u, "命令错误" },
    { 0x06u, "监视超时" },
    { 0x07u, "调制解调器初始化错误" },
    { 0x08u, "简易PC间链接参数错误" },
    { 0x09u, "简易PC间链接程序错误" },
    { 0x12u, "并联链接字符错误" },
    { 0x13u, "并联链接和校验错误" },
    { 0x14u, "并联链接格式错误" },
    { 0x20u, "变频器通信中的通信错误" },
    { 0x21u, "MODBUS通信出现错误" },
    { 0x30u, "存储器访问错误" },
    { 0x40u, "特殊适配器连接异常" },
};

/* 其余类别（硬件/参数/语法/回路/运算）的错误码 -> 错误内容 */
static const fx_plcinf_errmsg_t g_err_msg_table[] = {
    /* ── 类别 1：PLC 硬件错误 (D8061) ── */
    { 6101u, 1u, "存储器访问错误" },
    { 6102u, 1u, "运算回路错误" },
    { 6103u, 1u, "I/O总线错误" },
    { 6104u, 1u, "扩展单元24V掉电" },
    { 6105u, 1u, "看门狗定时器错误" },
    { 6106u, 1u, "I/O表制作错误" },
    { 6107u, 1u, "系统构成错误" },
    { 6108u, 1u, "扩展总线错误" },
    { 6112u, 1u, "存储器盒写入错误" },
    { 6113u, 1u, "存储器盒保护开关ON" },
    { 6114u, 1u, "CC-Link/LT主站写入错误" },
    { 6115u, 1u, "CC-Link/LT配置超时" },

    /* ── 类别 4：参数错误 (D8064) ── */
    { 6401u, 4u, "程序间校验和不一致" },
    { 6402u, 4u, "内存容量设定错误" },
    { 6403u, 4u, "保持区域设定错误" },
    { 6404u, 4u, "注释区域设定错误" },
    { 6405u, 4u, "文件寄存器设定错误" },
    { 6406u, 4u, "BFM初始值校验和不一致" },
    { 6407u, 4u, "BFM初始值数据异常" },
    { 6409u, 4u, "其它的设定错误" },
    { 6411u, 4u, "CC-Link/LT参数错误" },
    { 6412u, 4u, "CC-Link/LT参数和校验错" },
    { 6413u, 4u, "CC-Link/LT参数和校验错" },
    { 6420u, 4u, "特殊参数和校验不一致" },
    { 6421u, 4u, "特殊参数的设定错误" },

    /* ── 类别 5：语法错误 (D8065) ── */
    { 6501u, 5u, "指令/软元件组合错误" },
    { 6502u, 5u, "设定值前缺OUT/T/C" },
    { 6503u, 5u, "应用指令操作数不够" },
    { 6504u, 5u, "标签编号重复" },
    { 6505u, 5u, "软元件编号超出范围" },
    { 6506u, 5u, "使用了未定义的指令" },
    { 6507u, 5u, "标签编号(P)定义错误" },
    { 6508u, 5u, "中断输入(I)定义错误" },
    { 6509u, 5u, "其它的语法错误" },
    { 6510u, 5u, "MC嵌套编号大小错误" },

    /* ── 类别 6：回路错误 (D8066) ── */
    { 6610u, 6u, "LD/LDI连续超9次" },
    { 6611u, 6u, "ANB/ORB指令数过多" },
    { 6612u, 6u, "ANB/ORB指令数过少" },
    { 6613u, 6u, "MPS连续超12次" },
    { 6614u, 6u, "遗漏MPS" },
    { 6615u, 6u, "遗漏MPP" },
    { 6616u, 6u, "MPS-MRD/MPP关系错误" },
    { 6617u, 6u, "母线上未接指令" },
    { 6618u, 6u, "主程序专用指令用错" },
    { 6619u, 6u, "FOR-NEXT内非法指令" },
    { 6620u, 6u, "FOR-NEXT嵌套超出" },
    { 6621u, 6u, "FOR-NEXT对应关系错误" },
    { 6622u, 6u, "无NEXT指令" },
    { 6623u, 6u, "无MC指令" },
    { 6624u, 6u, "无MCR指令" },
    { 6625u, 6u, "STL连续超9次" },
    { 6626u, 6u, "STL-RET间非法指令" },
    { 6627u, 6u, "无STL指令" },
    { 6628u, 6u, "主程序内有非法指令" },
    { 6629u, 6u, "无P、I指令" },
    { 6630u, 6u, "无SRET/IRET指令" },
    { 6631u, 6u, "SRET指令场所错误" },
    { 6632u, 6u, "FEND指令场所错误" },

    /* ── 类别 7：运算错误 (D8067) ── */
    { 6701u, 7u, "跳转目标地址未定义" },
    { 6702u, 7u, "CALL嵌套超出6个" },
    { 6703u, 7u, "中断嵌套超出3个" },
    { 6704u, 7u, "FOR-NEXT嵌套超出6个" },
    { 6705u, 7u, "操作数不是软元件" },
    { 6706u, 7u, "软元件编号超出范围" },
    { 6707u, 7u, "文件寄存器未设定" },
    { 6708u, 7u, "FROM/TO指令错误" },
    { 6709u, 7u, "其它的运算错误" },
    { 6710u, 7u, "参数之间不匹配" },
    { 6730u, 7u, "采样时间超出范围" },
    { 6732u, 7u, "输入滤波常数超出范围" },
    { 6733u, 7u, "比例增益超出范围" },
    { 6734u, 7u, "积分时间超出范围" },
    { 6735u, 7u, "微分增益超出范围" },
    { 6736u, 7u, "微分时间超出范围" },
    { 6740u, 7u, "采样时间超运算周期" },
    { 6763u, 7u, "脉冲输出编号重复" },
    { 6770u, 7u, "存储器访问错误" },
    { 6771u, 7u, "存储器盒未连接" },
    { 6772u, 7u, "存储器盒写入保护" },
    { 6773u, 7u, "RUN中写入存储器盒" },
};

#define FX_PLCINF_ERR_SERIAL_NUM  (sizeof(g_err_serial_msg)/sizeof(g_err_serial_msg[0]))
#define FX_PLCINF_ERR_TABLE_NUM   (sizeof(g_err_msg_table)/sizeof(g_err_msg_table[0]))

/*********************************************************************
 * @fn      FX_PLCINF_LookupErrMsg
 *
 * @brief   查表：错误码 + 类别 -> 错误内容
 *
 * @param   code - PLC 故障码
 *          cls  - 错误类别下标(0~7)
 *
 * @return  错误内容字符串；未命中返回 NULL（调用方回退显示"类别名"）
 */
static const char* FX_PLCINF_LookupErrMsg(uint16_t code, uint8_t cls)
{
    uint16_t i;

    /* 串行通信类（0/1/2 = D8062/D8063/D8438）与 USB 通信错误(D8487)：
     * 手册 38.4 里这几个表的错误码"低 2 位"含义完全相同（6201/6301/3801/8702 → 同一条原因），
     * 因此共用同一张原因码表 —— 一张表覆盖 4 个类别。 */
    if (cls == 2u || cls == 3u || cls == 8u || cls == 9u) {
        uint8_t sub = (uint8_t)(code & 0xFFu);
        for (i = 0; i < FX_PLCINF_ERR_SERIAL_NUM; i++) {
            if (g_err_serial_msg[i].sub == sub) {
                return g_err_serial_msg[i].msg;
            }
        }
        return NULL;
    }

    /* 其余类别：精确匹配"代码 + 类别" */
    for (i = 0; i < FX_PLCINF_ERR_TABLE_NUM; i++) {
        if (g_err_msg_table[i].code == code && g_err_msg_table[i].cls == cls) {
            return g_err_msg_table[i].msg;
        }
    }
    return NULL;
}

/*********************************************************************
 * @fn      FX_PLCINF_DecodeIOErr
 *
 * @brief   I/O 构成错误(D8060) 的动态解码：代码是"未安装的 I/O 起始编号"的 BCD
 *          4 位 BCD：最高位 = 种类(1=输入X, 0=输出Y)，低 3 位 = 编号
 *          例：D8060 = 1020(BCD) -> 输入 X020 以后没有安装
 *
 * @param   code     - D8060 的值(BCD)
 *          out      - 输出缓冲
 *          out_size - 缓冲大小
 *
 * @return  none
 */
static void FX_PLCINF_DecodeIOErr(uint16_t code, char *out, uint8_t out_size)
{
    uint8_t  kind_digit = (uint8_t)((code >> 12) & 0x0Fu);
    uint16_t num = (uint16_t)((((code >> 8) & 0x0Fu) * 100u) +
                              (((code >> 4) & 0x0Fu) * 10u) +
                              (code & 0x0Fu));

    if (out == NULL || out_size < 24u) {
        return;
    }
    sprintf(out, "%c%03u 以后未安装", (kind_digit != 0u) ? 'X' : 'Y', (unsigned)num);
}

/*********************************************************************
 * @fn      FX_PLCINF_SetErrMsg
 *
 * @brief   组装一行错误信息："错误码 空格 错误内容"，写入 32 字节字段
 *          —— 按字节裁剪且不切断 GBK 汉字（否则页面会出现半个汉字乱码）
 *
 * @param   idx  - g_errors 行下标
 *          code - PLC 故障码
 *          msg  - 错误内容
 *
 * @return  none
 */
static void FX_PLCINF_SetErrMsg(uint8_t idx, uint16_t code, const char *msg)
{
    char    tmp[64];
    uint8_t n;
    uint8_t limit = (uint8_t)(sizeof(g_errors[idx].error_msg) - 1u);

    sprintf(tmp, "%u %s", (unsigned)code, (msg != NULL) ? msg : "");
    n = (uint8_t)strlen(tmp);
    if (n > limit) {
        n = limit;
        /* 被切断的位置若正好是 GBK 汉字的首字节(0x81~0xFE)，说明上一字节是半个汉字
         * —— 再退一格，保证最后留下的是完整字符 */
        if ((uint8_t)tmp[n] >= 0x81u) {
            n = (uint8_t)(n - 1u);
        }
    }
    memcpy(g_errors[idx].error_msg, tmp, n);
    g_errors[idx].error_msg[n] = '\0';
}

/*********************************************************************
 * @fn      FX_PLCINF_OnErrorReply
 *
 * @brief   处理 D8000~D8069 回帧：解析出错误代码并查表填充错误信息表
 *          （D8060~D8067 中非 0 的类别即为"当前发生的错误"）
 *
 * @param   data - 已由 ASCII 十六进制解码为二进制的数据区(低位字在前)
 *          len  - 数据字节数
 *
 * @return  none
 */
void FX_PLCINF_OnErrorReply(const uint8_t *data, uint16_t len)
{
    uint8_t  i;
    uint8_t  row = 0;
    uint8_t  first_cls = 0xFFu;                /* 第一个有错误的类别（用于日志标注动作的 M） */
    uint16_t code;

    g_plc_err_busy = 0;                        /* 无论成败都解除在途标志 */
    if (data == NULL || len < 4u) {
        HTTPS_DEBUG("PLC信息页: 错误块回帧过短(len=%u)\r\n", len);
        return;
    }

    /* D8069：发生的步编号（本页各错误行共用；初次发生的步编号在 D8312/D8313） */
    if (len >= (uint16_t)((FX_PLCINF_ERR_IDX_STEP + 1u) * 2u)) {
        g_plc_err_step = (uint16_t)(data[FX_PLCINF_ERR_IDX_STEP * 2u] |
                                    ((uint16_t)data[FX_PLCINF_ERR_IDX_STEP * 2u + 1u] << 8));
    }
    /* 说明：D8004(动作的特M小编号) 属于 D8000~D8126 开机块，本运行期小块不再读取；
     *       动作的特M由下面的类别索引 first_cls 直接换算，效果等价。 */

    /* 先清空错误表（页面在"无错误"时显示"无错误"） */
    for (i = 0; i < FX_PLCINF_MAX_ERRORS; i++) {
        g_errors[i].error_no    = (uint8_t)(i + 1u);
        g_errors[i].cls         = 0xFFu;            /* 0xFF = 未使用 */
        g_errors[i].error_step  = g_plc_err_step;
        g_errors[i].error_msg[0] = '\0';
    }

    /* 逐类判读：D8060+i 非 0 即为该类别发生了错误 */
    for (i = 0; i < FX_PLCINF_ERR_CLASSES && row < FX_PLCINF_MAX_ERRORS; i++) {
        uint16_t off = (uint16_t)((FX_PLCINF_ERR_IDX_CODE0 + i) * 2u);
        const char *msg;

        if (len < (uint16_t)(off + 2u)) {
            break;                              /* 回帧比预期短：能解多少解多少 */
        }
        code = (uint16_t)(data[off] | ((uint16_t)data[off + 1u] << 8));
        if (code == 0u) {
            continue;                           /* 该类别无错误 */
        }

        if (i == 0u) {
            /* I/O构成错误：BCD 动态解码 */
            char txt[40];
            FX_PLCINF_DecodeIOErr(code, txt, (uint8_t)sizeof(txt));
            FX_PLCINF_SetErrMsg(row, code, txt);
        } else {
            msg = FX_PLCINF_LookupErrMsg(code, i);
            if (msg != NULL) {
                FX_PLCINF_SetErrMsg(row, code, msg);
            } else {
                /* 表里没有的代码：回退显示"错误码 + 类别名"，不留空白行 */
                FX_PLCINF_SetErrMsg(row, code, g_err_cls_name[i]);
            }
        }
        g_errors[row].error_step = g_plc_err_step;
        g_errors[row].cls        = i;           /* 记录类别：页面据此标出动作的 M(如 M8065) */
        if (first_cls == 0xFFu) {
            first_cls = i;                      /* 记下第一个错误的类别 */
        }
        row++;
    }
    g_plc_err_row_cnt = row;                    /* 扩展错误块从这里接着填 */

    /* 日志里同时打印"动作的特殊继电器"（M8060+i）与 D8004 的小编号，
     * 便于与手册 38.3.2 的动作关系、D8004 的含义对照排查 */
    g_plc_err_mnum = (first_cls < (uint8_t)FX_PLCINF_ERR_CLASSES_ALL) ? g_err_cls_m[first_cls] : 0u;
    HTTPS_DEBUG("PLC信息页: 错误块8类中 %d 类有错误, 步=%u, 动作特M M%u\r\n",
                row, (unsigned)g_plc_err_step, (unsigned)g_plc_err_mnum);
    for (i = 0; i < row; i++) {
        HTTPS_DEBUG("   [%d] %u  %s\r\n", i + 1,
                    (unsigned)g_errors[i].error_step, g_errors[i].error_msg);
    }
}

/*********************************************************************
 * @fn      FX_PLCINF_OnErrorReplyExt
 *
 * @brief   处理 D8438~D8489 扩展错误块：串行通信错误2 / USB通信 / 特殊模块·单元 / 特殊参数
 *          主块(D8000~D8069)先到、扩展块后到，因此这里从 g_plc_err_row_cnt 接着往下填行。
 *
 * @param   data - 已由 ASCII 十六进制解码为二进制的数据区(低位字在前)
 *          len  - 数据字节数
 *
 * @return  none
 */
void FX_PLCINF_OnErrorReplyExt(const uint8_t *data, uint16_t len)
{
    /* {类别下标, 寄存器在扩展块内的下标} —— 类别下标见 g_err_cls_name/g_err_cls_m */
    static const struct {
        uint8_t  cls;
        uint16_t idx;
    } k_ext[4] = {
        { 8u,  (uint16_t)(8438u - FX_PLCINF_ERR_EXT_START) },   /* 串行通信错误2 */
        { 9u,  (uint16_t)(8487u - FX_PLCINF_ERR_EXT_START) },   /* USB通信错误   */
        { 10u, (uint16_t)(8449u - FX_PLCINF_ERR_EXT_START) },   /* 特殊模块/单元 */
        { 11u, (uint16_t)(8489u - FX_PLCINF_ERR_EXT_START) },   /* 特殊参数错误  */
    };
    uint8_t i;

    g_plc_err_ext_busy = 0;                     /* 无论成败都解除在途标志 */
    if (data == NULL || len < 4u) {
        HTTPS_DEBUG("PLC信息页: 扩展错误块回帧过短(len=%u)\r\n", len);
        return;
    }

    for (i = 0; i < 4u; i++) {
        uint16_t off = (uint16_t)(k_ext[i].idx * 2u);
        uint16_t code;
        const char *msg;

        if (g_plc_err_row_cnt >= (uint8_t)FX_PLCINF_MAX_ERRORS) {
            break;                              /* 行已满 */
        }
        if (len < (uint16_t)(off + 2u)) {
            break;                              /* 回帧比预期短：能解多少解多少 */
        }
        code = (uint16_t)(data[off] | ((uint16_t)data[off + 1u] << 8));
        if (code == 0u) {
            continue;                           /* 该类别无错误 */
        }

        msg = FX_PLCINF_LookupErrMsg(code, k_ext[i].cls);
        if (msg != NULL) {
            FX_PLCINF_SetErrMsg(g_plc_err_row_cnt, code, msg);
        } else {
            /* 表里没有的（例如特殊模块/特殊参数：单元号·CH 的编码方式需按手册 38.4 的
             * 记号逐位对照，本页不猜测）：回退显示"错误码 + 类别名"，用户仍可按代码查手册 */
            FX_PLCINF_SetErrMsg(g_plc_err_row_cnt, code, g_err_cls_name[k_ext[i].cls]);
        }
        g_errors[g_plc_err_row_cnt].cls        = k_ext[i].cls;
        g_errors[g_plc_err_row_cnt].error_step = g_plc_err_step;
        HTTPS_DEBUG("   [ext] M%u %u  %s\r\n",
                    (unsigned)g_err_cls_m[k_ext[i].cls], (unsigned)code,
                    g_errors[g_plc_err_row_cnt].error_msg);
        g_plc_err_row_cnt++;
    }
}

 
/*********************************************************************
 * @fn      FX_PLCINF_SendWebPage
 *
 * @brief   流式发送PLC信息页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */

void FX_PLCINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url)
{
    char *temp_buffer = HtmlBuffer;
    char *error_table;
    const char *monitor_status;
    char *cpu_type_str;
    char *mem_type_str;
    char *battery_mode_str;
 
    uint32_t offset;

    /* ★ 每次渲染前：
     *   ① RTC_Get() 取板载 RTC 的当前时间(年月日/时间实时刷新；秒中断里也在更新，
     *      这里再取一次保证渲染瞬间是新鲜的)；
     *   ② 监视执行中则发起一次 M8000~M8015 读取，回帧异步刷新 LED
     *      (本页 LED 用"最近一次读到的状态"渲染，下一轮刷新即体现新值)。 */
    RTC_Get();
    /* 主动请求：监视执行中时，每次页面刷新都拉一次 PLC 状态字 + 错误信息。
     * 回帧异步到达，本轮先用上一次的值渲染，下一轮刷新即体现新值。 */
    FX_PLCINF_RequestStatus(Sour_Sock, Dest_Sock);

    /* 说明：CPU 类型/版本按固定值显示（原厂页面同样如此）；PLC 内部 D8001 读到的
     * "版本"字段与型号编码耦合，直接显示会出现 95.01 这类无意义值，故不使用缓存值。
     * 上电同步缓存 g_plc_cache 仍照常维护（上电同步时写入），供排障/后续扩展使用。 */

    /* 获取CPU类型字符串 */
    cpu_type_str = FX_PLCINF_GetCPUTypeString(g_plc_info.cpu_type);
    /* 获取存储器类型字符串（不再附"容量(块)"，与原厂页面一致） */
    mem_type_str = FX_PLCINF_GetMemTypeString(g_plc_info.mem_type);
    /* 获取电池模式字符串 */
    battery_mode_str = g_plc_info.battery_mode ? "有效" : "无效";
 
    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);
    /* 绑定表格流式发送的目标 socket */
    MITSU_HTTP_BeginTable(Dest_Sock);
    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);
     /* 第一次打包: HTML头部到</head> (使用共享组件) */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "PLC信息");
    /* CSS 样式表直发：组件 >1.2KB，HtmlBuffer 装不下(越界会写穿 BSS 导致死机) */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_CSS_NEW),
              strlen(HTML_GetComponent(HTML_COMP_CSS_NEW)));
    /* 第二次打包: body开始到导航栏结束 (使用共享组件) */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 导航栏直发：组件约 0.9KB，同样超过 HtmlBuffer 容量 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_NAV_BAR_NEW),
              strlen(HTML_GetComponent(HTML_COMP_NAV_BAR_NEW)));
    /* 第四次打包: 内容区域开始和表单开始 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<div class=\"content\">\r\n");
    /* 只有"监视执行中"才主动刷新；停止时不注入 meta refresh，改为被动 GET 刷新 */
    if (net_monitor_state == FX_MONITOR_RUNNING) {
        offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    }
    offset += HTML_PACK(temp_buffer, offset, "<form action=\"fx_plcinf.html\" method=\"post\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<div style=\"text-align:center\"><font style=\"font-size=16px\"><b>PLC信息</b></font></div>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第五次打包: PLC信息表格开始和标题行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td width=\"20\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"50\"></td><td width=\"10\"></td><td width=\"90\"></td><td width=\"200\"></td><td width=\"170\"></td><td width=\"80\"></td><td width=\"80\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"7\">PLC信息</td><td colspan=\"1\" align=\"right\">状态 :&nbsp;</td><td colspan=\"2\">%s</td></tr>\r\n", monitor_status);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: CPU类型和CPU版本行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", cpu_type_str);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">CPU版本</td><td colspan=\"1\" class=\"inf\" align=\"right\">%d.%02d</td><td colspan=\"1\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td></tr>\r\n", (g_plc_info.cpu_version >> 8) & 0xFF, g_plc_info.cpu_version & 0xFF);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第七次打包: 存储器类型和电池模式行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">存储器类型</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", mem_type_str);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">无电池模式</td><td colspan=\"1\" class=\"inf\" align=\"right\">%s</td><td colspan=\"3\"></td></tr>\r\n", battery_mode_str);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第八次打包: 日期和时间行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">年月日</td><td colspan=\"1\" class=\"inf\" align=\"right\">%04d-%02d-%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.w_year, calendar.w_month, calendar.w_date);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"5\">时间</td><td colspan=\"1\" class=\"inf\" align=\"right\">%02d:%02d:%02d</td><td colspan=\"3\"></td></tr>\r\n", calendar.hour, calendar.min, calendar.sec);
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    // /* 第九次打包: LED状态行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"10\">&nbsp;</td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"10\">LED状态</td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">POWER</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_power));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">RUN</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_run));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">BATT</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_batt));
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"1\"></td><td colspan=\"1\" align=\"right\">ERROR</td><td colspan=\"1\"></td><td colspan=\"1\" class=\"%s\">&nbsp;</td><td colspan=\"6\"></td></tr>\r\n", FX_PLCINF_LEDClass(g_plc_info.led_error));
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第十次打包: 表格结束和隐藏字段 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十一次打包: 错误信息表格标题 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" bgcolor=\"#ffffff\" style=\"margin:0 auto;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tbody><tr><td bgcolor=\"#cccccc\">错误信息</td></tr></tbody>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十二次打包: 生成并发送错误表格 */
    error_table = FX_PLCINF_GenerateErrorTable();
    Data_Send(Dest_Sock, (uint8_t*)error_table, strlen(error_table));

    /* 打包: 页面尾部 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</div>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</div>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</body>\r\n</html>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 页脚(含 SX 自检脚本)直发：组件 >1.2KB，远超 HtmlBuffer 容量。
     * 原来与收尾标签挤在同一包(约 1.27KB) -> 越界写穿 http_request / g_access_fifo
     * (其 records 指针被 HTML 文本覆盖) -> 下一次 FX_ACCLOG_AddRecord() 取 conn_id
     * 即 HardFault(mcause=4 未对齐取数, mtval 为文本字节)。 */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_FOOTER_NEW),
              strlen(HTML_GetComponent(HTML_COMP_FOOTER_NEW)));

    HTTPS_DEBUG("PLC信息页面流式发送完成\r\n");
}
