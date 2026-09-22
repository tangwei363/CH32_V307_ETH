/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_enetinf.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP信息页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_enetinf.h"
#include "fifo_queue.h"
#include "HTTPS.h"
#include "html_components.h"
#include "index.h"
#include "ethernet_app.h"
#include "eth_driver.h"        /* SocketInf[]：查询套接字真实连接状态(OPEN LED 用) */
#include "bsp_uart.h"
/* 全局变量定义 */
static fx_enetinf_adapter_t g_adapter_info;

/* LED 状态 -> 原厂 CSS 类名（绿=ledg / 亮=ledr / 灭=off，类定义见共享 CSS）
 * 说明: 原代码调用了一个全工程并不存在的 LED 类名函数(已随旧实现删除)，
 *       这正是 LED 段被整块注释掉的原因。写法与 fx_plcinf.c 保持一致。 */
static const char* FX_ENETINF_LEDClass(fx_enetinf_led_state_t st)
{
    if (st == FX_ENET_LED_GREEN) { return "ledg"; }
    if (st == FX_ENET_LED_ON)    { return "ledr"; }
    return "off";
}

/* ★ LED 实时刷新（原实现只在 FX_ENETINF_Init 里赋一次固定值，页面 4 个色块永远是静态的）。
 * 数据源全部取工程内已在维护的实时量，不引入新依赖：
 *   POWER：net_status.bit.link_b7  —— PHY 链路连通(bsp_wch_net.c 的 PHY 变化处理中维护)
 *   100M ：net_status.bit.speed_b2 —— 链路速率 100Mbps(ethernet_app.c 初始化时置位)
 *   ERR. ：任一套接字带错误码 eth_socket[i].Error_Code != 0 → 红灯(错误锁存，与硬件一致)
 *   OPEN ：WCHNET 套接字表中任一连接已建立(SockStatus: TCP=ESTABLISHED / UDP=已开放) → 绿灯
 * 由 FX_ENETINF_SendWebPage() 在每次渲染前调用一次。 */
static void FX_ENETINF_UpdateLEDs(void)
{
    uint8_t i;
    uint8_t has_err  = 0;
    uint8_t has_open = 0;

    for (i = 0; i < ETH_MAX_CONNECTIONS; i++) {
        if (eth_socket[i].Error_Code != 0u) {
            has_err = 1;
        }
        /* OPEN：WCHNET 套接字表中任一连接已建立 → 绿灯
         * SockStatus 低字节 = 套接字状态(SOCK_STAT_OPEN)，次低字节 = TCP 状态
         * (wchnet.h 定义，仅 TCP 模式有意义)；UDP 无连接概念，只要套接字已开放即计入。 */
        if (((SocketInf[i].SockStatus & 0xFFu) == SOCK_STAT_OPEN) &&
            ((SocketInf[i].ProtoType != PROTO_TYPE_TCP) ||
             (((SocketInf[i].SockStatus >> 8) & 0xFFu) == TCP_ESTABLISHED))) {
            has_open = 1;
        }
    }

    g_adapter_info.led_power = net_status.bit.link_b7 ? FX_ENET_LED_GREEN : FX_ENET_LED_OFF;
    g_adapter_info.led_100m  = (net_status.bit.link_b7 && net_status.bit.speed_b2)
                               ? FX_ENET_LED_GREEN : FX_ENET_LED_OFF;
    g_adapter_info.led_err   = has_err  ? FX_ENET_LED_ON    : FX_ENET_LED_OFF;
    g_adapter_info.led_open  = has_open ? FX_ENET_LED_GREEN : FX_ENET_LED_OFF;
}
 
fx_enetinf_error_log_t g_error_logs[FX_ENETINF_MAX_ERRORS];
fifo_queue_t g_error_fifo = {0};
/* 1 = 队列里有尚未上传到 PLC 的履历（由添加路径置位，主循环 FX_ErrorLogs_Task 消费） */
static uint8_t g_error_logs_dirty = 0;

/* 静态函数声明 */
static char* FX_ENETINF_GenerateErrorTable(void);

