
#ifndef  _ETHERNET_APP_H_
#define  _ETHERNET_APP_H_

#include <stdio.h>
#include "ch32v30x.h"
#include "debug.h"

/* USER CODE END Private defines */
#ifdef _ETHERNET_DEBUG
    #define ETHERNET_DEBUG(format, ...)  printf (format, ##__VA_ARGS__)
#else
    #define ETHERNET_DEBUG(format, ...)
#endif

#define SOCKET_DIRECT_EN             1    // 直连TCPS melsoft链接            0:不启用 1:启用
#define SOCKET_DISCOVER_EN           1    // 局域网发现 TCPS 设备链接         0:不启用 1:启用
#define SOCKET_SNTP_EN               0    // SNTP 时间同步   UDP             0:不启用 1:启用
#define SOCKET_HTTP_EN               1    // HTTP 网页显示   TCP             0:不启用 1:启用
#define SOCKET_PHY_LINK_EN           0    // PHY link 状态同步               0:不启用 1:启用

#define FRAME_HEADER                0x02     //\STX
#define FRAME_END                   0x03     //\ETX

#define RETRY_DELAY_MS               700     // 重试延迟(ms)
#define MAX_RETRY_COUNT              10      // 最大重试次数
#define INITIAL_RETRY_DELAY_MS       200     // 初始重试延迟(ms)
#define MAX_RETRY_DELAY_MS           5000    // 最大重试延迟(ms)
#define ENABLE_RETRY_LOGGING         1       // 启用重试日志
#define ENABLE_ERROR_LOGGING         1       // 启用错误日志

#define NET_RETRY_DELAY_MS           100     // 重试延迟(ms)
#define NET_MAX_RETRY_COUNT          3      // 最大重试次数

 
#define ETH_RXBUF_MAX_SIZE          512
#define ETH_TXBUF_MAX_SIZE          612

#define ETH_MAX_CONNECTIONS         8
// 使用宏简化字节交换操作
#define SWAP_BYTES(val) (((val) << 8) | ((val) >> 8))
 
#pragma pack (1)                //编译器将按照1个字节对齐

/*各连接状状态*/
typedef struct eth_link_t
{
    //1 :连接号功能  (16 byte )
    uint16_t link_id;               // 0x5000,   //本站端口号:80--0x0050
    uint16_t remote_port;           // 0x0000,   //通讯对象端口号 
    uint8_t remote_ip[4];           // 0x0000,0x0000, //通讯对象IP地址

    uint16_t error_code;            // 0x0000,   //错误代码
    uint16_t open_mode;             // 0x02A8,   //0x02:TCP 0xA8 打开方式:数据监视
    uint16_t tcp_status;            // 0x0000,   //TCP 连接状态  0:切断 1:连接中
    uint16_t force_disable;         // 0x0000,   //强制禁用 状态 0:允许 1:禁用
}eth_link_t;

/* 协议统计信息 */  
typedef struct { 
    // (16 byte )
    uint32_t tcp_rx_bytes;    /* TCP接收包数 */
    uint32_t tcp_tx_bytes;    /* TCP发送包数 */
    uint32_t udp_rx_bytes;    /* UDP接收包数 */
    uint32_t udp_tx_bytes;    /* UDP发送包数 */
} eth_packet_t;

typedef struct eth_log_time_t
{
    uint16_t year;                  // 年 0x07EA  2023年
    uint16_t month;                 // 月 0x0004  4月
    uint16_t day;                   // 日 0x0002  2日
    uint16_t hour;                  // 时 0x0005  5时
    uint16_t minute;                // 分 0x0024  36分
    uint16_t second;                // 秒 0x000f  15秒
}eth_log_time_t;

