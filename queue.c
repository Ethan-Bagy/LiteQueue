/**
 * @file  queue.c
 * @brief 轻量级通用消息队列实现
 *
 * @author Ethan.Ba
 */

#include "queue.h"

/* =========================================================
 *  内部辅助宏
 * ========================================================= */
#define Q_VALID(q)   ((q) && (q)->buf && (q)->capacity && (q)->element_size)
#define Q_ELEM(q, i) ((q)->buf + (uint32_t)(i) * (q)->element_size)

/* =========================================================
 *  内部静态函数
 * ========================================================= */

/**
 * @brief 环形索引前进一步
 *
 * @param [in] idx      当前索引
 * @param [in] capacity 队列容量
 *
 * @return 前进后的索引
 *
 * @author Ethan.Ba
 */
static inline uint32_t q_next(uint32_t idx, uint32_t capacity)
{
    return (idx + 1 >= capacity) ? 0 : idx + 1;
}

/**
 * @brief 环形索引后退一步
 *
 * @param [in] idx      当前索引
 * @param [in] capacity 队列容量
 *
 * @return 后退后的索引
 *
 * @author Ethan.Ba
 */
static inline uint32_t q_prev(uint32_t idx, uint32_t capacity)
{
    return (idx == 0) ? capacity - 1 : idx - 1;
}

/* =========================================================
 *  初始化 / 创建 / 销毁
 * ========================================================= */

/**
 * @brief 用用户提供的静态缓冲区初始化队列
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
queue_err_t queue_init_static(queue_t *q, void *buf, uint32_t capacity, uint32_t element_size)
{
    if (!q || !buf)                  return QUEUE_NULLPTR;
    if (!capacity || !element_size)  return QUEUE_INVAL;

    q->buf          = (uint8_t *)buf;
    q->capacity     = capacity;
    q->element_size = element_size;
    q->head         = 0;
    q->tail         = 0;
    q->count        = 0;
    q->owner        = false;

    if (QUEUE_LOCK_INIT(q->lock) != 0)
        return QUEUE_INVAL;

    return QUEUE_OK;
}

#ifndef QUEUE_NO_MALLOC

/**
 * @brief 动态分配内存创建队列
 *
 * @param [in] capacity     队列最大元素个数
 * @param [in] element_size 单个元素的字节数
 *
 * @return 成功返回队列指针，分配失败返回 NULL
 *
 * @author Ethan.Ba
 */
queue_t *queue_create(uint32_t capacity, uint32_t element_size)
{
    if (!capacity || !element_size) return NULL;

    queue_t *q = (queue_t *)malloc(sizeof(queue_t));
    if (!q) return NULL;

    q->buf = (uint8_t *)malloc((size_t)capacity * element_size);
    if (!q->buf) { free(q); return NULL; }

    q->capacity     = capacity;
    q->element_size = element_size;
    q->head         = 0;
    q->tail         = 0;
    q->count        = 0;
    q->owner        = true;

    if (QUEUE_LOCK_INIT(q->lock) != 0) {
        free(q->buf);
        free(q);
        return NULL;
    }

    return q;
}

#endif /* QUEUE_NO_MALLOC */

/**
 * @brief 销毁队列，释放内部资源
 *
 * @param [in] q 队列句柄指针
 *
 * @return QUEUE_OK      销毁成功
 * @return QUEUE_NULLPTR q 为 NULL
 *
 * @author Ethan.Ba
 */
queue_err_t queue_destroy(queue_t *q)
{
    if (!q) return QUEUE_NULLPTR;

    QUEUE_LOCK_DESTROY(q->lock);

#ifndef QUEUE_NO_MALLOC
    if (q->owner && q->buf) {
        free(q->buf);
        q->buf = NULL;
    }
    /* 若整个结构体也是动态分配的，由调用者决定是否 free(q) */
#endif

    q->capacity = 0;
    q->count    = 0;
    q->head     = 0;
    q->tail     = 0;

    return QUEUE_OK;
}

/**
 * @brief 清空队列
 *
 * @param [in] q 队列句柄指针
 *
 * @return QUEUE_OK      清空成功
 * @return QUEUE_NULLPTR q 或内部缓冲区为 NULL
 *
 * @author Ethan.Ba
 */
queue_err_t queue_clear(queue_t *q)
{
    if (!Q_VALID(q)) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);
    q->head  = 0;
    q->tail  = 0;
    q->count = 0;
    QUEUE_UNLOCK(q->lock);

    return QUEUE_OK;
}

