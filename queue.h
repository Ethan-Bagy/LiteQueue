/**
 * @file   queue.h
 * @brief  轻量级通用消息队列 —— 适用于 MCU / 嵌入式 / 多进程环境
 *
 * 特性:
 *  - 泛型: 通过 element_size 支持任意数据类型 (int / float / struct …)
 *  - 环形缓冲区: O(1) 读写，零动态内存分配
 *  - 可选锁: POSIX pthread_mutex 或裸机临界区宏，编译开关切换
 *  - 进程安全: 可放入共享内存，配合进程间互斥锁使用
 *  - 无标准库依赖 (可裁剪): 只需 <string.h> memcpy / memset
 *
 * 编译宏:
 *  QUEUE_USE_PTHREAD        定义后使用 pthread_mutex，否则使用裸机临界区宏
 *  QUEUE_CRITICAL_ENTER(q)  用户自定义进入临界区 (如关中断)
 *  QUEUE_CRITICAL_EXIT(q)   用户自定义退出临界区 (如开中断)
 *  QUEUE_NO_MALLOC          定义后禁用动态内存分配接口
 *
 * @author Ethan.Ba
 */

#ifndef QUEUE_H
#define QUEUE_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <string.h>

/* =========================================================
 *  锁策略选择
 * ========================================================= */
#ifdef QUEUE_USE_PTHREAD
#  include <pthread.h>
   typedef pthread_mutex_t queue_lock_t;
#  define QUEUE_LOCK_INIT(lk)    pthread_mutex_init(&(lk), NULL)
#  define QUEUE_LOCK_DESTROY(lk) pthread_mutex_destroy(&(lk))
#  define QUEUE_LOCK(lk)         pthread_mutex_lock(&(lk))
#  define QUEUE_UNLOCK(lk)       pthread_mutex_unlock(&(lk))
#else
   /* 裸机模式: 用户通过宏注入临界区 (如 __disable_irq / __enable_irq) */
   typedef volatile uint8_t queue_lock_t;  /* 占位，裸机不用 */
#  ifndef QUEUE_CRITICAL_ENTER
#    define QUEUE_CRITICAL_ENTER(q)  /* 用户自定义: 关中断等 */
#  endif
#  ifndef QUEUE_CRITICAL_EXIT
#    define QUEUE_CRITICAL_EXIT(q)   /* 用户自定义: 开中断等 */
#  endif
#  define QUEUE_LOCK_INIT(lk)    (0)
#  define QUEUE_LOCK_DESTROY(lk) (void)(lk)
#  define QUEUE_LOCK(lk)         QUEUE_CRITICAL_ENTER(lk)
#  define QUEUE_UNLOCK(lk)       QUEUE_CRITICAL_EXIT(lk)
#endif

/* =========================================================
 *  错误码
 * ========================================================= */
typedef enum 
{
    QUEUE_OK       =  0,                /* 成功              */
    QUEUE_FULL     = -1,                /* 队列已满          */
    QUEUE_EMPTY    = -2,                /* 队列为空          */
    QUEUE_NULLPTR  = -3,                /* 空指针参数        */
    QUEUE_INVAL    = -4,                /* 非法参数          */
    QUEUE_NOMEM    = -5,                /* 内存不足          */

} queue_err_t;

/* =========================================================
 *  队列句柄
 *  存储布局: [head ... tail) 环形，tail 指向下一个写入位置
 * ========================================================= */
typedef struct 
{
    uint8_t            *buf;            /* 数据缓冲区指针 (用户提供或动态分配)   */
    uint32_t            capacity;       /* 最大元素个数                        */
    uint32_t            element_size;   /* 单个元素字节数                      */
    volatile uint32_t   head;           /* 读指针 (队首)                       */
    volatile uint32_t   tail;           /* 写指针 (队尾下一位)                 */
    volatile uint32_t   count;          /* 当前元素个数                        */
    queue_lock_t        lock;           /* 互斥锁                             */
    bool                owner;          /* 是否拥有 buf 内存 (负责释放)        */

} queue_t;

/* =========================================================
 *  API 声明
 * ========================================================= */