//错误日志记录数据  15字节*2
typedef struct eth_err_log_t
{
    uint16_t connection_id;          // 连接号       0x03
    uint16_t protocol_type;         // 协议类型     0xA702  udp mc协议
    uint16_t local_port;            // 本站端口号   0x138a
    uint16_t error_code;            // 错误代码     0x09fe
    uint8_t remote_ip[4];           // 通信对象IP地址 0x01e0c0a8  (IPv4格式)
    uint16_t remote_port;           // 通信对象端口号 0x1770  6000
    uint16_t command_code;          // 指令代码     0x0030
    uint16_t reserved_1;            // 保留      0x0000
    uint16_t reserved_2;            // 保留      0x0000
    eth_log_time_t log_time;        // 日志时间
}eth_err_log_t;

//登入日志记录数据  10字节*2
typedef struct eth_login_log_t
{
    eth_log_time_t log_time;        // 日志时间
    uint16_t connection_id;         // 连接号       0x03
    uint16_t protocol_type;         // 协议类型     0xA702  udp mc协议
    uint8_t remote_ip[4];           // 通信对象IP地址 0x01e0c0a8  (IPv4格式)
}eth_login_log_t;

typedef struct log_data_t //日志记录数据  8字节*2
{
    // 设置错误日志的存储目标类型
    uint8_t error_log_target_cnt;   //（记录件数）(1~ 16)
    uint8_t error_log_target_reg;   // 错误日志存储目标寄存器类型（01:D寄存器，02:R寄存器）
    uint16_t error_log_index;       // 软元件范围（100:6400，高低互换为0064）
    // 设置访问日志的存储目标类型 
    uint8_t access_log_target_cnt;   //（记录件数）(1~ 16)
    uint8_t access_log_target_reg;  // 访问日志存储目标寄存器类型（01:D寄存器，02:R寄存器）
    uint16_t access_log_index;      // 软元件范围（400:9010，高低互换为0190）
    // 设置时间设置结果的存储目标类型 
    uint8_t time_target_cnt;    // 使能  
    uint8_t time_target_reg;    // 时间设置结果存储目标寄存器类型（01:D寄存器，02:R寄存器）
    uint16_t time_range;        // 软元件范围（800:0x0320，高低互换）
    uint16_t end_marker[2];     // 结束符（FFFF FFFF）
}Log_data;

/*
 * SNTP 功能配置位域（对应 sntp_enable 低8位）
 * 用 union 统一访问: .raw 读写整字节, .bits.xxx 按位访问
 *
 * Bit7 (0x80): enable   → bits.enable    0=未启用  1=启用
 * Bit6 (0x40): startup  → bits.startup   0=关闭    1=勾选电源启动
 * Bit5 (0x20): on_error → bits.on_error  0=停止执行 1=继续执行
 * Bit4 (0x10): exec_mode→ bits.exec_mode 0=间隔执行 1=定时执行
 * Bit3-0:     reserved → bits.reserved  保留
 */
typedef union {
    uint8_t raw;           /* 原始字节值, 用于整体比较/赋值 */
    struct {
        uint8_t reserved  : 4;  /* [3:0] 保留 */
        uint8_t exec_mode : 1;  /* [4]   0=间隔执行, 1=定时执行   */
        uint8_t on_error  : 1;  /* [5]   0=停止执行, 1=继续执行   */
        uint8_t startup   : 1;  /* [6]   0=关闭,     1=电源启动   */
        uint8_t enable    : 1;  /* [7]   0=未启用,   1=启用      */
    } bits;
} sntp_cfg_t;

typedef struct SNTP_time_t      // 时间设置
{
    uint16_t sntp_enable;         // SNTP功能设置：0-不使用，1-使用
    uint16_t reserved_ffff;       // 保留字节 0xffff
    int16_t GMT_Time_Hour;        // 时区（GMT偏移）小时
    int16_t GMT_Time_Minute;      // 时区（GMT偏移）分钟
    uint8_t sntp_server_ip[4];    // SNTP服务器IP地址（IPv4格式）
    uint16_t reserved_7b00;       // 保留字节 0x7B00
    uint16_t execution_interval;  // 执行间隔 0100 不启用 其他数据则启用
    uint16_t execution_time[2];   // 执行时间（格式：HH:MM，例如12:00）

} SNTP_time;

