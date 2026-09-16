/********************************** (C) COPYRIGHT *******************************
 * File Name          : fifo_queue.c
 * Author             : AI Assistant
 * Version            : V1.0.0
 * Date               : 2026/04/03
 * Description        : 通用FIFO队列实现源文件
*********************************************************************************
* Copyright (c) 2026 AI Assistant. All rights reserved.
*******************************************************************************/

#include <string.h>
#include "fifo_queue.h"

/*********************************************************************
 * @fn      FIFO_Init
 *
 * @brief   初始化FIFO队列
 *
 * @param   queue - 队列结构体指针
 *          records - 记录数组指针
 *          item_size - 单个记录的大小
 *          size - 队列大小
 *
 * @return  无
 */
void FIFO_Init(fifo_queue_t *queue, void *records, uint8_t item_size, uint8_t size)
{
    if (queue == NULL) {
        return;
    }
    
    queue->records = records;
    queue->item_size = item_size;
    queue->size = size;
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->read_pos = 0;
    
    /* 初始化记录数组 */
    if (records != NULL) {
        memset(records, 0, item_size * size);
    }
}

/*********************************************************************
 * @fn      FIFO_AddRecord
 *
 * @brief   向队列添加记录
 *
 * @param   queue - 队列结构体指针
 *          record - 记录指针
 *
 * @return  无
 */
void FIFO_AddRecord(fifo_queue_t *queue, void *record)
{
    if (queue == NULL || record == NULL || queue->records == NULL) {
        return;
    }
    
    /* 计算当前记录的偏移量 */
    uint16_t offset = (uint16_t)queue->head * queue->item_size;
    
    /* 添加新记录到队列头部 */
    memcpy((uint8_t *)queue->records + offset, record, queue->item_size);
    
    /* 更新队列指针 */
    queue->head = (queue->head + 1) % queue->size;
    
    /* 如果队列已满，更新尾部指针（替换最旧的记录） */
    if (queue->count >= queue->size) {
        queue->tail = (queue->tail + 1) % queue->size;
    } else {
        queue->count++;
    }
    
    /* 重置读取位置到最新记录 */
    queue->read_pos = (queue->head - 1 + queue->size) % queue->size;
}

/*********************************************************************
 * @fn      FIFO_GetNextRecord
 *
 * @brief   按顺序获取下一条记录（从头到尾）
 *
 * @param   queue - 队列结构体指针
 *          record - 记录指针
 *
 * @return  1 - 成功获取记录，0 - 没有更多记录
 */
uint8_t FIFO_GetNextRecord(fifo_queue_t *queue, void *record)
{
    if (queue == NULL || record == NULL || queue->records == NULL || queue->count == 0) {
        return 0;
    }
    
    /* 计算当前读取位置的偏移量 */
    uint16_t offset = (uint16_t)queue->read_pos * queue->item_size;
    
    /* 复制当前读取位置的记录 */
    memcpy(record, (uint8_t *)queue->records + offset, queue->item_size);
    
    /* 更新读取位置到下一条记录（向尾部方向移动） */
    if (queue->read_pos == queue->tail) {
        // 已经到达最旧的记录
        return 1;
    } else {
        queue->read_pos = (queue->read_pos - 1 + queue->size) % queue->size;
        return 1;
    }
}

/*********************************************************************
 * @fn      FIFO_ResetReadPos
 *
 * @brief   重置读取位置到最新记录
 *
 * @param   queue - 队列结构体指针
 *
 * @return  无
 */
void FIFO_ResetReadPos(fifo_queue_t *queue)
{
    if (queue == NULL && queue->count > 0) {
        queue->read_pos = (queue->head - 1 + queue->size) % queue->size;
    }
}

/*********************************************************************
 * @fn      FIFO_Clear
 *
 * @brief   清空队列
 *
 * @param   queue - 队列结构体指针
 *
 * @return  无
 */
void FIFO_Clear(fifo_queue_t *queue)
{
    if (queue == NULL) {
        return;
    }
    
    /* 清空记录数组 */
    if (queue->records != NULL) {
        memset(queue->records, 0, queue->item_size * queue->size);
    }
    
    /* 重置队列指针 */
    queue->head = 0;
    queue->tail = 0;
    queue->count = 0;
    queue->read_pos = 0;
}

/*********************************************************************
 * @fn      FIFO_GetCount
 *
 * @brief   获取队列中的记录数量
 *
 * @param   queue - 队列结构体指针
 *
 * @return  记录数量
 */
uint8_t FIFO_GetCount(fifo_queue_t *queue)
{
    if (queue == NULL) {
        return 0;
    }
    return queue->count;
}

/*********************************************************************
 * @fn      FIFO_GetSize
 *
 * @brief   获取队列大小
 *
 * @param   queue - 队列结构体指针
 *
 * @return  队列大小
 */
uint8_t FIFO_GetSize(fifo_queue_t *queue)
{
    if (queue == NULL) {
        return 0;
    }
    return queue->size;
}
