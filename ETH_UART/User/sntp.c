
#include "sntp.h"

#include "melsec_fx_net.h"
#include "string.h"
#include "eth_driver.h"
#include "bsp_wch_net.h"
#include "bsp_uart.h"
#include "ethernet_app.h"
#include "bsp_rtc.h"


ntpformat NTPformat;
datetime Nowdatetime;
static uint8_t data_buf[48];  /* SNTP 发送缓冲区（仅 NTP 重试循环使用） */
uint8_t time_zone;
 
uint16_t ntp_retry_cnt = 0;     // counting the ntp retry number
uint8_t ntp_retry_enable = 1;   // enable or disable the ntp retry

extern _calendar_obj calendar;

/*
00)UTC-12:00 Baker Island, Howland Island (both uninhabited)
01) UTC-11:00 American Samoa, Samoa
02) UTC-10:00 (Summer)French Polynesia (most), United States (Aleutian Islands, Hawaii)
03) UTC-09:30 Marquesas Islands
04) UTC-09:00 Gambier Islands;(Summer)United States (most of Alaska)
05) UTC-08:00 (Summer)Canada (most of British Columbia), Mexico (Baja California)
06) UTC-08:00 United States (California, most of Nevada, most of Oregon, Washington (state))
07) UTC-07:00 Mexico (Sonora), United States (Arizona); (Summer)Canada (Alberta)
08) UTC-07:00 Mexico (Chihuahua), United States (Colorado)
09) UTC-06:00 Costa Rica, El Salvador, Ecuador (Galapagos Islands), Guatemala, Honduras
10) UTC-06:00 Mexico (most), Nicaragua;(Summer)Canada (Manitoba, Saskatchewan), United States (Illinois, most of Texas)
11) UTC-05:00 Colombia, Cuba, Ecuador (continental), Haiti, Jamaica, Panama, Peru
12) UTC-05:00 (Summer)Canada (most of Ontario, most of Quebec)
13) UTC-05:00 United States (most of Florida, Georgia, Massachusetts, most of Michigan, New York, North Carolina, Ohio, Washington D.C.)
14) UTC-04:30 Venezuela
15) UTC-04:00 Bolivia, Brazil (Amazonas), Chile (continental), Dominican Republic, Canada (Nova Scotia), Paraguay,
16) UTC-04:00 Puerto Rico, Trinidad and Tobago
17) UTC-03:30 Canada (Newfoundland)
18) UTC-03:00 Argentina; (Summer) Brazil (Brasilia, Rio de Janeiro, Sao Paulo), most of Greenland, Uruguay
19) UTC-02:00 Brazil (Fernando de Noronha), South Georgia and the South Sandwich Islands
20) UTC-01:00 Portugal (Azores), Cape Verde
21) UTC&#177;00:00 Cote d'Ivoire, Faroe Islands, Ghana, Iceland, Senegal; (Summer) Ireland, Portugal (continental and Madeira)
22) UTC&#177;00:00 Spain (Canary Islands), Morocco, United Kingdom
23) UTC+01:00 Angola, Cameroon, Nigeria, Tunisia; (Summer)Albania, Algeria, Austria, Belgium, Bosocketidia and Herzegovina,
24) UTC+01:00 Spain (continental), Croatia, Czech Republic, Denmark, Germany, Hungary, Italy, Kinshasa, Kosovo,
25) UTC+01:00 Macedonia, France (metropolitan), the Netherlands, Norway, Poland, Serbia, Slovakia, Slovenia, Sweden, Switzerland
26) UTC+02:00 Libya, Egypt, Malawi, Mozambique, South Africa, Zambia, Zimbabwe, (Summer)Bulgaria, Cyprus, Estonia,
27) UTC+02:00 Finland, Greece, Israel, Jordan, Latvia, Lebanon, Lithuania, Moldova, Palestine, Romania, Syria, Turkey, Ukraine
28) UTC+03:00 Belarus, Djibouti, Eritrea, Ethiopia, Iraq, Kenya, Madagascar, Russia (Kaliningrad Oblast), Saudi Arabia,
29) UTC+03:00 South Sudan, Sudan, Somalia, South Sudan, Tanzania, Uganda, Yemen
30) UTC+03:30 (Summer)Iran
31) UTC+04:00 Armenia, Azerbaijan, Georgia, Mauritius, Oman, Russia (European), Seychelles, United Arab Emirates
32) UTC+04:30 Afghanistan
33) UTC+05:00 Kazakhstan (West), Maldives, Pakistan, Uzbekistan
34) UTC+05:30 India, Sri Lanka
35) UTC+05:45 Nepal
36) UTC+06:00 Kazakhstan (most), Bangladesh, Russia (Ural: Sverdlovsk Oblast, Chelyabinsk Oblast)
37) UTC+06:30 Cocos Islands, Myanmar
38) UTC+07:00 Jakarta, Russia (Novosibirsk Oblast), Thailand, Vietnam
39) UTC+08:00 China, Hong Kong, Russia (Krasocketidoyarsk Krai), Malaysia, Philippines, Singapore, Taiwan, most of Mongolia, Western Australia
40) UTC+09:00 Korea, East Timor, Russia (Irkutsk Oblast), Japan
41) UTC+09:30 Australia (Northern Territory);(Summer)Australia (South Australia))
42) UTC+10:00 Russia (Zabaykalsky Krai); (Summer)Australia (New South Wales, Queensland, Tasmania, Victoria)
43) UTC+10:30 Lord Howe Island
44) UTC+11:00 New Caledonia, Russia (Primorsky Krai), Solomon Islands
45) UTC+11:30 Norfolk Island
46) UTC+12:00 Fiji, Russia (Kamchatka Krai);(Summer)New Zealand
47) UTC+12:45 (Summer)New Zealand
48) UTC+13:00 Tonga
49) UTC+14:00 Kiribati (Line Islands)
*/


