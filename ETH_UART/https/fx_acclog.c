/********************************** (C) COPYRIGHT *******************************
 * File Name          : fx_acclog.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/16
 * Description        : 三菱FX3U-ENET-ADP 访问履历页面实现
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <stdio.h>
#include <string.h>
#include "fx_acclog.h"
#include "fifo_queue.h"
#include "HTTPS.h"
#include "index.h"
#include "html_components.h"
#include "ethernet_app.h"

/* 全局变量定义 */
fx_acclog_record_t g_access_logs[FX_ACCLOG_MAX_RECORDS];
fx_acclog_fifo_t g_access_fifo = {0};
 
/* 静态函数声明 */
static void FX_ACCLOG_SendAccessTable(uint8_t Dest_Sock);

/*********************************************************************
 * @fn      FX_ACCLOG_Init
 *
 * @brief   初始化访问履历模块
 *
 * @param   无
 *
 * @return  无
 */
void FX_ACCLOG_Init(void )
{
    extern Log_data  log_data;                          // 日志记录数据
    uint8_t qsize;                                      // 钳位后的队列容量
    /* 初始化队列结构体 */
    /* ★ 容量钳位（关键，勿删）：PLC 侧"记录件数"合法范围是 1~16
     * （见 ethernet_app.h:109 / ethernet_app.c:577 的校验），而 g_access_logs[]
     * 只有 FX_ACCLOG_MAX_RECORDS(8) 个元素：
     *   - 直接传 16 → FIFO_Init 的 memset 写 352B 进 176B 数组 → 越界写穿 BSS，
     *     运行期 head % size 还会索引到 g_access_logs[8..15]；
     *   - 传 0（PLC 未设置/读取失败）→ head % 0 = 0xFFFFFFFF，截断为 0xFF
     *     → offset = 0xFF * item_size → 野地址 memcpy，首次连接即触发。
     * 故统一钳到 [1, FX_ACCLOG_MAX_RECORDS]。 */
    qsize = log_data.access_log_target_cnt;
    if (qsize == 0u || qsize > FX_ACCLOG_MAX_RECORDS) {
        qsize = FX_ACCLOG_MAX_RECORDS;
    }

    FIFO_Init(&g_access_fifo, g_access_logs, sizeof(fx_acclog_record_t), qsize);

    FX_ACCLOG_DEBUG("访问履历队列: 容量=%d (PLC设定=%d)\r\n", qsize, log_data.access_log_target_cnt);

    FX_ACCLOG_DEBUG("访问履历模块初始化完成\r\n");
}

/*********************************************************************
 * @fn      FX_ACCLOG_GetRecord
 *
 * @brief   获取指定访问履历`
 *
 * @param   index - 访问履历索引(0-31)
 *          record - 访问履历结构体指针
 *
 * @return  无
 */
void FX_ACCLOG_GetRecord(uint8_t index, fx_acclog_record_t *record)
{
    if (index < FX_ACCLOG_MAX_RECORDS && record != NULL) {
        memcpy(record, &g_access_logs[index], sizeof(fx_acclog_record_t));
    }
}

/*********************************************************************
 * @fn      FX_ACCLOG_AddRecord
 *
 * @brief   添加访问履历到队列
 *
 * @param   conn_id;         连接号 
 * @param   protocol;        协议类型 
 * @param   open_type;       开放方式 
 * @param   remote_ip;       通信对象IP地址 
 *
 * @return  无
 */