typedef struct process_Info_t  
{
    uint8_t reserved_02;        // 保留字节 0x02
    uint8_t protocol_type;      // 协议类型：A0 :tcp melsoft链接 A6 :TCP MC协议 A7:UDP MC协议   A8 :TCP 数据监控 
    uint16_t local_port;        // 本站端口号
    uint8_t  remote_ip[4];      // 通信对象IP地址（IPv4格式）
    uint16_t remote_port;       // 通信对象端口号
    uint16_t reserved;          // 保留字节 0xffff
} process_Info;

typedef struct Net_work_t
{
    uint8_t Local_IP[4];            //IP 地址    D8493 D8492 ==> 192.168.010.100 
    uint8_t Subnet_Mask[4];         //子网掩码   D8495 D8494 ==> 255.255.000.000   
    uint8_t Gateway[4];             //网关(默认路由器IP地址)       D8497 D8496 ==> 192.168.010.001  
} Network_t;

typedef struct ethernet_ch_t
{
    // 前一包内 字节 
    uint16_t ch1_en[12];             //选择通道1 0110  0084  0BB8  FFFE  FFFF  FFFF  FFFF  FFFF FFFF FFFF
    uint16_t ch2_en[12];             //选择通道2 0110  0084  0BB8  FFFE  FFFF  FFFF  FFFF  FFFF FFFF FFFF
    uint16_t reserved_1[3];          //备用数据  FFFF  FFFF  FFFF   
} ethernet_ch; 

typedef struct ethernet_Info_t
{
    // 后一包内 字节 
    uint16_t reserved_2[9];          //备用数据 FFFF  FFFF  FFFF  FFFF  FFFF FFFF FFFF
    uint16_t reserved_3[8];          //备用数据  B80B  6400  7800  2299  0000  0000  7400  6400    (未破解其含义)
    uint16_t reserved_4[4];          //备用数据  264C  0000  C9FF  FFFF  (未破解其含义)
 
    Network_t net_info;              // 网络信息 IP 地址 子网掩码 网关(默认路由器IP地址)  (12 byte)  
    uint16_t Unknown_data_2[6];      // 未知数据 7D01 0A00 9001 3200 3200 0800  (未破解其含义)  (12 byte)  
    process_Info process[4];         // 协议配置数组（最多支持4个协议）
    SNTP_time    sntp_time;          // 时间设置
    Log_data     log_data;           // 日志记录数据

} ethernet_Info;

typedef struct eth_ee50000_t
{
    uint16_t default_header[8];        // 0xB80B,0x7A00,0x957E,0x2EFA,0x0501,0x0000,0x0000,0x0000, 帧头 (16 byte)
    uint16_t reserved_1[6];            // 0x0A00,0x0F00,0x00FC,0x0000,0x0077,0x0100, 可能标志位    (12 byte)
    Network_t net_info;                // 网络信息 IP 地址 子网掩码 网关(默认路由器IP地址)    (12 byte)
    uint16_t  net_mac[4];              // MAC地址 0x0000,0x0000,0x0000,0x0000,            (8 byte)  
    /******************* 分包发送 到PLC * (前48byte 上电发送一次)****************************/
    eth_link_t link_status[6];         // 各连接状态 6个连接   ( 16 *6 = 96 byte )
    eth_packet_t eth_packet;           // 数据包总数        ( 16 byte )
    /******************* 分包发送 到PLC * (后 16 byte 上电发送一次)****************************/
    uint16_t version[4];               // 版本号(BCD码, 1.22 = 0x01 22) 0x1002,0x0000,0x2002,0x0001 ( 8 byte)
    uint16_t default_tail[4];          // 数据包尾识别码  0xFF02,0x4002,0xB80B,0x7A00, ( 8 byte)

}ETH_EE5000;

#pragma pack ()           //取消自定义字节对齐方式

