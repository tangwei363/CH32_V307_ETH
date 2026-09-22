/********************************** (C) COPYRIGHT *******************************
 * File Name          : fifo_queue.h
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/04/03
 * Description        : 通用FIFO队列实现头文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#ifndef __FIFO_QUEUE_H
#define __FIFO_QUEUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/**
 * @brief 通用FIFO队列结构体
 * @note 用于存储各种类型的记录，如错误日志、访问日志等
 */
typedef struct {
    void *records;        /* 记录数组指针 */
    uint8_t item_size;    /* 单个记录的大小 */
    uint8_t size;         /* 队列大小 */
    uint8_t head;         /* 队列头部索引（最新记录位置） */
    uint8_t tail;         /* 队列尾部索引（最旧记录位置） */
    uint8_t count;        /* 当前记录数量 */
    uint8_t read_pos;     /* 读取位置索引 */
} fifo_queue_t;

/* 函数声明 */

/**
 * @brief 初始化FIFO队列
 * @param queue - 队列结构体指针
 * @param records - 记录数组指针
 * @param item_size - 单个记录的大小
 * @param size - 队列大小
 * @return 无
 */
void FIFO_Init(fifo_queue_t *queue, void *records, uint8_t item_size, uint8_t size);

/**
 * @brief 向队列添加记录
 * @param queue - 队列结构体指针
 * @param record - 记录指针
 * @return 无
 */
void FIFO_AddRecord(fifo_queue_t *queue, void *record);

/**
 * @brief 按顺序获取下一条记录（从头到尾）
 * @param queue - 队列结构体指针
 * @param record - 记录指针
 * @return 1 - 成功获取记录，0 - 没有更多记录
 */
uint8_t FIFO_GetNextRecord(fifo_queue_t *queue, void *record);

/**
 * @brief 重置读取位置到最新记录
 * @param queue - 队列结构体指针
 * @return 无
 */
void FIFO_ResetReadPos(fifo_queue_t *queue);

/**
 * @brief 按"龄"读取记录（age 0 = 最新；不改变任何游标，因此可多读者并发）
 * @param queue - 队列结构体指针
 * @param age - 0 = 最新，1 = 次新 …（age >= count 时返回 0）
 * @param record - 输出缓冲（由调用方保证 >= item_size）
 * @return 1 - 成功获取，0 - 该"龄"无记录/参数非法
 * @note 与 FIFO_GetNextRecord 的区别：后者依赖共享的 read_pos 游标（有副作用），
 *       本函数无副作用，适合"渲染/上传"这类可重入的遍历场景。
 */
uint8_t FIFO_GetByAge(fifo_queue_t *queue, uint8_t age, void *record);

/**
 * @brief 清空队列
 * @param queue - 队列结构体指针
 * @return 无
 */
void FIFO_Clear(fifo_queue_t *queue);

/**
 * @brief 获取队列中的记录数量
 * @param queue - 队列结构体指针
 * @return 记录数量
 */
uint8_t FIFO_GetCount(fifo_queue_t *queue);

/**
 * @brief 获取队列大小
 * @param queue - 队列结构体指针
 * @return 队列大小
 */
uint8_t FIFO_GetSize(fifo_queue_t *queue);

#ifdef __cplusplus
}
#endif

#endif /* __FIFO_QUEUE_H */