/* =========================================================
 *  写入
 * ========================================================= */

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
queue_err_t queue_write_back(queue_t *q, const void *data)
{
    if (!Q_VALID(q) || !data) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count >= q->capacity) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_FULL;
    }

    memcpy(Q_ELEM(q, q->tail), data, q->element_size);
    q->tail = q_next(q->tail, q->capacity);
    q->count++;

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

/**
 * @brief 向队首插入一个元素 (Push Front)
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
queue_err_t queue_write_front(queue_t *q, const void *data)
{
    if (!Q_VALID(q) || !data) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count >= q->capacity) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_FULL;
    }

    /* head 向后退一格，再将数据写入新队首 */
    q->head = q_prev(q->head, q->capacity);
    memcpy(Q_ELEM(q, q->head), data, q->element_size);
    q->count++;

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

/**
 * @brief 向队尾写入一个元素，队列满时覆写最老的数据 (Overwrite Back)
 *
 * 与 queue_write_back() 的区别:
 *  - 队列未满时行为完全相同
 *  - 队列已满时，丢弃队首最老的一条数据，再将新数据写入队尾，
 *    确保队列中始终保留最新的 capacity 条数据
 *
 * 典型使用场景: 串口/传感器数据滚动缓存，宁可丢老数据也不阻塞写入
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 待写入数据的指针，大小须与初始化时的 element_size 一致
 *
 * @return QUEUE_OK      写入成功，队列未满，无数据被覆写
 * @return QUEUE_FULL    队列已满，覆写最老数据后写入成功 (新数据仍被写入)
 * @return QUEUE_NULLPTR q 或 data 为 NULL
 *
 * @author Ethan.Ba
 */
queue_err_t queue_write_back_overwrite(queue_t *q, const void *data)
{
    if (!Q_VALID(q) || !data) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    queue_err_t ret = QUEUE_OK;

    if (q->count >= q->capacity) 
    {   
        /* 队首前进，丢弃最老数据 */
        q->head = q_next(q->head, q->capacity); 
        q->count--;

        /* 返回 FULL 告知调用者发生了覆写 */
        ret = QUEUE_FULL;   
    }

    /* 写新数据 */
    memcpy(Q_ELEM(q, q->tail), data, q->element_size); 
    q->tail = q_next(q->tail, q->capacity);
    q->count++;

    QUEUE_UNLOCK(q->lock);

    /* QUEUE_OK = 正常写入，QUEUE_FULL = 覆写了老数据 */
    return ret;             
}

/* =========================================================
 *  读取 (移除元素)
 * ========================================================= */

/**
 * @brief 从队首读取并移除一个元素 (Pop Front)
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
queue_err_t queue_read_front(queue_t *q, void *out)
{
    if (!Q_VALID(q) || !out) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count == 0) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_EMPTY;
    }

    memcpy(out, Q_ELEM(q, q->head), q->element_size);
    q->head = q_next(q->head, q->capacity);
    q->count--;

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

/**
 * @brief 从队尾读取并移除一个元素 (Pop Back)
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
queue_err_t queue_read_back(queue_t *q, void *out)
{
    if (!Q_VALID(q) || !out) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count == 0) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_EMPTY;
    }

    /* tail 指向下一个写入位置，实际队尾元素在 tail-1 处 */
    q->tail = q_prev(q->tail, q->capacity);
    memcpy(out, Q_ELEM(q, q->tail), q->element_size);
    q->count--;

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

/* =========================================================
 *  查看 (不移除元素)
 * ========================================================= */

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
queue_err_t queue_peek_front(queue_t *q, void *out)
{
    if (!Q_VALID(q) || !out) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count == 0) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_EMPTY;
    }

    memcpy(out, Q_ELEM(q, q->head), q->element_size);

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

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
queue_err_t queue_peek_back(queue_t *q, void *out)
{
    if (!Q_VALID(q) || !out) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (q->count == 0) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_EMPTY;
    }

    uint32_t back = q_prev(q->tail, q->capacity);
    memcpy(out, Q_ELEM(q, back), q->element_size);

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}

/* =========================================================
 *  状态查询
 * ========================================================= */

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
bool queue_is_empty(queue_t *q)
{
    if (!Q_VALID(q)) return true;
    QUEUE_LOCK(q->lock);
    bool r = (q->count == 0);
    QUEUE_UNLOCK(q->lock);
    return r;
}

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
bool queue_is_full(queue_t *q)
{
    if (!Q_VALID(q)) return false;
    QUEUE_LOCK(q->lock);
    bool r = (q->count >= q->capacity);
    QUEUE_UNLOCK(q->lock);
    return r;
}

