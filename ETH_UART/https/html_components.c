/********************************** (C) COPYRIGHT *******************************
 * File Name          : html_components.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/20
 * Description        : HTML共享组件实现 - 用于减少ARM单片机内存占用
 *********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include "html_components.h"
#include "HTTPS.h"      /* 取 HTML_LEN：用于下面分包缓冲容量的编译期校验 */
#include <string.h>
#include <stdio.h>

/* ============== HTML共享组件定义 ============== */

/* HTML头部 (共用) */
static const char HTML_Component_Header[] =
    "<!DOCTYPE html>\r\n"
    "<html>\r\n"
    "<head>\r\n"
    "<meta http-equiv=\"Content-Type\" content=\"text/html; charset=GBK\">\r\n"
    "<title>%s</title>\r\n"
    "<meta http-equiv=\"pragma\" content=\"no-cache\">\r\n"
    "<meta http-equiv=\"cache-control\" content=\"no-cache\">\r\n"
    "<meta http-equiv=\"Expires\" content=\"0\">\r\n";

/* 自动刷新标签 */
static const char HTML_Component_Refresh[] =
    "<meta http-equiv=\"refresh\" content=\"5\">\r\n";
 
/* 新版CSS样式 (index.c使用) - 仅保留居中和自适应功能 */
static const char HTML_Component_CSS_New[] =
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">\r\n"
    "<style>*{margin:0;padding:0;box-sizing:border-box}body{padding:10px;font-family:Arial,sans-serif;text-align:center}.c{max-width:1200px;margin:0 auto}.l{padding:8px 15px;margin-bottom:10px;text-align:right}.l a{text-decoration:none;padding:5px 10px;margin:0 2px}.l a:hover{font-weight:bold}.n{padding:10px;margin-bottom:15px;text-align:center}.n a{text-decoration:none;padding:6px 12px;margin:3px;display:inline-block}.n a:hover{font-weight:bold}.x{padding:30px 20px;text-align:center;margin-bottom:10px}.c1{font-size:20px;font-style:italic;margin:15px 0}.p{font-size:32px;font-style:italic;margin:20px 0}.t{font-size:22px;font-style:italic;margin:25px 0}.f{font-size:11px;text-align:center;padding:15px 0;border-top:1px solid}.inf{background-color:#ffffff;border-style:inset;border-width:2px}.ledr{background-color:#ff0000;border-style:solid;border-color:#000000;border-width:2px}.ledg{background-color:#00ff00;border-style:solid;border-color:#000000;border-width:2px}.off{background-color:#ffffff;border-style:solid;border-color:#000000;border-width:2px}.ct{background-color:#cccccc}table{margin-left:auto;margin-right:auto;text-align:left}</style></head>\r\n";

/* 响应式CSS样式 (fx_acclog.c等使用) - 完整的响应式设计 */
static const char HTML_Component_CSS_Responsive[] =
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1,maximum-scale=1,user-scalable=0\">\r\n"
    "<style>*{box-sizing:border-box;margin:0;padding:0}body{font-family:Arial,Helvetica,sans-serif;padding:10px 0;min-height:100vh}.wrapper{min-height:calc(100vh - 60px)}.container{max-width:1200px;width:94%;margin:0 auto}.lang-bar{padding:10px 15px;margin-bottom:15px;display:flex;justify-content:flex-end;gap:10px;align-items:center}.lang-bar a{text-decoration:none;padding:6px 12px;border:1px solid;font-size:12px}.lang-bar a:hover{font-weight:bold}.nav-bar{padding:10px 15px;margin-bottom:20px;display:flex;justify-content:center;gap:8px;flex-wrap:wrap;font-size:12px}.nav-bar a{text-decoration:none;padding:6px 12px;font-weight:500}.nav-bar a:hover{font-weight:bold}.content{padding:30px;margin-bottom:20px}.page-title{font-size:24px;font-weight:700;margin-bottom:25px;text-align:center}.control-panel{padding:20px;margin-bottom:25px;display:flex;align-items:center;justify-content:flex-end;gap:15px;flex-wrap:wrap}.status-label{font-size:14px;font-weight:500}.status-value{font-size:16px;font-weight:700;padding:6px 15px;border:1px solid}.btn{padding:10px 25px;border:1px solid;font-size:14px;font-weight:600;cursor:pointer}.btn-start{font-weight:bold}.btn-stop{font-weight:bold}.table-wrapper{overflow-x:auto}.access-table{width:100%;border-collapse:collapse;font-size:14px}.access-table th{border:1px solid;padding:12px 8px;text-align:center;font-weight:600;white-space:nowrap}.access-table td{padding:10px 8px;text-align:center;border:1px solid}.access-table .row-header{font-weight:600}.access-table .row-new{font-weight:700}.footer{font-size:12px;text-align:center;padding:20px 0;border-top:2px solid;margin-top:20px}@media screen and (max-width:768px){.container{width:96%}.lang-bar{padding:8px 12px;flex-wrap:wrap;justify-content:center}.lang-bar a{font-size:11px;padding:5px 10px}.nav-bar{gap:6px;padding:8px 12px}.nav-bar a{padding:5px 10px;font-size:11px}.content{padding:20px}.page-title{font-size:20px}.control-panel{flex-direction:column;align-items:stretch}.status-value{text-align:center}.btn{width:100%}.access-table{font-size:12px}.access-table th,.access-table td{padding:8px 4px}}@media screen and (max-width:480px){.access-table{font-size:11px}.access-table th,.access-table td{padding:6px 3px}.page-title{font-size:18px}.lang-bar a{font-size:10px;padding:4px 8px}.nav-bar a{font-size:10px;padding:4px 8px}}</style></head>\r\n";