/**
 * @brief 从NTP服务器响应中提取并转换时间戳（考虑时区）
 * @param buf NTP响应数据缓冲区
 * @param idx 时间戳起始偏移量
 * @note 时区值对应关系：
 *       0: UTC-12:00, 21-22: UTC±00:00, 39: UTC+08:00(中国标准时间)
 */
void get_seconds_from_ntp_server(uint8_t *buf, uint16_t idx)
{
    tstamp seconds = 0;
    uint8_t i = 0;

    // 从NTP响应中提取4字节时间戳（大端序）
    for (i = 0; i < 4; i++)
    {
        seconds = (seconds << 8) | buf[idx + i];
    }

    // 时区偏移查找表（单位：秒）
    // 负值表示西时区（UTC-），正值表示东时区（UTC+）
    const int32_t timezone_offsets[] = {
        -43200,   // 0:  UTC-12:00
        -39600,   // 1:  UTC-11:00
        -36000,   // 2:  UTC-10:00
        -34200,   // 3:  UTC-09:30
        -32400,   // 4:  UTC-09:00
        -28800,   // 5:  UTC-08:00
        -28800,   // 6:  UTC-08:00
        -25200,   // 7:  UTC-07:00
        -25200,   // 8:  UTC-07:00
        -21600,   // 9:  UTC-06:00
        -21600,   // 10: UTC-06:00
        -18000,   // 11: UTC-05:00
        -18000,   // 12: UTC-05:00
        -18000,   // 13: UTC-05:00
        -16200,   // 14: UTC-04:30
        -14400,   // 15: UTC-04:00
        -14400,   // 16: UTC-04:00
        -12600,   // 17: UTC-03:30
        -10800,   // 18: UTC-03:00
        -7200,    // 19: UTC-02:00
        -3600,    // 20: UTC-01:00
        0,        // 21: UTC±00:00
        0,        // 22: UTC±00:00
        3600,     // 23: UTC+01:00
        3600,     // 24: UTC+01:00
        3600,     // 25: UTC+01:00
        7200,     // 26: UTC+02:00
        7200,     // 27: UTC+02:00
        10800,    // 28: UTC+03:00
        10800,    // 29: UTC+03:00
        12600,    // 30: UTC+03:30
        14400,    // 31: UTC+04:00
        16200,    // 32: UTC+04:30
        18000,    // 33: UTC+05:00
        19800,    // 34: UTC+05:30
        20700,    // 35: UTC+05:45
        21600,    // 36: UTC+06:00
        23400,    // 37: UTC+06:30
        25200,    // 38: UTC+07:00
        28800,    // 39: UTC+08:00 (中国标准时间)
        32400,    // 40: UTC+09:00
        34200,    // 41: UTC+09:30
        36000,    // 42: UTC+10:00
        37800,    // 43: UTC+10:30
        39600,    // 44: UTC+11:00
        41400,    // 45: UTC+11:30
        43200,    // 46: UTC+12:00
        46500,    // 47: UTC+12:45
        46800,    // 48: UTC+13:00
        50400,    // 49: UTC+14:00
    };

    // 应用时区偏移（边界保护）
    if (time_zone < sizeof(timezone_offsets) / sizeof(timezone_offsets[0]))
    {
        seconds += timezone_offsets[time_zone];
    }

    // 计算并转换日期时间
    calcdatetime(seconds);
}