void FX_ACCLOG_AddRecord(uint16_t conn_id, uint8_t protocol, uint8_t open_type, uint8_t *remote_ip)
{
    if ( remote_ip == NULL) {
        return;
    }
    fx_acclog_record_t record;
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
    record.open_type = 0x02FF & open_type; // 开放方式 
    memcpy(record.remote_ip, remote_ip, 4);

    /* 去重: 检查上一条记录是否相同(conn_id + remote_ip), 相同则跳过 */
    if (g_access_fifo.count > 0) {
        uint8_t last_idx = (g_access_fifo.head - 1 + g_access_fifo.size) % g_access_fifo.size;
        fx_acclog_record_t *last = (fx_acclog_record_t *)((uint8_t *)g_access_fifo.records + last_idx * g_access_fifo.item_size);
        if (last->conn_id == record.conn_id &&
            memcmp(last->remote_ip, record.remote_ip, 4) == 0) {
            return;
        }
    }

#ifdef _FX_ACCLOG_DEBUG
    FX_ACCLOG_DEBUG("添加访问履历,当前记录数量: %d\r\n", g_access_fifo.count);
    FX_ACCLOG_DEBUG("时间:%04d-%02d-%02d %02d:%02d:%02d.\r\n", 
                    record.log_time.year, 
                    record.log_time.month, 
                    record.log_time.day, 
                    record.log_time.hour, 
                    record.log_time.minute, 
                    record.log_time.second);
    FX_ACCLOG_DEBUG("连接号: %d, 协议类型: %X, 开放方式: %X, 通信对象IP地址: %d.%d.%d.%d\r\n", 
                    record.conn_id, 
                    record.protocol, 
                    record.open_type, 
                    record.remote_ip[0], 
                    record.remote_ip[1], 
                    record.remote_ip[2],
                    record.remote_ip[3]);
#endif

    /* 添加新记录到队列 */
    FIFO_AddRecord(&g_access_fifo, &record);

}

/*********************************************************************
 * @fn      FX_ACCLOG_GetNextRecord
 *
 * @brief   按顺序获取下一条访问履历（从头到尾）
 *
 * @param   record - 访问履历结构体指针
 *
 * @return  1 - 成功获取记录，0 - 没有更多记录
 */
uint8_t FX_ACCLOG_GetNextRecord(fx_acclog_record_t *record)
{
    return FIFO_GetNextRecord(&g_access_fifo, record);
}

/*********************************************************************
 * @fn      FX_ACCLOG_ResetReadPos
 *
 * @brief   重置读取位置到最新记录
 *
 * @param   无
 *
 * @return  无
 */
void FX_ACCLOG_ResetReadPos(void)
{
    FIFO_ResetReadPos(&g_access_fifo);
}

/*********************************************************************
 * @fn      FX_ACCLOG_ClearRecords
 *
 * @brief   清空访问履历队列
 *
 * @param   无
 *
 * @return  无
 */
void FX_ACCLOG_ClearRecords(void)
{
    FIFO_Clear(&g_access_fifo);
}

/*********************************************************************
 * @fn      FX_ACCLOG_GetRecordCount
 *
 * @brief   获取访问履历记录数量
 *
 * @param   无
 *
 * @return  记录数量
 */
uint8_t FX_ACCLOG_GetRecordCount(void)
{
    return FIFO_GetCount(&g_access_fifo);
}

 
/*********************************************************************
 * @fn      WCHNET_UpdateAccLog
 *
 * @brief   将访问记录更新到PLC的寄存器中
 *
 * @param   无
 *
 * @return  无
 *
 * @note    根据配置的记录件数(access_log_target_cnt)，将最新的访问记录写入PLC寄存器
 *          支持D寄存器和R寄存器两种目标类型
 */