/*********************************************************************
 * @fn      FX_ENETINF_Init
 *
 * @brief   初始化FX3U-ENET-ADP信息模块
 *
 * @param   无
 *
 * @return  无
 */
void FX_ENETINF_Init(void)
{
    uint8_t qsize;                                      // 钳位后的队列容量
    /* 初始化适配器信息 - 示例值 */
    g_adapter_info.version = 0x0122;  /* 版本1.22 */
    /* 设置LED状态 */
    g_adapter_info.led_power = FX_ENET_LED_GREEN;  /* POWER灯常亮 */
    g_adapter_info.led_100m = FX_ENET_LED_GREEN;   /* 100M灯常亮 */
    g_adapter_info.led_err = FX_ENET_LED_OFF;      /* ERR灯关闭 */
    g_adapter_info.led_open = FX_ENET_LED_GREEN;   /* OPEN灯常亮 */
    
    /* 初始化错误履历队列 */
    /* 同类钳位（与 fx_acclog 一致）：PLC 侧件数上限 16，而 g_error_logs[]
     * 只有 FX_ENETINF_MAX_ERRORS 个元素，直接透传同样会越界写穿 BSS；
     * 为 0 时 head % 0 会算出野偏移指针。 */
    qsize = log_data.error_log_target_cnt;
    if (qsize == 0u || qsize > FX_ENETINF_MAX_ERRORS) {
        qsize = FX_ENETINF_MAX_ERRORS;
    }

    /* ★ L7：PLC 配置的件数超过本模块数组上限时这里静默钳位 —— 明确告警一次，
     *   否则用户没法知道"配置没有完全生效"（页面只会显示 8 行）。
     *   注意：本函数只在开机(WRITE_PHY_CHANGE → MITSU_HTTP_Init)调用一次；
     *   若运行期 PLC 侧改了件数，需重新调用本函数，队列长度才会跟随。 */
    if (log_data.error_log_target_cnt > FX_ENETINF_MAX_ERRORS) {
        HTTPS_DEBUG("错误履历配置 %d 条 > 上限 %d，已按上限生效\r\n",
                    (int)log_data.error_log_target_cnt, (int)FX_ENETINF_MAX_ERRORS);
    }

    FIFO_Init(&g_error_fifo, g_error_logs, sizeof(fx_enetinf_error_log_t), qsize);

    HTTPS_DEBUG("FX3U-ENET-ADP信息模块初始化完成\r\n");
}
 
/*********************************************************************
 * @fn      FX_ENETINF_GetRecord
 *
 * @brief   获取指定错误履历
 *
 * @param   index - 错误履历索引(0-15)
 *          record - 错误履历结构体指针
 *
 * @return  无
 */
/* 说明：原 FX_ENETINF_GetRecord() 按"物理槽位"直读 g_error_logs[index]，与 FIFO 的
 * "按时间倒序"语义冲突（回绕后取到的是不同记录，且当队列容量 < FX_ENETINF_MAX_ERRORS 时
 * 会读到从未写过的槽位）。该函数无任何调用者，已删除；需要取记录请用
 * FX_ENETINF_GetErrorLog()（按龄：0 = 最新）。 */

/*********************************************************************
 * @fn      FX_ENETINFACCLOG_AddRecord
 *
 * @brief   添加错误履历到队列
 *
 * @param   conn_id;         连接号 
 * @param   protocol;        协议类型 
 * @param   open_type;       开放方式 
 * @param   local_port;      本站端口号 
 * @param   error_code;      错误代码 
 * @param   remote_ip[4];    通信对象IP地址 
 * @param   remote_port;     通信对象端口号 
 * @param   cmd_code;        指令代码 
 * @return  无
 */
