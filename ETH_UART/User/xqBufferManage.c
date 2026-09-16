/**
  ******************************************************************************
  * @author  yang feng wu 
  * @version V1.0.0
  * @date    2020/1/28
  * @brief   
  ******************************************************************************
    使用说明:https://www.cnblogs.com/yangfengwu/p/12228402.html
  ******************************************************************************
  */

#define BUFFMANAGE_C_

#include "ch32v30x.h"
#include "xqBufferManage.h"
#include "xqLoopList.h"
#include <stdio.h>

//32位共同体
typedef union
{
    int32_t s32val;         //有符号32位   
    uint32_t u32val;        //无符号32位
    uint16_t u16val[2];     //无符号16位
    uint8_t u8val[4];       //无符号8位
}DATA_UNION;

/* fifo上锁函数 */
static void Manage_fifo_lock(void)
{
    __disable_irq();
}

/* fifo解锁函数 */
static void Manage_fifo_unlock(void)
{
  __enable_irq();
}

/**
* @brief   创建数据缓存管理
* @param   bms           缓存管理结构体变量
* @param   buff          用于缓存数据的数组
* @param   BuffLen       用于缓存数据的数组的长度
* @param   ManageBuff    用于记录每次缓存多少字节的数组
* @param   ManageBuffLen 用于记录每次缓存多少字节的数组长度
* @retval  None
* @warning None
* @example 
**/
void BufferManageCreate(buff_manage_struct *bms,void *buff,uint32_t BuffLen,void *ManageBuff,uint32_t ManageBuffLen)
{
   
    fifo_register(&(bms->Buff),buff,BuffLen,Manage_fifo_lock,Manage_fifo_unlock);
    fifo_register(&(bms->ManageBuff),ManageBuff,ManageBuffLen/4,NULL,NULL);
    
    bms->Count=0;
    bms->Cnt=0;
//    bms->ReadFlage=0;
//    bms->ReadLen=0;
    bms->SendFlage=0;
    bms->SendLen=0;
    bms->value=0;
    bms->JoinFlage=0;
    bms->JoinLen=0;

}


/**
* @brief   写入缓存数据
* @param   bms      缓存管理结构体变量
* @param   buff     写入的数据
* @param   BuffLen  写入的数据个数
* @param   DataLen  返回: 0 Success;1:管理缓存满;2:数据缓存满
* @retval  None
* @warning None
* @example 
**/
uint8_t BufferManageWrite(buff_manage_struct *bms,void *buff,uint32_t BuffLen)
{
    DATA_UNION union_lend = {0};
    union_lend.u32val = BuffLen;                                //记录写入数据个数
    if(fifo_get_free_size(&(bms->Buff))>BuffLen)                //获取 缓存 fifo空闲空间大小
    {
        if(fifo_get_free_size(&(bms->ManageBuff))>4)            //获取 缓存管理 fifo空闲空间大小
        {            
            fifo_write(&(bms->Buff) ,buff, BuffLen);            //写入数据
            if(bms->JoinFlage)
            {
                union_lend.u32val += bms->JoinLen;
                bms->JoinFlage = 0;
                bms->JoinLen = 0;
            }
            fifo_write(&(bms->ManageBuff) ,union_lend.u8val,4); //记录写入数据个数
            return 0;     //0:写入成功
        }
        else{
            return 1;      //1:管理缓存满
        }
    }
    else {
       return  2;          //2:数据缓存满
    }
}

/**
* @brief   写入缓存数据 拼接管理数据
* @param   bms      缓存管理结构体变量
* @param   buff     写入的数据
* @param   BuffLen  写入的数据个数
* @param   DataLen  返回: 0 Success; 2:数据缓存满
* @retval  None
* @warning None
* @example 
**/
uint8_t BufferManage_Join_write(buff_manage_struct *bms,void *buff,uint32_t BuffLen)
{

    if(fifo_get_free_size(&(bms->Buff))>BuffLen)                //获取 缓存 fifo空闲空间大小
    {
        bms->JoinFlage = 1;
        bms->JoinLen += BuffLen;
        fifo_write(&(bms->Buff) ,buff, BuffLen);                //写入数据
        return 0;             //0:写入成功
    }
    else {
       return  2;          //2:数据缓存满
    }
}

/**
* @brief   从缓存中读取数据
* @param   bms      缓存管理结构体变量
* @param   buff     返回的数据地址
* @param   DataLen  读取的数据个数
* @retval  取出的数据个数
* @warning None
* @example 
**/
uint32_t BufferManageRead(buff_manage_struct *bms,void *buff)
{
    DATA_UNION union_lend = {0};
    if(fifo_get_occupy_size(&(bms->ManageBuff))>=4 )            //获取 缓存管理fifo已用空间大小
    {
        fifo_read(&(bms->ManageBuff), union_lend.u8val , 4);   //读缓存管理出来 存入的数据个数
        if( union_lend.u32val )                                //获取 缓存fifo已用空间大小
        {   
            bms->Len = union_lend.u32val;
            return fifo_read(&(bms->Buff),buff, bms->Len);  //读缓存 出来 存入的数据个数长度
        }
    }
    return  0; 
}








