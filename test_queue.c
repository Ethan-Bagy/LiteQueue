/**
 * @file  test_queue.c
 * @brief queue 完整测试用例
 *
 * 编译命令 (pthread 模式，支持多线程测试):
 *   gcc -Wall -Wextra -O2 -DQUEUE_USE_PTHREAD \
 *       test_queue.c queue.c -lpthread -o test_queue
 *   ./test_queue
 *
 * 编译命令 (裸机模式):
 *   gcc -Wall -Wextra -O2 \
 *       test_queue.c queue.c -o test_queue
 *   ./test_queue
 *
 * @author Ethan.Ba
 */

#include "queue.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#ifdef QUEUE_USE_PTHREAD
#  include <pthread.h>
#endif

/* =========================================================
 *  测试框架
 * ========================================================= */
static int s_pass = 0, s_fail = 0;

#define TEST_ASSERT(cond, msg)                                          \
    do {                                                                \
        if (cond) {                                                     \
            printf("  [PASS] %s\n", msg);                              \
            s_pass++;                                                   \
        } else {                                                        \
            printf("  [FAIL] %s  (line %d)\n", msg, __LINE__);         \
            s_fail++;                                                   \
        }                                                               \
    } while (0)

#define TEST_SECTION(name) printf("\n=== %s ===\n", name)

/* =========================================================
 *  自定义数据类型
 * ========================================================= */
typedef struct {
    uint16_t id;
    float    temperature;
    uint8_t  status;
} sensor_data_t;

/* =========================================================
 *  测试函数声明
 * ========================================================= */
static void test_basic_int(void);
static void test_float_queue(void);
static void test_struct_queue(void);
static void test_deque(void);
static void test_ring_wrap(void);
static void test_bulk(void);
static void test_at(void);
static void test_clear(void);
static void test_null_safety(void);
static void test_count_freespace(void);

#ifndef QUEUE_NO_MALLOC
static void test_dynamic_create(void);
#endif

#ifdef QUEUE_USE_PTHREAD
static void test_multithreaded(void);
#endif

/* =========================================================
 *  测试 1: 基本 int 队列操作
 * ========================================================= */

/**
 * @brief 测试 int 类型的基本入队、出队、peek 及边界行为
 *
 * @author Ethan.Ba
 */