void WCHNET_UpdateAccLog(void)
{
    /* 检查是否启用了访问日志存储 */
    // if (log_data.access_log_target_cnt == 0 || log_data.access_log_target_reg == 0) {
    //     return;
    // }

    /* 获取实际记录数量（不超过配置的最大记录件数） */
    uint8_t record_count = FX_ACCLOG_GetRecordCount();
    uint8_t write_count = (record_count < log_data.access_log_target_cnt) ? record_count : log_data.access_log_target_cnt;
    #ifdef _FX_ACCLOG_DEBUG
        FX_ACCLOG_DEBUG("访问记录更新到PLC寄存器，共写入 %d 条记录\r\n", write_count );
        FX_ACCLOG_DEBUG("目标寄存器类型: %d ,%s\r\n", 
                            log_data.access_log_target_reg ,
                            (log_data.access_log_target_reg == 0x02 )? "R寄存器" : "D寄存器");    
    #endif
    if (write_count == 0) {
        return;
    }


    /* 获取寄存器起始地址 */
    uint16_t start_device = log_data.access_log_index;
    uint16_t record_size = sizeof(fx_acclog_record_t) / sizeof(uint16_t);

    /* 遍历并写入最新的 access_log_target_cnt 条记录 */
    fx_acclog_record_t record;
    for (uint8_t i = 0; i < write_count; i++) 
    {
        //按顺序获取下一条访问履历（从头到尾）
        if (!FX_ACCLOG_GetNextRecord(&record)) {
            break;
        }
        /* 根据配置选择寄存器类型 */
        if (log_data.access_log_target_reg == 0x02) {
            /* R寄存器 */
            MELSEC_FX_Build_E16_write_R_Cmd(0xFF, 0xFF, start_device,
                (uint16_t*)&record, record_size);
        } else {
            /* D寄存器（默认） */
            MELSEC_FX_BuildE10WriteParamCmd(0xFF, 0xFF, start_device,
                (uint16_t*)&record, record_size);
        }
        /* 计算下一条记录的起始地址 */
        start_device += record_size;
        /* 短暂延迟确保数据发送完成 */
        Delay_Ms(50);
    }
        
    /* 重置读取位置到最新记录 */
    FX_ACCLOG_ResetReadPos();
}
 
/*********************************************************************
 * @fn      FX_ACCLOG_SendAccessTable
 *
 * @brief   分批发送访问履历表格HTML (避免内存越界)
 *
 * @param   Dest_Sock - Socket ID
 *
 * @return  无
 */
/* ── 记录字段 → 显示文本（必须"值域映射"，绝不能用原始编码当数组下标）──
 * 记录里存的是原始编码：
 *   protocol  = ETH_TYPE_TCP(0x01) / ETH_TYPE_UDP(0x02)      (见 bsp_wch_net.c 入队处)
 *   open_type = Pro_Type：0xA0 MELSOFT / 0xA6 TCP-MC / 0xA7 UDP-MC / 0xA8 数据监视
 * 原实现用它们直接索引 2~4 元素的小数组：open_str[0xA0] 越界 640 字节，
 * 读到栈上的野指针，再交给 sprintf("%s") 解引用非法地址，导致硬件异常/看门狗复位
 * （现象：一进访问履历页面就死机重启）。此处改为逐值映射并带默认兜底。 */
static const char* FX_ACCLOG_ProtoStr(uint16_t protocol)
{
    switch (protocol) {
        case (uint16_t)ETH_TYPE_TCP: return "TCP";
        case (uint16_t)ETH_TYPE_UDP: return "UDP";
        default:                     return "----";
    }
}

static const char* FX_ACCLOG_OpenStr(uint16_t open_type)
{
    switch (open_type) {
        case (uint16_t)PRO_TCPC_MELSOFT: return "MELSOFT连接";
        case (uint16_t)PRO_TCPC_MC:      return "MC协议";
        case (uint16_t)PRO_UDPC_MC:      return "MC协议";
        case (uint16_t)PRO_TCP_HTTP:     return "数据监视";
        default:                         return "----";
    }
}