void FX_ENETINF_ACCLOG_AddRecord(uint16_t conn_id, uint8_t protocol, uint8_t open_type, 
                                uint16_t local_port, uint16_t error_code, uint8_t remote_ip[4],
                                uint16_t remote_port, uint8_t cmd_code)
{
    if ( remote_ip == NULL) {
        return;
    }
    fx_enetinf_error_log_t record;

    /* ★ 必须先整体清零：reserved_1/reserved_2(约定 0x0000) 以及结构体的填充字节
     *   会被"原样"写入 PLC 的履历区，未初始化时写进去的是栈上垃圾。 */
    memset(&record, 0, sizeof(record));
    // 更新RTC实时时钟       
    RTC_Get();
    record.log_time.year = calendar.w_year;
    record.log_time.month = calendar.w_month;
    record.log_time.day = calendar.w_date;
    record.log_time.hour = calendar.hour;
    record.log_time.minute = calendar.min;
    record.log_time.second = calendar.sec;
    record.conn_id = conn_id;  // 连接号 
    record.protocol = protocol; // 协议类型 TCP / UDP
    /* 开放方式：直接用调用方给的 Pro_Type 原始编码(0xA0/0xA6/0xA8/0xA9…)。
     * 原实现写成 "0x02FF & open_type" 属无意义掩码(形参已是 uint8_t)，
     * 若调用方按 PLC 的 0x02xx 编码传入还会把高位截断，使页面显示退化成 "----"。
     * "编码 -> 文本"的映射交给显示侧的 FX_ENETINF_OpenStr()。 */
    record.open_type = open_type; // 开放方式 
    record.local_port = local_port; // 本站端口号 
    record.error_code = error_code; // 错误代码 
    record.remote_port = remote_port; // 通信对象端口号 
    record.cmd_code = cmd_code; // 指令代码 
    memcpy(record.remote_ip, remote_ip, 4);
    
    /* 添加新记录到队列 */
    FIFO_AddRecord(&g_error_fifo, &record);

    /* ★ 有新错误 ? 置"待上传"标志，真正的串口上传交给主循环 FX_ErrorLogs_Task()：
     *   本函数可能在 socket 事件/中断上下文被调用，不允许在这里做带延时的串口写。 */
    g_error_logs_dirty = 1;
}

/*********************************************************************
 * @fn      FX_ENETINF_GetErrorLog
 *
 * @brief   获取指定错误履历
 *
 * @param   index - 错误履历索引(0-15)
 *          log - 错误履历结构体指针
 *
 * @return  无
 */
uint8_t FX_ENETINF_GetErrorLog(uint8_t index, fx_enetinf_error_log_t *log)
{
    if (log == NULL) {
        return 0;
    }
    /* ★ 关键修正：取不到记录时必须清零输出。
     *   原实现在"队列不足 index+1 条"时直接跳出循环、不写 log，调用方会继续使用
     *   未初始化或上一次遗留的数据（显示脏记录）。
     *   另外改为"按龄访问"(age 0 = 最新)，不再依赖 FIFO 的全局游标 ? 渲染/上传互不干扰。 */
    memset(log, 0, sizeof(fx_enetinf_error_log_t));
    return FIFO_GetByAge(&g_error_fifo, index, log);
}

/*********************************************************************
 * @fn      FX_ErrorLogs_GetNextRecord
 *
 * @brief   按顺序获取下一条错误履历（从头到尾）
 *
 * @param   record - 错误履历结构体指针
 *
 * @return  1 - 成功获取记录，0 - 没有更多记录
 */
uint8_t FX_ErrorLogs_GetNextRecord(fx_enetinf_error_log_t *record)
{
    return FIFO_GetNextRecord(&g_error_fifo, record);
}
/*********************************************************************
 * @fn      FX_ErrorLogs_ResetReadPos
 *
 * @brief   重置读取位置到最新记录
 *
 * @param   无
 *
 * @return  无
 */
void FX_ErrorLogs_ResetReadPos(void)
{
    FIFO_ResetReadPos(&g_error_fifo);
}
/*********************************************************************
 * @fn      FX_ENETINF_ClearErrorLogs
 *
 * @brief   清空错误履历队列
 *
 * @param   无
 *
 * @return  无
 */