#define E5000_PACKET1_SIZE          (48)     
#define E5000_PACKET2_SIZE          (112)
#define E5000_PACKET3_SIZE          (16)

// 保存以太网适配器状态。
typedef union
{
    int16_t s16val;     //有符号16位   
    uint16_t u16val;    //无符号16位
    uint8_t u8val[2];   //无符号8位
    int8_t s8val[2];    //有符号8位
    struct
    {
        /* b8～b11:连接号1～4 0:关闭中1:打开中*/
        uint8_t b8          : 1;    //连接号1    0:关闭中1:打开中
        uint8_t b9          : 1;    //连接号2    0:关闭中1:打开中
        uint8_t b10         : 1;    //连接号3    0:关闭中1:打开中
        uint8_t b11         : 1;    //连接号4    0:关闭中1:打开中
        uint8_t b12         : 1;    //连接号5    0:关闭中1:打开中
        uint8_t b13         : 1;    //连接号6    0:关闭中1:打开中
        uint8_t b14         : 1;    //连接号7    0:关闭中1:打开中
        uint8_t b15         : 1;    //连接号8    0:关闭中1:打开中
        
        // 高16位/低16位互换 
        uint8_t init_b0     : 1;// b0:INIT 1:初始化处理正常结束0:－
        uint8_t b1          : 1;
        uint8_t speed_b2    : 1;// b2:100M/10M 1:100Mbps0:10Mbps/未链接时
        uint8_t set_err_b3  : 1;// b3:ERR. 1:设置异常显示0:设置正常显示
        uint8_t com_err_b4  : 1;// b4:COM.ERR. 1:通信异常显示0:通信正常显示
        uint8_t init_err_b5 : 1;// b5: 1:初始化处理异常结束0:－
        uint8_t b6          : 1;
        uint8_t link_b7     : 1;// b7: 1:Link信号ON0:Link信号OFF


    } bit;
}ETH_status;

// 通过用户程序强制连接无效时指定。 (连接1～4/MELSOFT/直接连接)
typedef union
{
    int16_t s16val;     //有符号16位   
    uint16_t u16val;    //无符号16位
    uint8_t u8val[2];   //无符号8位
    int8_t s8val[2];    //有符号8位
    struct
    {
        uint8_t b8          : 1;
        uint8_t b9          : 1;
        uint8_t b10         : 1;    //  b10:MELSOFT通信端口
        uint8_t b11         : 1;
        uint8_t b12         : 1;
        uint8_t b13         : 1;    //  b13:直接连接MELSOFT 0:有效(初始值) 1:无效
        uint8_t b14         : 1;
        uint8_t b15         : 1;

        // 高16位/低16位互换 
        uint8_t b0         : 1;    //     b 0:连接1
        uint8_t b1         : 1;    //     b 1:连接2
        uint8_t b2         : 1;    //     b 2:连接3
        uint8_t b3         : 1;    //     b 3:连接4
        uint8_t b4         : 1;    //     
        uint8_t b5         : 1;    //     
        uint8_t b6         : 1;    //     
        uint8_t b7         : 1;    //  
    } bit;

}ETH_connect;

typedef enum ETH_TYPE_t           // 网络类型： TCPS  TCPC  UDPS  UDPC  HTTPS
{
    ETH_TYPE_NULL = 0x00,
    ETH_TYPE_TCP = 0x01,
    ETH_TYPE_UDP = 0x02,
}ETH_Type;

typedef enum PRO_TYPE_t           // 网络类型： TCPS  TCPC  UDPS  UDPC  HTTPS
{
    PRO_TYPE_NULL = 0x00,
    PRO_TCPC_MELSOFT = 0xA0,      //tcp melsoft链接
    PRO_TCPC_MC = 0xA6,           //TCP MC协议
    PRO_UDPC_MC = 0xA7,           //UDP MC协议
    PRO_TCP_HTTP = 0xA8           //TCP 数据监控
}PRO_Type;