static void FX_ACCLOG_SendAccessTable(uint8_t Dest_Sock)
{
    char *temp_buffer = HtmlBuffer;
    uint32_t offset;
    uint8_t i;
    uint8_t row_count = 0;  /* 用于计数每4-5行发送一次 */
    uint8_t rows;                   /* 表格行数（含未设置时的回退值） */

    /* 行数来源：PLC 设定的访问履历点数(FIFO 容量)。若为 0（未设置）则回退 16 行，
     * 否则整表只剩表头、看起来像页面故障（原厂页面固定显示整表）。 */
    rows = FIFO_GetSize(&g_access_fifo);
    if (rows == 0u) { rows = 16u; }

    /* 第一次打包: 表格容器开始 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"1\" cellspacing=\"1\" style=\"margin:0 auto;\">\r\n<tbody>\r\n<tr>\r\n<td>\r\n<table border=\"1\" cellspacing=\"0\" bgcolor=\"#ffffff\" style=\"table-layout:fixed;text-align:center;font-size:14px\">\r\n<tbody>\r\n<tr bgcolor=\"#cccccc\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"60\">No.</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"130\">年月日</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"80\">时间</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"190\">连接号</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"80\">协议</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"160\">开放方式</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<td width=\"110\">通信对象<br>IP地址</td>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</tr>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);

    /* 生成并发送访问履历行，每4-5行打包一次 */
    row_count = 0;
    offset = 0;

    if (FIFO_GetCount(&g_access_fifo) > 0) {
        /* 重置读取位置到最新记录 */
        FX_ACCLOG_ResetReadPos();
        
        /* 显示最新记录 */
        fx_acclog_record_t latest_record;
        if (FX_ACCLOG_GetNextRecord(&latest_record)) {
            offset += HTML_PACK(temp_buffer, offset,
                    "<tr>\r\n"
                    "<td class=\"ct\">最新</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "</tr>\r\n",
                    latest_record.log_time.year, 
                    latest_record.log_time.month, 
                    latest_record.log_time.day,
                    latest_record.log_time.hour, 
                    latest_record.log_time.minute, 
                    latest_record.log_time.second,
                    latest_record.conn_id,
                    FX_ACCLOG_ProtoStr(latest_record.protocol),
                    FX_ACCLOG_OpenStr(latest_record.open_type),
                    latest_record.remote_ip[0], 
                    latest_record.remote_ip[1],
                    latest_record.remote_ip[2], 
                    latest_record.remote_ip[3]);
            row_count++;
        }
        
        /* 显示剩余记录 */
        fx_acclog_record_t record;
        uint8_t record_index = 1;
        while (FX_ACCLOG_GetNextRecord(&record) && record_index < rows) {
            offset += HTML_PACK(temp_buffer, offset,
                    "<tr>\r\n"
                    "<td class=\"ct\">%d</td>\r\n"
                    "<td>%04d-%02d-%02d</td>\r\n"
                    "<td>%02d:%02d:%02d</td>\r\n"
                    "<td>%d</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%s</td>\r\n"
                    "<td>%d.%d.%d.%d</td>\r\n"
                    "</tr>\r\n",
                    record_index,
                    record.log_time.year, record.log_time.month, record.log_time.day,
                    record.log_time.hour, record.log_time.minute, record.log_time.second,
                    record.conn_id,
                    FX_ACCLOG_ProtoStr(record.protocol),
                    FX_ACCLOG_OpenStr(record.open_type),
                    record.remote_ip[0], record.remote_ip[1],
                    record.remote_ip[2], record.remote_ip[3]);
            row_count++;
            record_index++;
            
            /* 每4行或者缓冲区接近800字节时打包发送 */
            if (row_count >= 4 || offset > 700) {
                Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
                offset = 0;
                row_count = 0;
            }
        }
        
        /* 显示空行 */
        for (; record_index < rows; record_index++) {
            offset += HTML_PACK(temp_buffer, offset,
                    "<tr>\r\n"
                    "<td class=\"ct\">%d</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "</tr>\r\n",
                    record_index);
            row_count++;
            
            /* 每4行或者缓冲区接近800字节时打包发送 */
            if (row_count >= 4 || offset > 700) {
                Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
                offset = 0;
                row_count = 0;
            }
        }
    } else {
        /* 所有行都是空行 */
        for (i = 0; i < rows; i++) {
            offset += HTML_PACK(temp_buffer, offset,
                    "<tr>\r\n"
                    "<td class=\"ct\">%d</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "<td>&nbsp;</td>\r\n"
                    "</tr>\r\n",
                    i);
            row_count++;
            
            /* 每4行或者缓冲区接近800字节时打包发送 */
            if (row_count >= 4 || offset > 700) {
                Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
                offset = 0;
                row_count = 0;
            }
        }
    }

    /* 发送剩余数据 */
    if (offset > 0) {
        Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    }

    /* 表格尾部 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n</table>\r\n</td>\r\n</tr>\r\n</tbody>\r\n</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
}

/*********************************************************************
 * @fn      FX_ACCLOG_SendWebPage
 *
 * @brief   流式发送访问履历页面 (避免在RAM中存储完整页面)
 *
 * @param   Dest_Sock - Socket ID
 *          url - URL字符串
 *
 * @return  none
 */
void FX_ACCLOG_SendWebPage(uint8_t Sour_Sock ,uint8_t  Dest_Sock,char *url)
{
    char *temp_buffer = HtmlBuffer;
    // char *access_table;
    char *monitor_status;
    uint32_t offset;
 
    /* 获取监控状态 */
    monitor_status  = (char*) HTML_GetStateString(net_monitor_state);
    /* 发送HTTP响应头 */
    SendHttpHeader(Dest_Sock, PTYPE_HTML);
    /* 第一次打包: HTML头部到</head> (使用共享组件) */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, HTML_GetComponent(HTML_COMP_HEADER), "访问履历");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* CSS 样式表直发：组件 >1.2KB，HtmlBuffer 装不下(越界会写穿 BSS 导致死机) */
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_CSS_NEW),
              strlen(HTML_GetComponent(HTML_COMP_CSS_NEW)));
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "%s", HTML_GetComponent(HTML_COMP_BODY_START_NEW));
    Data_Send(Dest_Sock, (uint8_t*)HTML_GetComponent(HTML_COMP_NAV_BAR_NEW),
              strlen(HTML_GetComponent(HTML_COMP_NAV_BAR_NEW)));
    
    /* 第五次打包: 内容区域开始、页面标题和表单开始 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<div class=\"content\">\r\n<br>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<div style=\"text-align:center\"><font style=\"font-size=16px\"><b>访问履历</b></font></div>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<form action=\"fx_acclog.html\" method=\"post\">\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第六次打包: 控制面板 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<table border=\"0\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:14px; margin:0 auto;\">\r\n<tbody>\r\n<tr><td width=\"100\"></td><td width=\"100\"></td><td width=\"100\"></td><td width=\"100\"></td><td width=\"100\"></td><td width=\"100\"></td><td width=\"80\"></td><td width=\"80\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"5\"></td><td colspan=\"1\" align=\"right\">状态 :&nbsp;</td><td colspan=\"2\">%s</td></tr>\r\n", monitor_status);
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"6\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视开始\" style=\"width:120;font-weight:bold\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "<tr><td colspan=\"6\"></td><td colspan=\"2\"><input type=\"submit\" name=\"CMD\" value=\"监视停止\" style=\"width:120;font-weight:bold\"></td></tr>\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</tbody>\r\n</table>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第七次打包: 表单结束 */
    offset = 0;
    offset += HTML_PACK(temp_buffer, offset, "<input type=\"hidden\" name=\"LANG\" value=\"ZS\">\r\n");
    offset += HTML_PACK(temp_buffer, offset, "</form>\r\n");
    Data_Send(Dest_Sock, (uint8_t*)temp_buffer, offset);
    /* 第八次打包: 生成并发送访问履历表格（分批发送） */
    FX_ACCLOG_SendAccessTable(Dest_Sock);
    /* 第九次打包: 页面尾部 */
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

    FX_ACCLOG_DEBUG("访问履历页面流式发送完成\r\n");
}