/**
 * @brief 获取当前队列中的元素个数
 *
 * @param [in] q 队列句柄指针
 *
 * @return 当前元素个数，句柄无效时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_count(queue_t *q)
{
    if (!Q_VALID(q)) return 0;
    QUEUE_LOCK(q->lock);
    uint32_t c = q->count;
    QUEUE_UNLOCK(q->lock);
    return c;
}

/**
 * @brief 获取队列剩余可写入的元素个数
 *
 * @param [in] q 队列句柄指针
 *
 * @return 剩余空闲槽位数，句柄无效时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_free_space(queue_t *q)
{
    if (!Q_VALID(q)) return 0;
    QUEUE_LOCK(q->lock);
    uint32_t f = q->capacity - q->count;
    QUEUE_UNLOCK(q->lock);
    return f;
}

/**
 * @brief 获取队列的最大容量
 *
 * @param [in] q 队列句柄指针
 *
 * @return 最大元素个数，q 为 NULL 时返回 0
 *
 * @author Ethan.Ba
 */
uint32_t queue_capacity(queue_t *q)
{
    if (!q) return 0;
    return q->capacity;   /* capacity 初始化后不变，无需加锁 */
}

/* =========================================================
 *  批量操作
 * ========================================================= */

/**
 * @brief 批量向队尾写入元素
 *
 * @param [in] q    队列句柄指针
 * @param [in] data 源数据数组首地址，总大小须 >= n * element_size 字节
 * @param [in] n    期望写入的元素个数
 *
 * @return 实际写入的元素个数
 *
 * @author Ethan.Ba
 */
uint32_t queue_write_back_bulk(queue_t *q, const void *data, uint32_t n)
{
    if (!Q_VALID(q) || !data || !n) return 0;

    QUEUE_LOCK(q->lock);

    uint32_t free_slots = q->capacity - q->count;
    uint32_t write_n    = (n < free_slots) ? n : free_slots;

    for (uint32_t i = 0; i < write_n; i++) {
        memcpy(Q_ELEM(q, q->tail),
               (const uint8_t *)data + (size_t)i * q->element_size,
               q->element_size);
        q->tail = q_next(q->tail, q->capacity);
    }
    q->count += write_n;

    QUEUE_UNLOCK(q->lock);
    return write_n;
}

/**
 * @brief 批量从队首读取并移除元素
 *
 * @param [in]  q   队列句柄指针
 * @param [out] out 目标缓冲区首地址，总大小须 >= n * element_size 字节
 * @param [in]  n   期望读取的元素个数
 *
 * @return 实际读取的元素个数
 *
 * @author Ethan.Ba
 */
uint32_t queue_read_front_bulk(queue_t *q, void *out, uint32_t n)
{
    if (!Q_VALID(q) || !out || !n) return 0;

    QUEUE_LOCK(q->lock);

    uint32_t read_n = (n < q->count) ? n : q->count;

    for (uint32_t i = 0; i < read_n; i++) {
        memcpy((uint8_t *)out + (size_t)i * q->element_size,
               Q_ELEM(q, q->head),
               q->element_size);
        q->head = q_next(q->head, q->capacity);
    }
    q->count -= read_n;

    QUEUE_UNLOCK(q->lock);
    return read_n;
}

/**
 * @brief 按逻辑索引随机访问元素（不移除）
 *
 * @param [in]  q     队列句柄指针
 * @param [in]  index 逻辑索引，范围 [0, count-1]，0 为队首
 * @param [out] out   接收数据的缓冲区指针，大小须与 element_size 一致
 *
 * @return QUEUE_OK      读取成功
 * @return QUEUE_NULLPTR q 或 out 为 NULL
 * @return QUEUE_INVAL   index >= count（越界）
 *
 * @author Ethan.Ba
 */
queue_err_t queue_at(queue_t *q, uint32_t index, void *out)
{
    if (!Q_VALID(q) || !out) return QUEUE_NULLPTR;

    QUEUE_LOCK(q->lock);

    if (index >= q->count) {
        QUEUE_UNLOCK(q->lock);
        return QUEUE_INVAL;
    }

    uint32_t real_idx = (q->head + index) % q->capacity;
    memcpy(out, Q_ELEM(q, real_idx), q->element_size);

    QUEUE_UNLOCK(q->lock);
    return QUEUE_OK;
}