void FX_ENETINF_ClearErrorLogs(void)
{
    FIFO_Clear(&g_error_fifo);
}


/*********************************************************************
 * @fn      FX_ErrorLogs_SendAccess_PLC
 *
 * @brief   分批发送访问履历PLC寄存器中 (避免内存越界)
 *
 *
 * @return  无
 */
void FX_ErrorLogs_SendAccess_PLC(void)
{
    uint8_t  count, limit, i;
    uint16_t start_device;
    uint16_t record_words;
    fx_enetinf_error_log_t record;

    /* ① 未使能（件数为 0 / 寄存器类型非法）立即返回：不做任何串口动作与延时 */
    if (log_data.error_log_target_cnt == 0u || log_data.error_log_target_reg == 0u) {
        g_error_logs_dirty = 0;
        return;
    }

    count = FIFO_GetCount(&g_error_fifo);
    if (count == 0u) {
        g_error_logs_dirty = 0;
        return;
    }

    /* ② 上传条数不超过 PLC 配置的记录件数（原实现把队列全部写出，会超出 PLC 的履历区） */
    limit = (count < log_data.error_log_target_cnt) ? count : log_data.error_log_target_cnt;

    /* ③ 起始地址必须用"软元件范围"字段 error_log_index。
     *    原实现写成 log_data.error_log_target_cnt（件数），于是写到 D1/D2… 一带，
     *    把无关寄存器当履历区覆盖 —— 这是与访问履历(WCHNET_UpdateAccLog)对照后
     *    确认的复制粘贴错误。 */
    start_device = log_data.error_log_index;

    /* ④ 单条长度必须换算成"字数"：MELSEC_FX_BuildE1x 的 count 是软元件点数(字)，
     *    直接用 sizeof() 得到的是字节数 ? 每条多写一倍，越界污染 PLC 寄存器。
     *    本模块履历布局 = 17 字(含开放方式)，与 ethernet_app.h 的 eth_err_log_t 注释一致。 */
    record_words = (uint16_t)(sizeof(fx_enetinf_error_log_t) / sizeof(uint16_t));

    /* ⑤ 按龄遍历(0 = 最新)，写入 PLC 时也是一条接一条顺序排列 */
    for (i = 0; i < limit; i++) {
        if (!FIFO_GetByAge(&g_error_fifo, i, &record)) {
            break;
        }
        if (log_data.error_log_target_reg == 0x02) {          /* R 寄存器 */
            MELSEC_FX_Build_E16_write_R_Cmd(0xFF, 0xFF, start_device,
                                            (uint16_t *)&record, record_words);
        } else {                                              /* D 寄存器(默认) */
            MELSEC_FX_BuildE10WriteParamCmd(0xFF, 0xFF, start_device,
                                            (uint16_t *)&record, record_words);
        }
        start_device += record_words;
        Delay_Ms(50);                                         /* 等本帧发出（主循环上下文） */
    }

    g_error_logs_dirty = 0;
    HTTPS_DEBUG("错误履历上传PLC: %d 条, 起始=%d, 每条=%d 字\r\n",
                limit, (int)log_data.error_log_index, (int)record_words);
}

/*********************************************************************
 * @fn      FX_ErrorLogs_Task
 *
 * @brief   错误履历上传任务（主循环调用）：有未上传的履历就写入 PLC 寄存器
 *
 * @param   无
 *
 * @return  无
 *
 * @note    添加路径可能运行在 socket 事件/中断上下文，只置 g_error_logs_dirty；
 *          带 Delay_Ms 的串口上传统一放到这里（主循环），避免阻塞事件回调。
 */
void FX_ErrorLogs_Task(void)
{
    if (g_error_logs_dirty == 0u) {
        return;
    }
    FX_ErrorLogs_SendAccess_PLC();      /* 内部完成后会清标志 */
}
 
/*********************************************************************
 * @fn      FX_ENETINF_GenerateErrorTable
 *
 * @brief   生成错误履历表格HTML
 *
 * @param   无
 *
 * @return  表格HTML字符串
 */