/**
 * @brief 将时间戳（从1900年起的秒数）转换为年月日时分秒
 * @param seconds 从1900年1月1日起的总秒数
 * @note NTP时间起点为1900年1月1日（UTC）
 */
void calcdatetime(tstamp seconds)
{
    uint8_t yf = 0;          // 月份（1-12）
    tstamp n = 0, d = 0, total_d = 0, rz = 0;  // 剩余秒数、已过天数、总天数、年内天数
    uint16_t y = 0, r = 0, yr = 0;  // 年份、闰年计数、日
    signed long long yd = 0;  // 年内剩余天数

    n = seconds;
    total_d = seconds / SECS_PERDAY;  // 转换为总天数
    d = 0;
    uint32_t p_year_total_sec = SECS_PERDAY * 365;  // 平年秒数
    uint32_t r_year_total_sec = SECS_PERDAY * 366;  // 闰年秒数

    // 逐年计算年份和剩余天数
    while (n >= p_year_total_sec)
    {
        // 判断当前年份是否为闰年
        if ((EPOCH + r) % 400 == 0 || ((EPOCH + r) % 100 != 0 && (EPOCH + r) % 4 == 0))
        {
            n = n - r_year_total_sec;  // 闰年减去366天
            d = d + 366;
        }
        else
        {
            n = n - p_year_total_sec;  // 平年减去365天
            d = d + 365;
        }
        r += 1;
        y += 1;
    }

    y += EPOCH;  // 计算实际年份
    Nowdatetime.yy = y;

    // 计算年内剩余天数
    yd = 0;
    yd = total_d - d;

    // 逐月计算月份和日期
    yf = 1;
    while (yd >= 28)
    {
        // 31天的月份：1、3、5、7、8、10、12月
        if (yf == 1 || yf == 3 || yf == 5 || yf == 7 || yf == 8 || yf == 10 || yf == 12)
        {
            yd -= 31;
            if (yd < 0) break;
            rz += 31;
        }

        // 2月（需判断闰年）
        if (yf == 2)
        {
            if (y % 400 == 0 || (y % 100 != 0 && y % 4 == 0))
            {
                yd -= 29;  // 闰年2月
                if (yd < 0) break;
                rz += 29;
            }
            else
            {
                yd -= 28;  // 平年2月
                if (yd < 0) break;
                rz += 28;
            }
        }

        // 30天的月份：4、6、9、11月
        if (yf == 4 || yf == 6 || yf == 9 || yf == 11)
        {
            yd -= 30;
            if (yd < 0) break;
            rz += 30;
        }
        yf += 1;
    }

    Nowdatetime.mo = yf;  // 保存月份
    yr = total_d - d - rz;  // 计算日期
    yr += 1;  // 日期从1开始
    Nowdatetime.dd = yr;

    // 计算时分秒
    seconds = seconds % SECS_PERDAY;  // 当天剩余秒数
    Nowdatetime.hh = seconds / 3600;  // 小时
    Nowdatetime.mm = (seconds % 3600) / 60;  // 分钟
    Nowdatetime.ss = (seconds % 3600) % 60;  // 秒
}