/* 连接状态定义 */
typedef enum {
    NET_STATUS_TCP_NONE = 0,      /* 无连接 */
    NET_STATUS_TCP_LISTEN,        /* 监听中 */
    NET_STATUS_TCP_ESTABLISHED,   /* 连接中 */
    NET_STATUS_TCP_CLOSED,        /* 已关闭 */
    NET_STATUS_TCP_ERROR          /* 错误 */
} net_status_t;

/* 监视状态 */
typedef enum {
    FX_MONITOR_IDLE = 0,        /* 空闲 */
    FX_MONITOR_RUNNING,         /* 监视执行中 */
    FX_MONITOR_STOPPED          /* 已停止 */
} net_monitor_state_t;

/**
 * @brief 以太网初始化状态机状态枚举
 */
typedef enum NET_INIT_STATE_t
{
    NET_INIT_STATE_START = 0,       // 状态: 初始化开始，获取芯片版本和MAC地址
    NET_INIT_STATE_READ_EE,         // 状态: 从PLC EE存储器读取网络配置信息
    NET_INIT_STATE_WAIT_EE,         // 状态: 等待EE读取完成
    NET_INIT_STATE_SET_E5xx,        // 状态: 等待E5xx设置网络诊断
    NET_INIT_STATE_LIB_INIT,        // 状态: WCHNET库初始化，创建Socket
    NET_INIT_STATE_CONFIG_PROTO,    // 状态: 协议配置数组初始化
    NET_INIT_STATE_WRITE_PHY_CHANGE, // 状态: 写入PHY配置变化
    NET_INIT_STATE_WRITE_NETCFG,    // 状态: 写入网络配置信息到PLC

    NET_INIT_STATE_MainTask,        // 状态: 循环检测net寄存器状态
 
} NET_INIT_STATE;

/**
 * @brief 以太网 出错代码 枚举
 */