/* Body开始标签 (新版) */
static const char HTML_Component_Body_Start_New[] =
    "<body bgcolor=\"#cccccc\" style=\"margin-bottom:0px\">\r\n";



/* 新版导航栏 (index.c) */
static const char HTML_Component_Nav_Bar_New[] =
    "<table rules=\"all\" cellspacing=\"0\" cellpadding=\"0\" style=\"table-layout:fixed; font-size:12px; margin:0 auto;\"><tbody><tr><td align=\"center\" width=\"60\"><a href=\"index.html?LANG=ZS\">主页</a></td><td align=\"center\" width=\"220\"><a href=\"fx_devmon.html?LANG=ZS\">软元件/缓冲存储器批量监视</a></td><td align=\"center\" width=\"110\"><a href=\"fx_plcinf.html?CMD=%BC%E0%CA%D3%BF%AA%CA%BC&amp;LANG=ZS\">PLC信息</a></td><td align=\"center\" width=\"180\"><a href=\"fx_enetinf.html?CMD=%BC%E0%CA%D3%BF%AA%CA%BC&amp;LANG=ZS\">FX3U-ENET-ADP信息</a></td><td align=\"center\" width=\"140\"><a href=\"fx_status.html?CMD=%BC%E0%CA%D3%BF%AA%CA%BC&amp;LANG=ZS\">通信状态</a></td><td align=\"center\" width=\"90\"><a href=\"fx_acclog.html?CMD=%BC%E0%CA%D3%BF%AA%CA%BC&amp;LANG=ZS\">访问履历</a></td></tr></tbody></table>\r\n";

/* ── 编译期防呆：分包缓冲容量校验（越界会写穿 BSS 导致死机）────────────
 * CSS 与导航栏组件已改为"绕过 HtmlBuffer 直发"(见各页面 SendWebPage)，
 * 因此这里只校验仍然 sprintf 进 HtmlBuffer[HTML_LEN] 的 HTML 头部与刷新块。
 * 历史故障：头部+CSS 超过 HTML_LEN 写穿 BSS，覆盖 http_request/g_data_rows，
 * 表现为页面能显示但随即死机。 */
typedef char HTML_Assert_Header_Fits_HtmlBuffer[
    ((sizeof(HTML_Component_Header) + 64u) <= (unsigned)HTML_LEN) ? 1 : -1];
typedef char HTML_Assert_Refresh_Fits_HtmlBuffer[
    ((sizeof(HTML_Component_Refresh) + 16u) <= (unsigned)HTML_LEN) ? 1 : -1];

/* 响应式导航栏 (fx_acclog.c等使用) */
static const char HTML_Component_Nav_Bar_Responsive[] =
    "<div class=\"nav-bar\">\r\n"
    "<a href=\"index.html?LANG=ZS\">主页</a>\r\n"
    "<a href=\"fx_devmon.html?LANG=ZS\">软元件监视</a>\r\n"
    "<a href=\"fx_plcinf.html?CMD=监视开始&LANG=ZS\">PLC信息</a>\r\n"
    "<a href=\"fx_enetinf.html?CMD=监视开始&LANG=ZS\">ADP信息</a>\r\n"
    "<a href=\"fx_status.html?CMD=监视开始&LANG=ZS\">通信状态</a>\r\n"
    "<a href=\"fx_acclog.html?CMD=监视开始&LANG=ZS\">访问履历</a>\r\n"
    "</div>\r\n";