/**
 * @brief 将日期时间转换为从1900年起的时间戳（秒数）
 * @return 从1900年1月1日起的总秒数
 * @note 与calcdatetime函数互逆，用于时间计算
 */
tstamp changedatetime_to_seconds(void)
{
    tstamp seconds = 0;
    uint32_t total_day = 0;
    uint16_t i = 0, run_year_cnt = 0, l = 0;

    l = Nowdatetime.yy;  // 当前年份

    // 计算从EPOCH年到当前年份之间的闰年数量
    for (i = EPOCH; i < l; i++)
    {
        if ((i % 400 == 0) || ((i % 100 != 0) && (i % 4 == 0)))
        {
            run_year_cnt += 1;
        }
    }

    // 计算总天数（平年天数 + 闰年天数）
    total_day = (l - EPOCH - run_year_cnt) * 365 + run_year_cnt * 366;

    // 计算当年已过天数（逐月累加）
    for (i = 1; i <= Nowdatetime.mo; i++)
    {
        if (i == 5 || i == 7 || i == 10 || i == 12)
        {
            total_day += 30;  // 30天的月份（5月、7月、10月、12月）
        }
        else if (i == 3)
        {
            // 2月：判断是否为闰年
            if ((l % 400 == 0) || ((l % 100 != 0) && (l % 4 == 0)))
            {
                total_day += 29;  // 闰年2月
            }
            else
            {
                total_day += 28;  // 平年2月
            }
        }
        else if (i == 2 || i == 4 || i == 6 || i == 8 || i == 9 || i == 11)
        {
            total_day += 31;  // 31天的月份
        }
    }

    // 转换为秒数
    seconds = (total_day + Nowdatetime.dd - 1) * 24 * 3600;  // 天数转秒
    seconds += Nowdatetime.ss;       // 加秒
    seconds += Nowdatetime.mm * 60;  // 加分
    seconds += Nowdatetime.hh * 3600;  // 加时

    return seconds;
}

void  set_ntp_retry_state(uint8_t enable)
{
    if(enable)
    {     
        ntp_retry_enable = enable;   
        ntp_retry_cnt = 0;
        SNTP_DEBUG("执行时间设置 enable=%d \r\n",enable);
    }
}
uint8_t  get_ntp_retry_state(void)
{
    return ntp_retry_enable;   
}
/**
 * @brief 初始化SNTP客户端
 * @param s Socket编号（未使用，保留用于扩展）
 * @param ntp_server NTP服务器IP地址数组（4字节）
 * @param tz 时区编号（0-49）
 * @param buf 数据接收缓冲区指针
 * @note 时区39对应UTC+08:00（中国标准时间）
 */
void SNTP_init(uint8_t s, uint8_t *ntp_server, uint8_t tz)
{
    // 配置NTP服务器地址
    NTPformat.dstaddr = ntp_server;   // sntp 指针

    time_zone = tz;
    ntp_retry_cnt = 0;
    ntp_retry_enable = 1;    // 上电启用sntp

    SNTP_DEBUG("SNTP IP:%d.%d.%d.%d\r\n",
                    NTPformat.dstaddr[0],NTPformat.dstaddr[1],
                    NTPformat.dstaddr[2],NTPformat.dstaddr[3] );
}
 
/**
 * @brief 处理NTP服务器响应并更新RTC时钟
 * @param socketid Socket编号
 * @param frame_buff 接收到的NTP响应数据
 * @param frame_len 响应数据长度
 * @return 1: 成功处理
 */