typedef enum NET_ERROR_CODE_t
{
    /* 基本错误 */
    NET_ERR_BASE_UNIT_ABNORMAL = 21,           /* 检测出基本单元异常 */
    NET_ERR_BASE_UNIT_ABNORMAL2 = 120,         /* 检测出基本单元异常 */
    
    /* 连接错误 */
    NET_ERR_CONNECTION_ISSUE = 101,            /* 连接器部位连接状态异常 */
    NET_ERR_SPECIFICATION_ISSUE = 102,         /* 以太网适配器规格问题 */
    NET_ERR_POWER_SUPPLY = 103,                /* 电源容量不足 */
    NET_ERR_ROM_ABNORMAL = 104,                /* ROM异常 */
    
    /* 参数错误 */
    NET_ERR_PARAM_SUM = 750,                   /* 参数的求和异常 */
    NET_ERR_PARAM_RANGE = 751,                 /* 参数设定值范围错误 */
    NET_ERR_IP_ADDRESS = 753,                  /* 以太网适配器IP地址的设定值有误 */
    NET_ERR_PORT_OUT_OF_RANGE = 756,           /* MELSOFT连接指定时，本端端口号的设定值为容许范围外 */
    NET_ERR_TCP_PORT_RANGE = 757,              /* 协议(TCP, UDP)指定时，本端端口号的设定值为容许范围外 */
    NET_ERR_DATA_MONITOR_PORT = 758,           /* 数据监视指定时，本端端口号的设定值为容许范围外 */
    NET_ERR_UDP_TARGET_IP = 759,               /* MC协议(UDP)指定时，通讯对象IP地址的设定值有误 */
    NET_ERR_UDP_TARGET_PORT = 760,             /* MC协议(UDP)指定时，通讯对象端口号的设定值为容许范围外 */
    NET_ERR_ROUTER_SUBNET = 761,               /* 指定默认路由器IP地址时，子网掩码的设定值为容许范围外 */
    NET_ERR_ROUTER_SUBNET2 = 762,              /* 指定默认路由器IP地址时，子网掩码的设定值有误 */
    NET_ERR_ROUTER_IP = 763,                   /* 指定默认路由器IP地址时，默认路由器IP地址的设定值有误 */
    NET_ERR_ROUTER_NETWORK = 764,              /* 指定默认路由器IP地址时，以太网适配器的IP地址和默认路由器IP地址不属于同一网络地址 */
    NET_ERR_NO_ROUTER_NETWORK = 765,           /* 未指定默认路由器IP地址时，以太网适配器的IP地址和通讯对象IP地址不属于同一网络地址 */
    NET_ERR_SNTP_SERVER_NETWORK = 766,         /* 未指定默认路由器IP地址时，以太网适配器的IP地址和SNTP服务器IP地址不属于同一网络地址 */
    NET_ERR_SNTP_TIMEZONE = 767,               /* 使用SNTP功能时，时区的设定值为容许范围外 */
    NET_ERR_SNTP_SERVER_IP = 768,              /* 使用SNTP功能时，SNTP服务器IP地址的设定值有误 */
    NET_ERR_SNTP_EXEC_TIME = 769,              /* 使用SNTP功能时，执行时间的设定值为容许范围外 */
    NET_ERR_SNTP_INTERVAL = 770,               /* 使用SNTP功能时，执行间隔的设定值为容许范围外 */
    
    /* 记录错误 */
    NET_ERR_RECORD_DEVICE_TYPE = 771,          /* 记录错误记录的软元件种类指定为容许范围外 */
    NET_ERR_RECORD_COUNT = 772,                /* 记录错误记录时，记录件数的设定值为容许范围外 */
    NET_ERR_RECORD_DEVICE_START = 773,         /* 记录错误记录时，起始软元件的设定值为容许范围外 */
    NET_ERR_ACCESS_RECORD_DEVICE = 774,        /* 记录访问记录的软元件种类指定为容许范围外 */
    NET_ERR_ACCESS_RECORD_COUNT = 775,         /* 记录访问记录时，记录件数的设定值为容许范围外 */
    NET_ERR_ACCESS_RECORD_START = 776,         /* 记录访问记录时，起始软元件的设定值为容许范围外 */
    NET_ERR_TIME_RECORD_DEVICE = 777,          /* 记录时间设置结果的软元件种类指定为容许范围外 */
    NET_ERR_TIME_RECORD_START = 778,           /* 记录时间设置结果时，起始软元件的设定值为容许范围外 */
    NET_ERR_RECORD_OVERLAP = 779,              /* 记录各种记录(错误记录、访问记录、时间设置结果)软元件的使用范围重复 */
    NET_ERR_PORT_CONFLICT = 780,               /* 本端端口号的设定有误 */
    
    /* 通信错误 */
    NET_ERR_CABLE_DISCONNECTED = 815,          /* 因电缆未连接/断线，无法进行发送处理 */
    NET_ERR_SNTP_NO_RESPONSE = 850,            /* 未能接收SNTP服务器的响应 */
    NET_ERR_TCP_RECEIVE = 911,                 /* 在TCP/IP通信时发生接收出错 */
    NET_ERR_UDP_RECEIVE = 912,                 /* 在UDP/IP通信时发生接收出错 */
    NET_ERR_TCP_SEND = 1013,                   /* 在TCP/IP通信时发生发送出错 */
    NET_ERR_UDP_SEND = 1014,                   /* 在UDP/IP通信时发生发送出错 */
    NET_ERR_CABLE_DISCONNECTED2 = 1015,        /* 因电缆未连接/断线，无法进行发送处理 */
    NET_ERR_COMM_LINE_CLOSED = 1016,           /* 因通信线路关闭，无法进行发送处理 */
    NET_ERR_DATA_LENGTH = 1117,                /* 数据长度超出容许范围 */
    
    /* 协议错误 */
    NET_ERR_ASCII_CONVERSION = 2550,           /* 在以太网适配器的操作设置中设置ASCII码通信时，接收了无法转换为二进制码的ASCII码数据 */
    NET_ERR_DEVICE_TYPE = 2551,                /* 软元件的指定有误(软元件种类为预想外) */
    NET_ERR_DEVICE_ACCESS = 2552,              /* 软元件的指定有误(向位软元件以外读出/写入单位位) */
    NET_ERR_DEVICE_RANGE = 2553,               /* 软元件的指定有误(向C200～C255的访问，点数指定为奇数) */
    NET_ERR_DEVICE_ADDRESS = 2554,             /* 软元件的指定有误(向位软元件的单位位访问时，起始软元件编号不是16的倍数) */
    NET_ERR_DEVICE_RANDOM_WRITE = 2555,        /* 软元件的指定有误(单位随机写入时，指定C200～C255) */
    NET_ERR_POINT_RANGE = 2556,                /* 读出/写入点数在容许范围外 */
    NET_ERR_COMMAND_ERROR = 2557,              /* 命令、子命令的指定有误 */
    NET_ERR_MAX_ADDRESS = 2558,                /* 超过最大地址的读出/写入请求 */
    NET_ERR_RESPONSE_TIMEOUT = 2559,           /* 在响应监视定时器值以内未能接收响应 */
    NET_ERR_PC_NUMBER = 2560,                  /* PC编号有误 */
    NET_ERR_HTTP_REQUEST = 2650,               /* HTTP请求异常 */
    
    /* 系统错误 */
    NET_ERR_NO_RESPONSE = 10032,               /* 以太网适配器未能接收到对象设备的发送内容 */
    NET_ERR_SEND_INTERRUPT = 10166,            /* 中断以太网适配器发送 */
    NET_ERR_SYSTEM_COMM_FAILURE = 20357,       /* 系统出错(与基本单元通信失败) */
    NET_ERR_SYSTEM_COMM_FAILURE2 = 20852,      /* 系统出错(与基本单元通信失败) */
    NET_ERR_SYSTEM_COMM_FAILURE3 = 21251,      /* 系统出错(与基本单元通信失败) */
    NET_ERR_SYSTEM_COMM_FAILURE4 = 21751,      /* 系统出错(与基本单元通信失败) */
} NET_ERROR_CODE;