/* ── 记录字段 → 显示文本（值域映射，禁止用原始编码当数组下标）──
 * 与 fx_acclog.c 同因：protocol/open_type 是原始编码(0x01/0x02、0xA0~0xA8)，
 * 若直接索引 2~4 元素数组会越界读到野指针，再由 sprintf("%s") 解引用，导致死机。 */
static const char* FX_ENETINF_ProtoStr(uint16_t protocol)
{
    switch (protocol) {
        case (uint16_t)ETH_TYPE_TCP: return "TCP";
        case (uint16_t)ETH_TYPE_UDP: return "UDP";
        default:                     return "----";
    }
}

static const char* FX_ENETINF_OpenStr(uint16_t open_type)
{
    switch (open_type) {
        case (uint16_t)PRO_TCPC_MELSOFT: return "MELSOFT连接";
        case (uint16_t)PRO_TCPC_MC:      return "MC协议";
        case (uint16_t)PRO_UDPC_MC:      return "MC协议";
        case (uint16_t)PRO_TCP_HTTP:     return "数据监视";
        default:                         return "----";
    }
}

static char* FX_ENETINF_GenerateErrorTable(void)
{
    char *table_buffer = MITSU_HTTP_GetTableBuffer();
    char *temp_buffer = HtmlBuffer;
    uint8_t i;
    
    MITSU_HTTP_ClearTableBuffer();
    
    /* 表格头部 */
    MITSU_HTTP_Emit(
           "<table border=\"1\" cellspacing=\"1\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td>\r\n"
           "<table border=\"1\" cellspacing=\"0\" style=\"text-align:center;font-size:14px\">\r\n"
           "<tbody>\r\n"
           "<tr>\r\n"
           "<td width=\"50\">&nbsp;</td>\r\n"
           "<td width=\"110\">连接号</td>\r\n"
           "<td width=\"70\">协议</td>\r\n"
           "<td width=\"90\">开放方式</td>\r\n"
           "<td width=\"100\">本站端口号</td>\r\n"
           "<td width=\"50\">错误<br>代码</td>\r\n"
           "<td width=\"90\">通信对象<br>IP地址</td>\r\n"
           "<td width=\"100\">通信对象<br>端口号</td>\r\n"
           "<td width=\"60\">指令<br>代码</td>\r\n"
           "<td width=\"110\">年月日</td>\r\n"
           "<td width=\"70\">时间</td>\r\n"
           "</tr>\r\n");

    /* ★ L7 页面提示：PLC 侧配置的履历件数超过本机数组上限时明确告知 ——
     *   否则用户会以为"页面少了记录"或页面故障（本机只保留最新 FX_ENETINF_MAX_ERRORS 条）。
     *   colspan 必须等于表头列数（11：空列+连接号+协议+开放方式+本站端口号+错误代码
     *   +通信对象IP+通信对象端口号+指令代码+年月日+时间）。 */
    if (log_data.error_log_target_cnt > FX_ENETINF_MAX_ERRORS) {
        /* MITSU_HTTP_Emit() 不是可变参数函数，先 sprintf 到 HtmlBuffer 再提交 */
        sprintf(temp_buffer,
                "<tr bgcolor=\"#fff3cd\">\r\n"
                "<td colspan=\"11\">注意：PLC 配置的错误履历件数为 %d，"
                "本机仅保留最新 %d 条（超出部分不显示）。</td>\r\n"
                "</tr>\r\n",
                (int)log_data.error_log_target_cnt,
                (int)FX_ENETINF_MAX_ERRORS);
        MITSU_HTTP_Emit(temp_buffer);
    }
    
    /* 生成错误履历行 */
    if (FIFO_GetCount(&g_error_fifo) > 0) {
        /* ★ 显示最新记录：按龄访问(age 0 = 最新)，不再操作 FIFO 的全局游标 ——
         *   这样"渲染"没有副作用，也不会与"上传 PLC"互相偷记录。 */
        fx_enetinf_error_log_t latest_log;
        if (FIFO_GetByAge(&g_error_fifo, 0u, &latest_log)) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>最新</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04X</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "</tr>\r\n",
                    latest_log.conn_id,
                    FX_ENETINF_ProtoStr(latest_log.protocol),
                    FX_ENETINF_OpenStr(latest_log.open_type),
                    latest_log.local_port,
                    latest_log.error_code,
                    latest_log.remote_ip[0], latest_log.remote_ip[1],
                    latest_log.remote_ip[2], latest_log.remote_ip[3],
                    latest_log.remote_port,
                    latest_log.cmd_code,
                    latest_log.log_time.year, latest_log.log_time.month, latest_log.log_time.day,
                    latest_log.log_time.hour, latest_log.log_time.minute, latest_log.log_time.second);
            MITSU_HTTP_Emit( temp_buffer);
        }
        
        /* 显示剩余记录：表行号 2..N 依次对应 age 1..N-1（仍是"最新在前"的时间倒序）。
         * 注意把范围判断放在前面：原写法先取记录再判范围，会在最后一次多消费一条。 */
        fx_enetinf_error_log_t log;
        uint8_t log_index = 2;
        while (log_index <= FX_ENETINF_MAX_ERRORS &&
               FIFO_GetByAge(&g_error_fifo, (uint8_t)(log_index - 1u), &log)) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04X</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "</tr>\r\n",
                    log_index,
                    log.conn_id,
                    FX_ENETINF_ProtoStr(log.protocol),
                    FX_ENETINF_OpenStr(log.open_type),
                    log.local_port,
                    log.error_code,
                    log.remote_ip[0], log.remote_ip[1],
                    log.remote_ip[2], log.remote_ip[3],
                    log.remote_port,
                    log.cmd_code,
                    log.log_time.year, log.log_time.month, log.log_time.day,
                    log.log_time.hour, log.log_time.minute, log.log_time.second);
            MITSU_HTTP_Emit( temp_buffer);
            log_index++;
        }
        
        /* 显示空行 */
        for (; log_index <= FX_ENETINF_MAX_ERRORS; log_index++) {
            sprintf(temp_buffer,
                    "<tr>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "</tr>\r\n",
                    log_index);
            MITSU_HTTP_Emit( temp_buffer);
        }
    } else {
        /* 所有行都是空行 */
        for (i = 0; i < FX_ENETINF_MAX_ERRORS; i++) {
            if (i == 0) {
                /* 最新行 - 无错误 */
                MITSU_HTTP_Emit(
                       "<tr>\r\n"
                       "<td>最新</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "<td>&nbsp;</td>\r\n"
                       "</tr>\r\n");
            } else {
                /* 空行 */
                sprintf(temp_buffer,
                        "<tr>\r\n"
                        "<td>%d</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "<td>&nbsp;</td>\r\n"
                        "</tr>\r\n",
                        i + 1);
                MITSU_HTTP_Emit( temp_buffer);
            }
        }
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
 * @fn      FX_ENETINF_SendWebPage
 *
 * @brief   流式发送FX3U-ENET-ADP信息页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */
void FX_ENETINF_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url)
{
    char *temp_buffer = HtmlBuffer;
    char *error_table;
    const char *monitor_status;
 
    uint32_t offset;
    /* 更新监视状态 */
    FX_ENETINF_UpdateMonitor();
    /* ★ 刷新 POWER/100M/ERR./OPEN 四个 LED 的实时状态（页面下方 LED 段的数据源） */
    FX_ENETINF_UpdateLEDs();
 
    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);
    /* 绑定表格流式发送的目标 socket */
    MITSU_HTTP_BeginTable(Dest_Sock);
    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);
    /* 第一次打包: HTML头部和CSS样式 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "FX3U-ENET-ADP信息");
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
    /* 第四次打包: 表单开始和适配器信息标题 */
    offset = 0;
    /* 只有"监视执行中"才主动刷新；停止时不注入 meta refresh，改为被动 GET 刷新 */
    if (net_monitor_state == FX_MONITOR_RUNNING) {
        offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_REFRESH));
    }
    offset += HTML_PACK(temp_buffer, offset, "<div style=\"text-align:center;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<form action=\"fx_enetinf.html\" method=\"post\" style=\"display:inline-block; text-align:left;\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<font style=\"font-size=16px\"><b>FX3U-ENET-ADP信息</b></font>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n<tbody>\r\n<tr>\r\n<td width=\"20\"></td>\r\n<td width=\"50\"></td>\r\n<td width=\"10\"></td>\r\n<td width=\"50\"></td>\r\n<td width=\"10\"></td>\r\n<td width=\"90\"></td>\r\n<td width=\"200\"></td>\r\n<td width=\"170\"></td>\r\n<td width=\"80\"></td>\r\n<td width=\"80\"></td>\r\n</tr>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第五次打包: 状态行和版本行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"7\">以太网适配器信息</td>\r\n<td colspan=\"1\" align=\"right\">状态 :&nbsp;</td>\r\n<td colspan=\"2\">%s</td>\r\n</tr>\r\n", monitor_status);
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">FX3U-ENET-ADP 版本</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%02d</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td>\r\n</tr>\r\n", (g_adapter_info.version >> 8) & 0xFF, g_adapter_info.version & 0xFF);
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"8\"></td>\r\n<td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td>\r\n</tr>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: 以太网适配器设置行 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"10\">以太网适配器设置</td>\r\n</tr>\r\n<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">IP地址</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n", Basic_CfgBuf.ip[0], Basic_CfgBuf.ip[1], Basic_CfgBuf.ip[2], Basic_CfgBuf.ip[3]);
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">子网掩码类型</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n", Basic_CfgBuf.mask[0], Basic_CfgBuf.mask[1], Basic_CfgBuf.mask[2], Basic_CfgBuf.mask[3]);
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"5\">默认路由器IP地址</td>\r\n<td colspan=\"1\" class=\"inf\" align=\"right\">%d.%d.%d.%d</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n", Basic_CfgBuf.gateway[0], Basic_CfgBuf.gateway[1], Basic_CfgBuf.gateway[2], Basic_CfgBuf.gateway[3]);
    /* 以太网适配器设置段必须在这里发出去：原代码只拼装、不发送，
     * 内容随即被下一个分包(offset=0)覆盖 → 页面缺整段"以太网适配器设置"。 */
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);


    /* 第八次打包(恢复): LED 状态标题 + POWER/100M/ERR./OPEN 四行
     * 原代码从 LED 行到 Data_Send 全被注释，导致 LED 段与表格/表单收尾
     * 都未发出；这里改成本文件内可用的 FX_ENETINF_LEDClass()，
     * 且 LED 段单独成包，不与上面的设置行挤在同一缓冲。 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"10\">&nbsp;</td>\r\n</tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"10\">LED 状态</td>\r\n</tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">POWER</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", FX_ENETINF_LEDClass(g_adapter_info.led_power));
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">100M</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", FX_ENETINF_LEDClass(g_adapter_info.led_100m));
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">ERR.</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", FX_ENETINF_LEDClass(g_adapter_info.led_err));
    offset += HTML_PACK(temp_buffer, offset, "<tr>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" align=\"right\">OPEN</td>\r\n<td colspan=\"1\"></td>\r\n<td colspan=\"1\" class=\"%s\">&nbsp;</td>\r\n<td colspan=\"6\"></td>\r\n</tr>\r\n", FX_ENETINF_LEDClass(g_adapter_info.led_open));
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n</table>\r\n<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第九次打包: 错误履历表格标题 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\">\r\n<tbody>\r\n<tr>\r\n<td colspan=\"5\" style=\"font-size:14px\">错误履历</td>\r\n<td colspan=\"3\"></td>\r\n</tr>\r\n</tbody>\r\n</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 第十次打包: 错误履历表格 */
    error_table = FX_ENETINF_GenerateErrorTable();
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

    HTTPS_DEBUG("FX3U-ENET-ADP信息页面流式发送完成\r\n");
}