/* 新版页脚 (index.c) */
static const char HTML_Component_Footer_New[] =
    /* 页脚：分割横线 + 公司信息（全部行内样式，不依赖共享 CSS 是否被浏览器应用） */
    "<hr style=\"width:800px;max-width:96%;margin:18px auto 0 auto;border:0;border-top:1px solid #000000\">\r\n"
    "<div class=\"f\" style=\"font-size:12px;text-align:center;line-height:1.7;padding:12px 0\">020-32382254<br>xq001@gdxq.xyz<br>广州高新技术产业开发区风信路1号110房<br>备案号：粤ICP备2024253175号<br>Copyright &#169; 2024 小崎科技</div>\r\n"
    "</div>\r\n"
    /*
     * SX 客户端：接收端（浏览器）重组与校验辅助。
     *
     * 设备端在每个响应末尾以 HTML 注释形式附带元信息，形如：
     *   <!--SX:PAGE=status;TID=12;CHUNKS=5;BYTES=4021;CRC32=A1B2C3D4-->
     *   （分块本身由 HTTP chunked 协议栈自动重组，这里只做元信息与校验）
     *
     * 本脚本解析该注释并暴露为 window.SX，用途：
     *   1) 查看本次传输的任务号/分块数/负载字节数；
     *   2) 用 SX.crc32(text) 对原始文本复算 CRC-32 做完整性自检；
     *   3) 将来切换为 /api/data JSON 接口时可复用同一校验逻辑。
     *
     * 注意：本字符串经 "%s" 插入，不含格式占位符；JS 中的 \s \S 已按 C 转义写作 \\s \\S。
     */
    "<script>\r\n"
    "(function(){"
    "var w=document.createTreeWalker(document.documentElement,NodeFilter.SHOW_COMMENT,null,false),n,m,s=null;"
    "while(n=w.nextNode()){m=/^\\s*SX:([\\s\\S]*?)\\s*$/.exec(n.nodeValue||\"\");if(m){s=m[1];break;}}"
    "var o={};"
    "if(s){s.split(\";\").forEach(function(kv){var p=kv.split(\"=\");if(p[0]){o[p[0]]=p[1]||\"\";}});}"
    "o.crc32=function(t){var c=0xFFFFFFFF;for(var i=0;i<t.length;i++){"
    "c^=t.charCodeAt(i)&0xFF;for(var k=0;k<8;k++){c=(c>>>1)^(0xEDB88320&(-(c&1)));}}"
    "return((c^0xFFFFFFFF)>>>0).toString(16).toUpperCase();};"
    "window.SX=o;"
    "if(o.CHUNKS){try{console.log(\"[SX] page=\"+o.PAGE+\" tid=\"+o.TID+\" chunks=\"+o.CHUNKS+\" bytes=\"+o.BYTES+\" crc32=\"+o.CRC32);}catch(e){}}"
    "})();\r\n"
    "</script>\r\n"
    "</body></html>";
/* ============== 共享字符串定义 ============== */

/* 状态字符串 */
static const char* HTML_StateStrings[] = {
    "空闲",           /* HTML_STATE_IDLE */
    "监视执行中",     /* HTML_STATE_RUNNING */
    "已停止"          /* HTML_STATE_STOPPED */
};

/* ============== 函数实现 ============== */

/*********************************************************************
 * @fn      HTML_GetComponent
 *
 * @brief   获取指定类型的HTML组件
 *
 * @param   comp_type - 组件类型
 *
 * @return  组件内容指针
 */
const char* HTML_GetComponent(html_component_type_t comp_type)
{
    switch (comp_type) {
        case HTML_COMP_HEADER:
            return HTML_Component_Header;
        case HTML_COMP_CSS_NEW:
            return HTML_Component_CSS_New;
        case HTML_COMP_CSS_RESPONSIVE:
            return HTML_Component_CSS_Responsive;
        case HTML_COMP_BODY_START_NEW:
            return HTML_Component_Body_Start_New;
        case HTML_COMP_NAV_BAR_NEW:
            return HTML_Component_Nav_Bar_New;
        case HTML_COMP_NAV_BAR_RESPONSIVE:
            return HTML_Component_Nav_Bar_Responsive;
        case HTML_COMP_FOOTER_NEW:
            return HTML_Component_Footer_New;
        case HTML_COMP_BODY_END:
            return "</body></html>";
        case HTML_COMP_REFRESH:
            return HTML_Component_Refresh;

        default:
            return "";
    }
}

/*********************************************************************
 * @fn      HTML_GetStateString
 *
 * @brief   获取状态字符串
 *
 * @param   state - 状态枚举
 *
 * @return  状态字符串指针
 */
const char* HTML_GetStateString(html_state_t state)
{
    if (state < 3) {
        return HTML_StateStrings[state];
    }
    return "未知";
}
