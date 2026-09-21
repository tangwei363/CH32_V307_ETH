/********************************** (C) COPYRIGHT *******************************
 * File Name          : html_components.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/03/20
 * Description        : HTML共享组件头文件 - 用于减少ARM单片机内存占用
 *********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __HTML_COMPONENTS_H__
#define __HTML_COMPONENTS_H__

#include <stdint.h>

#include "melsec_fx_core.h"
#include "melsec_fx_net.h"
#include "melsec_fx_tables.h"

 
/* 页面类型枚举 */
typedef enum {
    HTML_PAGE_INDEX = 0,
    HTML_PAGE_DEVMON = 1,
    HTML_PAGE_PLCINF = 2,
    HTML_PAGE_ENETINF = 3,
    HTML_PAGE_STATUS = 4,
    HTML_PAGE_ACCLOG = 5,
    HTML_PAGE_COUNT
} html_page_type_t;

/* HTML组件类型 */
typedef enum {
    HTML_COMP_HEADER = 0,           /* HTML头部 - DOCTYPE到<head> */
    HTML_COMP_CSS_NEW ,             /* 新版CSS样式 (index.c) */
    HTML_COMP_CSS_RESPONSIVE ,      /* 响应式CSS样式 (fx_acclog.c等) */
    HTML_COMP_BODY_START_NEW ,      /* <body>开始 (新版) */
    HTML_COMP_LANG_BAR_NEW ,        /* 新版语言选择栏 */
    HTML_COMP_LANG_BAR_RESPONSIVE ,  /* 响应式语言选择栏 */
    HTML_COMP_NAV_BAR_NEW ,         /* 新版导航栏 */
    HTML_COMP_NAV_BAR_RESPONSIVE ,  /* 响应式导航栏 */
    HTML_COMP_FORM_START ,          /* 表单开始 */
    HTML_COMP_FOOTER_NEW ,          /* 新版页脚 */
    HTML_COMP_BODY_END ,            /* HTML结束标签 */
    HTML_COMP_REFRESH ,             /* 自动刷新标签 (用于需要自动刷新的页面) */
    HTML_COMP_COUNT
} html_component_type_t;

/* 状态枚举 */
typedef enum {
    HTML_STATE_IDLE = 0,
    HTML_STATE_RUNNING = 1,
    HTML_STATE_STOPPED = 2
} html_state_t;

/* 获取组件内容 */
const char* HTML_GetComponent(html_component_type_t comp_type);

/* 获取状态字符串 */
const char* HTML_GetStateString(html_state_t state);


#endif /* __HTML_COMPONENTS_H__ */