int8_t process_sntp_response(uint8_t socketid, uint8_t* frame_buff, uint16_t frame_len)
{
    SNTP_DEBUG("sntp recvfrom len = %d\r\n", frame_len);
    extern _calendar_obj calendar;
    // 解析NTP时间戳（从偏移40字节处读取传输时间戳）
    get_seconds_from_ntp_server(frame_buff, 40);
    if( calendar.w_year < 100 ) 
    {
        // 年份显示调整 年: 2023 -> 23 
        Nowdatetime.yy = Nowdatetime.yy%100 ;   //保留2位
        SNTP_DEBUG("年:%02d ",Nowdatetime.yy);
    } else{
        // 年份显示调整 年: 2023 
        SNTP_DEBUG("年:%04d ",Nowdatetime.yy);
    }
    SNTP_DEBUG("月:%02d 日:%02d  时间:%02d:%02d:%02d\r\n",
                Nowdatetime.mo, Nowdatetime.dd,
                Nowdatetime.hh, Nowdatetime.mm, Nowdatetime.ss);

    // 设置RTC实时时钟
    RTC_Set(Nowdatetime.yy, Nowdatetime.mo, Nowdatetime.dd,
            Nowdatetime.hh, Nowdatetime.mm, Nowdatetime.ss);

    // 重置重试计数器
    ntp_retry_cnt = 0;
    // 校时成功,停止这一次的校时.等待下一次触发校时
    ntp_retry_enable = 0;
    // 时间设置结果存储目标寄存器类型（01:D寄存器，02:R寄存器）
    wizchip_sntp_execute_D8013_PLC();

 
    return 1; // 成功
}


/**
 * @brief 执行NTP请求重试操作
 * @param socketid Socket编号
 * @return 0: 继续重试; 1: 达到重试上限
 */
int8_t sntp_execute_retry(uint8_t socketid)
{
    //uint32_t lend = sizeof(ntpmessage);
    uint32_t lend = 48;

    if( ntp_retry_cnt == 0 )
    {
        wizchip_sntp_get_D8013_PLC();         // 获取 D8013-D8018  rtc时间
        ntp_retry_cnt = 1;
    }
    // 检查重试次数限制
    else if (ntp_retry_cnt < 60)
    {
        ntp_retry_cnt++;
        SNTP_DEBUG("send Sntp socketid= %d ,lend= %d, etry_cnt = %d \r\n",socketid,lend ,ntp_retry_cnt);
        // 构造NTP请求报文标志字节
        // 格式：LI(2位) + VN(3位) + Mode(3位)
        // LI=0（无闰秒警告）, VN=4（NTP版本4）, Mode=3（客户端模式）
        uint8_t Flag = (0 << 6) | (4 << 3) | 3;
        // 清零并填充NTP请求报文
        memset(data_buf, 0, 48 );
        memcpy(data_buf, (void const*)(&Flag), 1);
        // 发送NTP请求到服务器
        WCHNET_SocketUdpSendTo(socketid, data_buf, &lend,NTPformat.dstaddr, ntp_port);

        SNTP_DEBUG("IP:%03d.%03d.%03d.%03d  \r\n",
                    NTPformat.dstaddr[0],NTPformat.dstaddr[1],
                    NTPformat.dstaddr[2],NTPformat.dstaddr[3]);
        #if NET_LED_ENABLE == 1            
        NEN_TX_LED_Trigger();  // 触发发送LED闪烁
        #endif
    }
    else
    {
        // 检查是否达到最大重试次数（60次）
        ntp_retry_enable = 0;
        ntp_retry_cnt = 0;  // 达到重试上限
        SNTP_DEBUG("Sntp  retry failed after %d attempts!\r\n", ntp_retry_cnt);
 
        return 1; // 达到重试上限
    }
    return 0; // 继续重试
}
 
/**
 * @brief SNTP客户端主任务，负责时间同步调度
 * @param phy_status 物理链路状态（1=已连接, 0=断开）
 * @return 0: 未执行/正在重试/成功; 1: 达到重试上限(获取失败)
 * @note 本函数在 main 的 while(1) 中被高频调用(循环内无延时)，
 *       故用 Nowdatetime.ss 做 1Hz 节拍门控，保证每秒仅执行一次，
 *       避免对 PLC 重复轮询及日志刷屏。
 *       触发模式：
 *       1. 间隔模式：每隔 execution_interval 分钟触发一次
 *       2. 定时模式：在 execution_time[0]:execution_time[1] 时触发
 */
