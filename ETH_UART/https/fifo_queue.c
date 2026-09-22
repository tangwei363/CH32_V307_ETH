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
#include "ch32v30x.h"   /* __disable_irq()/__enable_irq()：索引更新需成组完成（写者可能在事件上下文） */

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
    /* queue->size == 0 必须拒绝：后续 head % size 会除零，据此算出的偏移是野指针 */
    if (queue == NULL || record == NULL || queue->records == NULL || queue->size == 0) {
        return;
    }
    
    /* ① 先把记录体写入目标槽位：此刻 head/count 还没动，读者（按 count 判定）
     *    不会引用到"还没写完"的槽位。 */
    uint16_t offset = (uint16_t)queue->head * queue->item_size;
    memcpy((uint8_t *)queue->records + offset, record, queue->item_size);

    /* ② 索引更新必须成组完成：本函数可能由 socket 事件上下文调用，而主循环里的
     *    "渲染/上传"正在读 head/tail/count/read_pos，非原子会读到"半更新"的环
     *    （现象：履历重复行/跳号/顺序错乱，且难以复现）。
     *    注：本工程的生产者与消费者都在主循环，这里的临界区是防御性措施。 */
    __disable_irq();
    queue->head = (queue->head + 1) % queue->size;
    /* 如果队列已满，更新尾部指针（替换最旧的记录） */
    if (queue->count >= queue->size) {
        queue->tail = (queue->tail + 1) % queue->size;
    } else {
        queue->count++;
    }
    /* 重置读取位置到最新记录 */
    queue->read_pos = (uint8_t)((queue->head - 1 + queue->size) % queue->size);
    __enable_irq();
}

/*********************************************************************
 * @fn      FIFO_GetByAge
 *
 * @brief   按"龄"读取记录（age 0 = 最新），不改变任何游标
 *
 * @param   queue - 队列结构体指针
 *          age - 0 = 最新，1 = 次新 …（age >= count 时返回 0）
 *          record - 输出缓冲（>= item_size）
 *
 * @return  1 - 成功；0 - 无该龄记录或参数非法
 */
uint8_t FIFO_GetByAge(fifo_queue_t *queue, uint8_t age, void *record)
{
    uint8_t head, size, count, idx;

    if (queue == NULL || record == NULL || queue->records == NULL || queue->size == 0u) {
        return 0;
    }

    /* 三个索引必须"同一时刻"读取（写者可能在事件上下文更新它们） */
    __disable_irq();
    head  = queue->head;
    size  = queue->size;
    count = queue->count;
    __enable_irq();

    if (age >= count) {
        return 0;                                       /* 该龄还没有记录 */
    }
    /* (head - 1 - age) 取模：用 uint16 中间量，避免 uint8 溢出（head/size 接近 255 时） */
    idx = (uint8_t)(((uint16_t)head + size - 1u - age) % size);
    memcpy(record, (uint8_t *)queue->records + (uint16_t)idx * queue->item_size,
           queue->item_size);
    return 1;
}

/* 说明：曾计划提供 FIFO_GetOldestAge()（返回 count-1）供页面决定"要画几行"，
 * 但表格需要固定 8 行（不足的行用空行补齐），用不到该信息，故未保留。 */

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
    /* read_pos >= size 为"已读完"哨兵（合法下标只有 0..size-1） */
    if (queue->read_pos >= queue->size) {
        return 0;
    }
    
    /* 计算当前读取位置的偏移量 */
    uint16_t offset = (uint16_t)queue->read_pos * queue->item_size;
    
    /* 复制当前读取位置的记录 */
    memcpy(record, (uint8_t *)queue->records + offset, queue->item_size);
    
    /* 更新读取位置到下一条记录（向尾部方向移动）
     * 原实现到达最旧记录后仍 return 1 且不推进 read_pos，调用方会反复拿到
     * 同一条旧记录、且永远等不到"读完"（表格被重复行填满 / while 无法退出）。
     * 这里读完即置哨兵 read_pos = size，下一次调用返回 0 表示结束。 */
    if (queue->read_pos == queue->tail) {
        queue->read_pos = queue->size;      /* 哨兵：本队列已读完 */
    } else {
        queue->read_pos = (queue->read_pos - 1 + queue->size) % queue->size;
    }
    return 1;
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
    /* 原写法 "queue == NULL && count > 0" 有两处错误：
     *   ① 队列非空时第一个条件为假 → 读取位置永不重置，页面与回写每次都从
     *      上次读完的位置继续，拿到的不是"最新"记录；
     *   ② 传入 NULL 时会继续求值 queue->count → 空指针解引用。
     * 正确语义：NULL 立即返回；非空则把读取位置重置到最新一条。 */
    if (queue == NULL) {
        return;
    }
    if (queue->count > 0 && queue->size > 0) {
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