#ifdef __cplusplus
extern "C" {
#endif

/* ---------- 初始化 / 创建 / 销毁 ---------- */

/**
 * @brief 用用户提供的静态缓冲区初始化队列
 *
 * 推荐在 MCU 上使用，零动态内存分配。
 *
 * @param [in] q            队列句柄指针
 * @param [in] buf          数据缓冲区，大小须 >= capacity * element_size 字节
 * @param [in] capacity     队列最大元素个数
 * @param [in] element_size 单个元素的字节数
 *
 * @return QUEUE_OK      初始化成功
 * @return QUEUE_NULLPTR q 或 buf 为 NULL
 * @return QUEUE_INVAL   capacity / element_size 为 0，或互斥锁初始化失败
 *
 * @author Ethan.Ba
 */
queue_err_t queue_init_static(queue_t *q, void *buf, uint32_t capacity, uint32_t element_size);

#ifndef QUEUE_NO_MALLOC
#include <stdlib.h>

/**
 * @brief 动态分配内存创建队列
 *
 * 内部调用 malloc 分配句柄及数据缓冲区。
 * 使用完毕后须先调用 queue_destroy()，再 free() 句柄本身。
 * 若目标平台不支持动态内存，定义 QUEUE_NO_MALLOC 以禁用此接口。
 *
 * @param [in] capacity     队列最大元素个数
 * @param [in] element_size 单个元素的字节数
 *
 * @return 成功返回队列指针，分配失败返回 NULL
 *
 * @author Ethan.Ba
 */
queue_t *queue_create(uint32_t capacity, uint32_t element_size);

#endif /* QUEUE_NO_MALLOC */

/**
 * @brief 销毁队列，释放内部资源
 *
 * 销毁互斥锁，并在队列拥有缓冲区所有权时释放缓冲区内存。
 * 若句柄本身由 queue_create() 动态分配，调用者需在此函数返回后
 * 自行 free() 句柄指针。
 *
 * @param [in] q 队列句柄指针
 *
 * @return QUEUE_OK      销毁成功
 * @return QUEUE_NULLPTR q 为 NULL
 *
 * @author Ethan.Ba
 */
queue_err_t queue_destroy(queue_t *q);

/**
 * @brief 清空队列
 *
 * 重置读写指针与元素计数，不释放缓冲区内存，队列可继续使用。
 *
 * @param [in] q 队列句柄指针
 *
 * @return QUEUE_OK      清空成功
 * @return QUEUE_NULLPTR q 或内部缓冲区为 NULL
 *
 * @author Ethan.Ba
 */
queue_err_t queue_clear(queue_t *q);

/* ---------- 写入 ---------- */

/**
 * @brief 向队尾写入一个元素 (Push Back)
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 待写入数据的指针，大小须与初始化时的 element_size 一致
 *
 * @return QUEUE_OK      写入成功
 * @return QUEUE_NULLPTR q 或 data 为 NULL
 * @return QUEUE_FULL    队列已满
 *
 * @author Ethan.Ba
 */
queue_err_t queue_write_back(queue_t *q, const void *data);

/**
 * @brief 向队首插入一个元素 (Push Front)
 *
 * 双端队列写入接口，插入后该元素成为新的队首。
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 待写入数据的指针，大小须与初始化时的 element_size 一致
 *
 * @return QUEUE_OK      插入成功
 * @return QUEUE_NULLPTR q 或 data 为 NULL
 * @return QUEUE_FULL    队列已满
 *
 * @author Ethan.Ba
 */
queue_err_t queue_write_front(queue_t *q, const void *data);

/**
 * @brief 向队尾写入一个元素，队列满时自动覆写最老的数据（Push Back with Overwrite）
 *
 * 若队列未满，行为同 queue_write_back()。
 * 若队列已满，则丢弃队首元素，再将新数据写入队尾。队列始终保持满状态，
 * 相当于环形缓冲区覆盖最旧数据。
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 待写入数据的指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      写入成功（始终成功，除非参数无效）
 * @return QUEUE_NULLPTR q 或 data 为 NULL
 * @return QUEUE_INVAL   队列未正确初始化
 *
 * @author Ethan.Ba
 */
queue_err_t queue_write_back_overwrite(queue_t *q, const void *data);

/* ---------- 读取 (移除元素) ---------- */

/**
 * @brief 从队首读取并移除一个元素 (Pop Front)
 *
 * 标准 FIFO 出队操作。
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_EMPTY   队列为空
 *
 * @author Ethan.Ba
 */
queue_err_t queue_read_front(queue_t *q, void *out);

/**
 * @brief 从队尾读取并移除一个元素 (Pop Back)
 *
 * 双端队列读取接口。
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_EMPTY   队列为空
 *
 * @author Ethan.Ba
 */
queue_err_t queue_read_back(queue_t *q, void *out);

/* ---------- 查看 (不移除元素) ---------- */

/**
 * @brief 查看队首元素但不移除 (Peek Front)
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_EMPTY   队列为空
 *
 * @author Ethan.Ba
 */
queue_err_t queue_peek_front(queue_t *q, void *out);

/**
 * @brief 查看队尾元素但不移除 (Peek Back)
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_EMPTY   队列为空
 *
 * @author Ethan.Ba
 */
queue_err_t queue_peek_back(queue_t *q, void *out);

/* ---------- 状态查询 ---------- */

/**
 * @brief 判断队列是否为空
 *
 * @param [in] q 队列句柄指针
 *
 * @return true  队列为空或句柄无效
 * @return false 队列非空
 *
 * @author Ethan.Ba
 */
bool queue_is_empty(queue_t *q);

/**
 * @brief 判断队列是否已满
 *
 * @param [in] q 队列句柄指针
 *
 * @return true  队列已满
 * @return false 队列未满或句柄无效
 *
 * @author Ethan.Ba
 */
bool queue_is_full(queue_t *q);

/**
 * @brief 获取当前队列中的元素个数
 *
 * @param [in] q 队列句柄指针
 *
 * @return 当前元素个数，句柄无效时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_count(queue_t *q);

/**
 * @brief 获取队列剩余可写入的元素个数
 *
 * @param [in] q 队列句柄指针
 *
 * @return 剩余空闲槽位数，句柄无效时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_free_space(queue_t *q);

/**
 * @brief 获取队列的最大容量
 *
 * capacity 在初始化后不再改变，此函数无需加锁。
 *
 * @param [in] q 队列句柄指针
 *
 * @return 最大元素个数，q 为 NULL 时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_capacity(queue_t *q);

/* ---------- 批量操作 ---------- */

/**
 * @brief 批量向队尾写入元素
 *
 * 一次加锁完成所有写入，效率高于循环调用 queue_write_back()。
 * 若剩余空间不足 n 个，则仅写入能容纳的最大数量。
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 源数据数组首地址，总大小须 >= n * element_size 字节
 * @param [in] n    期望写入的元素个数
 *
 * @return 实际写入的元素个数
 *
 * @author Ethan.Ba
 */
uint32_t queue_write_back_bulk(queue_t *q, const void *data, uint32_t n);

/**
 * @brief 批量从队首读取并移除元素
 *
 * 一次加锁完成所有读取，效率高于循环调用 queue_read_front()。
 * 若队列中元素数量不足 n 个，则仅读取实际存在的数量。
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 目标缓冲区首地址，总大小须 >= n * element_size 字节
 * @param [in]  n   期望读取的元素个数
 *
 * @return 实际读取的元素个数
 *
 * @author Ethan.Ba
 */
uint32_t queue_read_front_bulk(queue_t *q, void *out, uint32_t n);

/**
 * @brief 按逻辑索引随机访问元素（不移除）
 *
 * index 0 对应队首，index count-1 对应队尾。
 *
 * @param [in]  q     队列句柄指针
 * @param [in]  index 逻辑索引，范围 [0, count-1]
 * @param [out] out   接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_INVAL   index >= count（越界）
 *
 * @author Ethan.Ba
 */
queue_err_t queue_at(queue_t *q, uint32_t index, void *out);

#ifdef __cplusplus
}
#endif

#endif /* QUEUE_H */