int8_t Sntp_client_task( uint8_t phy_status )
{
    extern _calendar_obj calendar;   // 日期时间对象

    /* 1Hz 节拍门控: main 循环无延时, 用秒变化保证每秒仅执行一次 */
    if (Nowdatetime.ss == calendar.sec) {
        return 0;
    }
    Nowdatetime.ss = calendar.sec;   // 记录本次秒, 同一秒内的后续调用直接返回

    if (phy_status == 0) {
        return 0;   // 物理链路未就绪, 不做事
    }

    sntp_cfg_t sntp_cfg;
    sntp_cfg.raw = (uint8_t)(sntp_time.sntp_enable & 0xff);  // 获取SNTP功能配置
    if ( !sntp_cfg.bits.enable || sntp_cfg.raw == 0xFF )     // bit7 == 0: 未启用
    {
        return 0;  // SNTP未使能，直接退出
    }

    uint8_t socketid = eth_socket[5].SocketIdForListen;  // SNTP使用socket 5（与UDP广播共用）
    uint8_t ret = 0;
    /* sntp_cfg 位域详见 ethernet_app.h 中 sntp_cfg_t 定义 */

    static uint8_t sntp_start_flag  = 0;
    /* ── Bit6: 电源启动时是否执行时间设置 ── */
    if (sntp_cfg.bits.startup && sntp_start_flag == 0) {
        BSP_DEBUG("sntp_time : 勾选 电源开始时执行时间设置. 执行一次 \r\n");
        sntp_start_flag = 1;
        ntp_retry_cnt = 0;
        ntp_retry_enable = 1;  // 启用重试
    }

    // 非重试态: 每5秒轮询一次 PLC 手动校时(M8404)
    if (ntp_retry_enable == 0 && calendar.sec % 5 == 0) {
        wizchip_get_PLC_M8404_info();
    }

    // 非重试态: 分钟变化后检查定时/间隔触发条件
    if (ntp_retry_enable == 0 && Nowdatetime.mm != calendar.min) {
        Nowdatetime.mm = calendar.min;  // 先更新, 防本秒内重复进入

        SNTP_DEBUG("RTC时间: %04d-%02d-%02d %02d:%02d:%02d\r\n",
                calendar.w_year, calendar.w_month, calendar.w_date,
                calendar.hour, calendar.min, calendar.sec);

        /* ── Bit4: 执行模式（间隔 / 定时） ── */
        if (sntp_cfg.bits.exec_mode) {
            BSP_DEBUG("定时执行时间 %d:%d \r\n",
                        sntp_time.execution_time[0],
                        sntp_time.execution_time[1]);
            // 模式2：定点触发（每天指定时间点）
            if ( sntp_time.execution_time[0] == calendar.hour &&
                sntp_time.execution_time[1] == calendar.min )
            {
                ntp_retry_cnt = 0;
                ntp_retry_enable = 1;
                SNTP_DEBUG("定点触发: %02d:%02d\r\n", calendar.hour, calendar.min);
            }
        } else {
            BSP_DEBUG("间隔执行时间 %d 分钟\r\n",
                        sntp_time.execution_interval);
            // 模式1：间隔触发（每隔N分钟）, execution_interval>0 防止除零
            if (sntp_time.execution_interval > 0 &&
                calendar.min % sntp_time.execution_interval == 1)
            {
                ntp_retry_cnt = 0;
                ntp_retry_enable = 1;
                SNTP_DEBUG("间隔触发: 分钟=%d\r\n", calendar.min);
            }
        }
    }
 
    // 重试态: 每秒发送一次 NTP 请求
    if ( ntp_retry_enable ) {
        
        ret = sntp_execute_retry(socketid);
        if (ret != 0) {
            /* ── Bit5: 错误时行为 ── */
            if (sntp_cfg.bits.on_error) {
                BSP_DEBUG("错误:继续执行, 重置重试计数\r\n");
                ntp_retry_cnt = 0;       // sntp_execute_retry 已达上限并关闭 enable, 此处重新开启以持续重试
                ntp_retry_enable = 1;
            } else {
                BSP_DEBUG("错误:停止执行, \r\n");
                ntp_retry_enable = 0;    // 停止重试

            }
            return ret;
        }
        
    }

    return 0;
}