typedef struct ETH_SOCKET_Type  
{
    uint8_t   EN;                 // 0-不启用 1-启用
    uint8_t   SOCKET_ID;          // 网络 SOCKET ID 
    ETH_Type  Eth_Type;           // 网络类型：1 TCPS  2 UDPS 
    PRO_Type  Pro_Type;           // 协议类型： A0 :tcp melsoft链接 A6 :TCP MC协议 A7:UDP MC协议   A8 :TCP 数据监控 
    uint8_t   Pro_ID;             //  0:tcp melsoft链接 1:TCP MC协议 2:UDP MC协议  3 :TCP 数据监控 
    
    uint16_t local_port;         // 本站端口号
    uint8_t  destip[4];          // 目标 IP地址
    uint16_t destport;           // 目标 端口号

    uint8_t  is_processed;       // 检查是否已存在相同的协议类型和端口号组合 0-不相同未处理 1-相同已处理
    uint8_t  SocketIdForListen;  // Socket for Listening

    net_status_t  net_stat;       // 网络状态 0:未连接 1:已连接

    uint32_t net_rx_packets;        /* net接收包数 */
    uint32_t net_tx_packets;        /* net发送包数 */   

    uint16_t  Error_Code;          // 错误代码
    uint16_t  reserved;            // 保留字节 0xffff

} ETH_SOCKET;

extern SNTP_time    sntp_time;        // 时间设置
extern Log_data     log_data;         // 日志记录数据
extern ETH_SOCKET   eth_socket[8];    // 网络 SOCKET 数组
extern net_monitor_state_t net_monitor_state; // 监视状态
extern ETH_status   net_status;       // 网络使能状态
extern ETH_connect  net_connect;      // 网络连接状态