static void test_basic_int(void)
{
    TEST_SECTION("基本 int 队列操作");

    int buf[8];
    queue_t     q;
    queue_err_t err;

    err = queue_init_static(&q, buf, 8, sizeof(int));
    TEST_ASSERT(err == QUEUE_OK,   "init_static 成功");
    TEST_ASSERT(queue_is_empty(&q),  "初始化后为空");
    TEST_ASSERT(!queue_is_full(&q),  "初始化后非满");
    TEST_ASSERT(queue_count(&q) == 0,    "初始 count=0");
    TEST_ASSERT(queue_capacity(&q) == 8, "capacity=8");

    /* 入队 */
    for (int i = 1; i <= 8; i++) {
        err = queue_write_back(&q, &i);
        TEST_ASSERT(err == QUEUE_OK, "push_back 成功");
    }
    TEST_ASSERT(queue_is_full(&q),       "8个元素后满");
    TEST_ASSERT(queue_count(&q) == 8,    "count=8");

    /* 超出容量 */
    int extra = 99;
    err = queue_write_back(&q, &extra);
    TEST_ASSERT(err == QUEUE_FULL, "满后 push_back 返回 FULL");

    /* peek 不移除 */
    int val;
    err = queue_peek_front(&q, &val);
    TEST_ASSERT(err == QUEUE_OK && val == 1, "peek_front=1");
    TEST_ASSERT(queue_count(&q) == 8,        "peek 后 count 不变");

    err = queue_peek_back(&q, &val);
    TEST_ASSERT(err == QUEUE_OK && val == 8, "peek_back=8");

    /* 出队，验证 FIFO 顺序 */
    for (int i = 1; i <= 8; i++) {
        err = queue_read_front(&q, &val);
        TEST_ASSERT(err == QUEUE_OK && val == i, "pop_front 顺序正确");
    }
    TEST_ASSERT(queue_is_empty(&q), "全部出队后为空");

    /* 空队列继续出队 */
    err = queue_read_front(&q, &val);
    TEST_ASSERT(err == QUEUE_EMPTY, "空队列 pop 返回 EMPTY");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 2: float 队列
 * ========================================================= */

/**
 * @brief 测试 float 类型的入队与出队精度
 *
 * @author Ethan.Ba
 */
static void test_float_queue(void)
{
    TEST_SECTION("float 队列");

    float buf[4];
    queue_t q;
    queue_init_static(&q, buf, 4, sizeof(float));

    float vals[] = {1.1f, 2.2f, 3.3f, 4.4f};
    for (int i = 0; i < 4; i++)
        queue_write_back(&q, &vals[i]);

    float out;
    for (int i = 0; i < 4; i++) {
        queue_read_front(&q, &out);
        TEST_ASSERT(out == vals[i], "float 出队值正确");
    }

    queue_destroy(&q);
}

/* =========================================================
 *  测试 3: 结构体队列
 * ========================================================= */

/**
 * @brief 测试自定义结构体 sensor_data_t 的入队与出队完整性
 *
 * @author Ethan.Ba
 */
static void test_struct_queue(void)
{
    TEST_SECTION("结构体 sensor_data_t 队列");

    sensor_data_t buf[4];
    queue_t q;
    queue_init_static(&q, buf, 4, sizeof(sensor_data_t));

    sensor_data_t s1 = {1, 25.5f, 0xAB};
    sensor_data_t s2 = {2, 36.6f, 0xCD};
    queue_write_back(&q, &s1);
    queue_write_back(&q, &s2);

    sensor_data_t out;
    queue_read_front(&q, &out);
    TEST_ASSERT(out.id == 1 && out.temperature == 25.5f && out.status == 0xAB,
                "结构体元素 s1 正确");

    queue_read_front(&q, &out);
    TEST_ASSERT(out.id == 2 && out.temperature == 36.6f && out.status == 0xCD,
                "结构体元素 s2 正确");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 4: 双端队列
 * ========================================================= */

/**
 * @brief 测试 push_front / pop_back 双端队列接口
 *
 * 操作序列: push_front(3,2,1) + push_back(4,5) → [1,2,3,4,5]
 * 交替从两端出队验证顺序。
 *
 * @author Ethan.Ba
 */
static void test_deque(void)
{
    TEST_SECTION("双端队列 (push_front / pop_back)");

    int buf[5];
    queue_t q;
    queue_init_static(&q, buf, 5, sizeof(int));

    int v;

    /* push_front 依次插入 3,2,1 → 队列为 [1,2,3] */
    v = 3; queue_write_front(&q, &v);
    v = 2; queue_write_front(&q, &v);
    v = 1; queue_write_front(&q, &v);

    /* push_back 追加 4,5 → [1,2,3,4,5] */
    v = 4; queue_write_back(&q, &v);
    v = 5; queue_write_back(&q, &v);

    TEST_ASSERT(queue_count(&q) == 5, "双端 count=5");

    int out;
    queue_read_front(&q, &out); TEST_ASSERT(out == 1, "pop_front=1");
    queue_read_back(&q,  &out); TEST_ASSERT(out == 5, "pop_back=5");
    queue_read_front(&q, &out); TEST_ASSERT(out == 2, "pop_front=2");
    queue_read_back(&q,  &out); TEST_ASSERT(out == 4, "pop_back=4");
    queue_read_front(&q, &out); TEST_ASSERT(out == 3, "pop_front=3");
    TEST_ASSERT(queue_is_empty(&q), "全部弹出后为空");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 5: 环形绕回
 * ========================================================= */

/**
 * @brief 验证环形缓冲区在写指针绕回后读写仍然正确
 *
 * @author Ethan.Ba
 */
static void test_ring_wrap(void)
{
    TEST_SECTION("环形缓冲区绕回");

    int buf[4];
    queue_t q;
    queue_init_static(&q, buf, 4, sizeof(int));

    /* 填满后出队 2 个，再入队 2 个触发绕回 */
    for (int i = 1; i <= 4; i++) queue_write_back(&q, &i);

    int out;
    queue_read_front(&q, &out); /* 取出 1 */
    queue_read_front(&q, &out); /* 取出 2 */

    int v;
    v = 5; queue_write_back(&q, &v);
    v = 6; queue_write_back(&q, &v);

    queue_read_front(&q, &out); TEST_ASSERT(out == 3, "绕回后 pop=3");
    queue_read_front(&q, &out); TEST_ASSERT(out == 4, "绕回后 pop=4");
    queue_read_front(&q, &out); TEST_ASSERT(out == 5, "绕回后 pop=5");
    queue_read_front(&q, &out); TEST_ASSERT(out == 6, "绕回后 pop=6");
    TEST_ASSERT(queue_is_empty(&q), "绕回测试完成后为空");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 6: 批量操作
 * ========================================================= */

/**
 * @brief 测试 push_back_bulk / pop_front_bulk 的容量限制与数据完整性
 *
 * @author Ethan.Ba
 */
static void test_bulk(void)
{
    TEST_SECTION("批量操作 push_back_bulk / pop_front_bulk");

    int buf[8];
    queue_t q;
    queue_init_static(&q, buf, 8, sizeof(int));

    int src[10] = {10, 20, 30, 40, 50, 60, 70, 80, 90, 100};

    uint32_t written = queue_write_back_bulk(&q, src, 10);
    TEST_ASSERT(written == 8,          "批量写入: 容量限制为 8");
    TEST_ASSERT(queue_is_full(&q), "批量写入后满");

    int dst[8] = {0};
    uint32_t read = queue_read_front_bulk(&q, dst, 8);
    TEST_ASSERT(read == 8,                           "批量读取 8 个");
    TEST_ASSERT(dst[0] == 10 && dst[7] == 80,        "批量数据正确");
    TEST_ASSERT(queue_is_empty(&q),              "批量读取后为空");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 7: at() 随机访问
 * ========================================================= */

/**
 * @brief 测试 queue_at() 的正常访问与越界检测
 *
 * @author Ethan.Ba
 */
static void test_at(void)
{
    TEST_SECTION("at() 随机访问");

    int buf[5];
    queue_t q;
    queue_init_static(&q, buf, 5, sizeof(int));

    for (int i = 0; i < 5; i++) queue_write_back(&q, &i);

    int val;
    for (int i = 0; i < 5; i++) {
        queue_at(&q, i, &val);
        TEST_ASSERT(val == i, "at(i) 值正确");
    }

    queue_err_t err = queue_at(&q, 5, &val);
    TEST_ASSERT(err == QUEUE_INVAL, "越界 at() 返回 INVAL");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 8: clear() 重置
 * ========================================================= */

/**
 * @brief 测试 queue_clear() 后队列状态是否完全归零
 *
 * @author Ethan.Ba
 */
static void test_clear(void)
{
    TEST_SECTION("clear() 重置");

    int buf[4];
    queue_t q;
    queue_init_static(&q, buf, 4, sizeof(int));

    int v = 1;
    queue_write_back(&q, &v);
    queue_write_back(&q, &v);
    queue_clear(&q);

    TEST_ASSERT(queue_count(&q) == 0,       "clear 后 count=0");
    TEST_ASSERT(queue_is_empty(&q),          "clear 后 is_empty");
    TEST_ASSERT(queue_free_space(&q) == 4,   "clear 后 free_space=4");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 9: 空指针安全性
 * ========================================================= */

/**
 * @brief 验证所有接口在传入 NULL 时均能安全返回而不崩溃
 *
 * @author Ethan.Ba
 */
static void test_null_safety(void)
{
    TEST_SECTION("空指针安全性");

    int val;
    TEST_ASSERT(queue_write_back(NULL, &val)  == QUEUE_NULLPTR, "NULL queue push_back");
    TEST_ASSERT(queue_read_front(NULL, &val)  == QUEUE_NULLPTR, "NULL queue pop_front");
    TEST_ASSERT(queue_peek_front(NULL, &val) == QUEUE_NULLPTR, "NULL queue peek_front");
    TEST_ASSERT(queue_is_empty(NULL)  == true,  "NULL queue is_empty 返回 true");
    TEST_ASSERT(queue_capacity(NULL)  == 0,     "NULL queue capacity 返回 0");

    /* data 为 NULL */
    int buf[4];
    queue_t q;
    queue_init_static(&q, buf, 4, sizeof(int));
    TEST_ASSERT(queue_write_back(&q, NULL) == QUEUE_NULLPTR,
                "data=NULL push_back 返回 NULLPTR");

    queue_destroy(&q);
}

/* =========================================================
 *  测试 10: 动态分配
 * ========================================================= */

#ifndef QUEUE_NO_MALLOC

/**
 * @brief 测试 queue_create() 动态分配及 double 类型数据正确性
 *
 * @author Ethan.Ba
 */
static void test_dynamic_create(void)
{
    TEST_SECTION("动态创建 queue_create");

    queue_t *q = queue_create(16, sizeof(double));
    TEST_ASSERT(q != NULL,                    "create 成功");
    TEST_ASSERT(queue_capacity(q) == 16,  "capacity=16");

    double d = 3.14;
    queue_write_back(q, &d);
    d = 2.71;
    queue_write_back(q, &d);

    double out;
    queue_read_front(q, &out);
    TEST_ASSERT(out == 3.14, "double 出队正确");

    queue_destroy(q);
    free(q);   /* destroy 只释放 buf，结构体本身须外部 free */
}

#endif /* QUEUE_NO_MALLOC */

/* =========================================================
 *  测试 11: 多线程并发
 * ========================================================= */

#ifdef QUEUE_USE_PTHREAD

#define THREAD_N     4
#define ITEMS_PER_T  1000

typedef struct {
    queue_t *q;
    int          producer_id;
} thread_arg_t;

/**
 * @brief 生产者线程：循环入队直到写满 ITEMS_PER_T 个元素
 *
 * @param [in] arg thread_arg_t 指针
 *
 * @return NULL
 *
 * @author Ethan.Ba
 */
static void *producer_func(void *arg)
{
    thread_arg_t *a = (thread_arg_t *)arg;
    for (int i = 0; i < ITEMS_PER_T; i++) {
        int val = a->producer_id * 10000 + i;
        while (queue_write_back(a->q, &val) == QUEUE_FULL)
            sched_yield();
    }
    return NULL;
}

/**
 * @brief 消费者线程：循环出队直到读取 ITEMS_PER_T 个元素
 *
 * @param [in] arg queue_t 指针
 *
 * @return NULL
 *
 * @author Ethan.Ba
 */
static void *consumer_func(void *arg)
{
    queue_t *q = (queue_t *)arg;
    int val, cnt = 0;
    while (cnt < ITEMS_PER_T) {
        if (queue_read_front(q, &val) == QUEUE_OK)
            cnt++;
        else
            sched_yield();
    }
    return NULL;
}

/**
 * @brief 测试 4 个生产者 + 4 个消费者并发读写的线程安全性
 *
 * @author Ethan.Ba
 */
static void test_multithreaded(void)
{
    TEST_SECTION("多线程并发 (4生产者 + 4消费者)");

    int buf[64];
    queue_t q;
    queue_init_static(&q, buf, 64, sizeof(int));

    pthread_t    producers[THREAD_N], consumers[THREAD_N];
    thread_arg_t args[THREAD_N];

    for (int i = 0; i < THREAD_N; i++) {
        args[i].q = &q;
        args[i].producer_id = i;
        pthread_create(&producers[i], NULL, producer_func, &args[i]);
        pthread_create(&consumers[i], NULL, consumer_func, &q);
    }
    for (int i = 0; i < THREAD_N; i++) {
        pthread_join(producers[i], NULL);
        pthread_join(consumers[i], NULL);
    }

    TEST_ASSERT(queue_is_empty(&q),
                "4×1000 生产 + 4×1000 消费后队列为空，无数据丢失");

    queue_destroy(&q);
}

#endif /* QUEUE_USE_PTHREAD */

/* =========================================================
 *  测试 12: count 与 free_space 一致性
 * ========================================================= */

/**
 * @brief 验证每次入队/出队后 count + free_space == capacity 始终成立
 *
 * @author Ethan.Ba
 */
static void test_count_freespace(void)
{
    TEST_SECTION("count / free_space 一致性");

    int buf[6];
    queue_t q;
    queue_init_static(&q, buf, 6, sizeof(int));

    for (int i = 0; i < 6; i++) {
        int v = i;
        queue_write_back(&q, &v);
        TEST_ASSERT(queue_count(&q) == (uint32_t)(i + 1) &&
                    queue_free_space(&q) == (uint32_t)(5 - i),
                    "push 后 count+free_space == capacity");
    }

    int out;
    for (int i = 0; i < 6; i++) {
        queue_read_front(&q, &out);
        TEST_ASSERT(queue_count(&q) == (uint32_t)(5 - i) &&
                    queue_free_space(&q) == (uint32_t)(i + 1),
                    "pop 后 count+free_space == capacity");
    }

    queue_destroy(&q);
}

/* =========================================================
 *  main
 * ========================================================= */

/**
 * @brief 测试入口，依次运行全部测试用例并汇总结果
 *
 * @return 0 全部通过，1 存在失败用例
 *
 * @author Ethan.Ba
 */
int main(void)
{
    printf("========================================\n");
    printf("  queue 完整测试\n");
#ifdef QUEUE_USE_PTHREAD
    printf("  模式: POSIX pthread 锁\n");
#else
    printf("  模式: 裸机临界区 (无锁测试)\n");
#endif
    printf("========================================\n");

    test_basic_int();
    test_float_queue();
    test_struct_queue();
    test_deque();
    test_ring_wrap();
    test_bulk();
    test_at();
    test_clear();
    test_null_safety();
#ifndef QUEUE_NO_MALLOC
    test_dynamic_create();
#endif
#ifdef QUEUE_USE_PTHREAD
    test_multithreaded();
#endif
    test_count_freespace();

    printf("\n========================================\n");
    printf("  结果: PASS=%d  FAIL=%d  总计=%d\n",
           s_pass, s_fail, s_pass + s_fail);
    printf("========================================\n");

    return s_fail ? 1 : 0;
}