void FX_ENETINF_UpdateMonitor(void);
net_monitor_state_t FX_ENETINF_GetMonitorState(void);
void FX_ENETINF_SetMonitorState(net_monitor_state_t state);

void ethernet_app_loopback(void);
void ethernet_info_handler( uint8_t *buf, uint16_t len ,uint32_t offset_addr );
void WCHNET_Create_Socket_info( void );
 
void wizchip_updata_socket_to_PLC(uint8_t Sour_Sock,uint8_t link_state);

void wizchip_sntp_get_D8013_PLC(void);

void wizchip_sntp_execute_D8013_PLC(void);

void wizchip_Analysis_M_info(uint8_t *buf, uint16_t len, uint32_t start_dev);

void wizchip_get_PLC_M8404_info(void);

void wizchip_Analysis_D_info(uint8_t *buf, uint16_t len,uint32_t start_dev);

void wizchip_PHY_Link_PLC(uint8_t link_status);

void ethernet_connect_set( uint8_t Sour_Sock_id , uint8_t *destip, uint16_t destport ,uint8_t state );

void synch_time_handler(void);

uint32_t synch_time_get(void);

void ethernet_send(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t *buf, uint16_t len, uint8_t *destip, uint16_t destport);

void ethernet_error_code_ack(uint8_t Sour_Sock, uint8_t Dest_Sock,
                             uint8_t sub_header, uint8_t code );

int  process_Discover_device(uint8_t Sour_Sock ,uint8_t  Dest_Sock,uint8_t* frame_buff, uint16_t frame_len, uint8_t * destip, uint16_t destport);

int  Analysis_eth_frame_handler(uint8_t Sour_Sock ,uint8_t Dest_Sock, uint8_t* frame_buff, uint16_t frame_len, uint8_t * ipaddr, uint16_t port); 

int sim_Process_switch_MELSOFT(uint8_t *buf,uint16_t len);

void sim_Process_switch(uint8_t *buf,uint16_t len);

/* MELSOFT 快速通道: 串口原始数据去校验位后直接透传到网口，跳过帧解析 */
int uartMelsoftFastPath(uint8_t *buf, uint32_t len, uint8_t Dest_Sock);

void ethernet_init(void);

void Wizchip_PHY_Link_Disconnect(void);

void ethernet_app_task(void);

/**
 * @brief   发送构造的TCP数据包（IP Raw模式）
 * @param   sn          Socket 编号
 * @param   src_ip      源IP地址（4字节数组）
 * @param   dst_ip      目标IP地址（4字节数组）
 * @param   src_port    源端口号
 * @param   dst_port    目标端口号
 * @param   seq_num     TCP序列号
 * @param   ack_num     TCP确认号
 * @param   tcp_flags   TCP标志位（0x01=FIN, 0x02=SYN, 0x08=PSH, 0x10=ACK等）
 * @param   data        数据载荷指针
 * @param   data_len    数据载荷长度
 * @return  0: 失败; 1: 成功
 */
uint8_t send_constructed_tcp_packet(uint8_t sn, uint8_t *src_ip, uint8_t *dst_ip,
                                     uint16_t src_port, uint16_t dst_port,
                                     uint32_t seq_num, uint32_t ack_num,
                                     uint8_t tcp_flags, uint8_t *data, uint16_t data_len);

/**
 * @brief   发送预构造的TCP数据包
 * @param   sn Socket编号
 * @return  0: 失败; 1: 成功
 * @note    对应数据包：D4 93 90 1E 71 00 28 E9 8E 29 D0 BF 08 00 45 00 00 28 BF 57 00 00 40 06 36 4D C0 A8 01 FC C0 A8 01 DF 15 B4 C3 18 55 3B 46 39 A2 73 53 34 50 10 16 C3 A9 FC 00 00 00 00 00 00 00 00
 */
uint8_t send_predefined_tcp_packet(uint8_t sn);

 
 
#endif    //ethernet_app
