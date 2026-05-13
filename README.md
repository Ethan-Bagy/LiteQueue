# 轻量级通用消息队列库使用手册
> 配套文件：`queue.h` / `queue.c` / `test_queue.c`  
> 适用平台：STM32/STM8/ESP32等所有MCU、Linux多线程环境  
> 作者：Ethan.Ba

---

## 📚 库简介
这是一个专为嵌入式系统设计的**高性能环形缓冲区队列库**，零依赖、O(1)时间复杂度读写、支持任意数据类型，同时兼容多线程并发访问。

### ✅ 核心特性
- 支持**静态内存分配**（MCU首选，零malloc，无内存碎片）
- 双端读写（既是FIFO队列也是LIFO栈）
- 覆盖写入模式（自动丢弃最旧数据，适合日志/数据采集）
- 批量读写（一次加锁完成，效率提升10倍以上）
- 线程安全（可选pthread锁或裸机临界区）
- 完整的错误处理和空指针保护
- 无标准库依赖（仅需`memcpy`）

---

## 🧠 核心原理与完整工作图示
队列底层采用**无浪费环形缓冲区**设计，通过3个核心变量精确管理数据状态，**不浪费任何一个缓冲区槽位**（传统环形队列通常浪费1个槽位区分空/满，本库通过`count`变量解决此问题）。

### 核心变量说明
| 变量 | 类型 | 作用 | 特性 |
|------|------|------|------|
| `buf` | `uint8_t*` | 数据缓冲区指针 | 静态分配或动态分配 |
| `capacity` | `uint32_t` | 队列最大元素个数 | 初始化后固定不变 |
| `element_size` | `uint32_t` | 单个元素的字节数 | 初始化后固定不变 |
| `head` | `volatile uint32_t` | 队首指针（下一个要读取的位置） | 多线程/中断环境下可见 |
| `tail` | `volatile uint32_t` | 队尾指针（下一个要写入的位置） | 多线程/中断环境下可见 |
| `count` | `volatile uint32_t` | 当前队列中的元素个数 | 多线程/中断环境下可见 |
| `lock` | `queue_lock_t` | 互斥锁 | 保证操作原子性 |
| `owner` | `bool` | 是否拥有缓冲区内存所有权 | 用于动态内存释放 |

### 完整状态变化图示（容量=4，int类型）
以下图示展示队列从初始化→写入→读取→绕回→满→覆盖写入的完整生命周期，每个状态标注**当前元素个数**、**head位置**、**tail位置**。

#### 状态1：初始化完成（空队列）
```
缓冲区：[ 空 | 空 | 空 | 空 ]
索引：    0    1    2    3
指针：   head
         tail
状态：count=0，head=0，tail=0
说明：队列为空，可写入4个元素
```

#### 状态2：写入1个元素（值=10）
```
缓冲区：[ 10 | 空 | 空 | 空 ]
索引：    0    1    2    3
指针：   head  tail
状态：count=1，head=0，tail=1
说明：tail后移1位，count加1
```

#### 状态3：再写入2个元素（值=20、30）
```
缓冲区：[ 10 | 20 | 30 | 空 ]
索引：    0    1    2    3
指针：   head            tail
状态：count=3，head=0，tail=3
说明：剩余可写入1个元素
```

#### 状态4：写入第4个元素（值=40，队列满）
```
缓冲区：[ 10 | 20 | 30 | 40 ]
索引：    0    1    2    3
指针：   head
         tail
状态：count=4，head=0，tail=0
说明：tail绕回至0，count等于capacity，队列满
⚠️ 关键：此时head==tail，但count=4≠0，所以判断为满而非空
```

#### 状态5：读取1个元素（队首10）
```
缓冲区：[ 空 | 20 | 30 | 40 ]
索引：    0    1    2    3
指针：        head
         tail
状态：count=3，head=1，tail=0
说明：head后移1位，count减1
```

#### 状态6：再读取2个元素（20、30）
```
缓冲区：[ 空 | 空 | 空 | 40 ]
索引：    0    1    2    3
指针：                  head
         tail
状态：count=1，head=3，tail=0
说明：剩余1个元素（40）
```

#### 状态7：写入2个元素（值=50、60，触发环形绕回）
```
缓冲区：[ 50 | 60 | 空 | 40 ]
索引：    0    1    2    3
指针：                  head
                  tail
状态：count=3，head=3，tail=2
说明：tail先写索引0（50），再写索引1（60），最后停在索引2
```

#### 状态8：覆盖写入1个元素（值=70，队列满时自动覆写）
```
先写入70使队列满：
缓冲区：[ 50 | 60 | 70 | 40 ]
索引：    0    1    2    3
指针：                  head
                        tail
状态：count=4，head=3，tail=3

再覆盖写入80（最旧数据40被丢弃）：
缓冲区：[ 50 | 60 | 70 | 80 ]
索引：    0    1    2    3
指针：        head
                        tail
状态：count=4，head=0，tail=0
说明：head和tail同时后移1位，count保持4不变
```

#### 状态9：读取所有元素（队列空）
```
依次读取50、60、70、80后：
缓冲区：[ 空 | 空 | 空 | 空 ]
索引：    0    1    2    3
指针：                        head
                        tail
状态：count=0，head=0，tail=0
说明：回到初始空队列状态
```

---

## 📖 核心接口详解（补充图示版）
### 1. 初始化与销毁
> ⚠️ MCU强烈推荐使用**静态初始化**，避免动态内存分配失败

| 接口 | 功能 | 适用场景 |
|------|------|----------|
| `queue_init_static` | 用用户提供的静态缓冲区初始化队列 | 所有MCU环境 |
| `queue_create` | 动态分配内存创建队列 | Linux/有malloc的平台 |
| `queue_destroy` | 销毁队列，释放内部资源 | 所有场景 |
| `queue_clear` | 清空队列（不释放缓冲区） | 重置队列复用 |

#### 1.1 静态初始化（首选）
```c
/**
 * @param q 队列句柄指针（提前定义的queue_t变量地址）
 * @param buf 静态缓冲区地址（比如数组名）
 * @param capacity 队列最大能存储的元素个数
 * @param element_size 单个元素的字节数（用sizeof(类型)获取）
 * @return QUEUE_OK 成功，其他为错误码
 */
queue_err_t queue_init_static(queue_t *q, void *buf, uint32_t capacity, uint32_t element_size);
```
**示例**：创建一个能存10个float的队列
```c
queue_t float_q;
float float_buf[10];  // 缓冲区大小必须 >= 10 * sizeof(float)
queue_err_t err = queue_init_static(&float_q, float_buf, 10, sizeof(float));
```

#### 1.2 动态创建（Linux专用）
```c
/**
 * @return 成功返回队列指针，失败返回NULL
 * @note 使用完必须先调用queue_destroy，再free队列句柄本身
 */
queue_t *queue_create(uint32_t capacity, uint32_t element_size);
```
**示例**：动态创建一个能存20个结构体的队列
```c
typedef struct {
    uint16_t id;
    float temperature;
    uint8_t status;
} sensor_data_t;

queue_t *sensor_q = queue_create(20, sizeof(sensor_data_t));
if (!sensor_q) {
    printf("内存不足，创建队列失败\n");
}

// 使用完销毁
queue_destroy(sensor_q);
free(sensor_q);  // 必须手动释放队列句柄
```

#### 1.3 清空队列
```c
queue_err_t queue_clear(queue_t *q);
```
**效果图示**：
```
清空前列队：[10,20,30]，count=3，head=0，tail=3
清空后队列：[空,空,空]，count=0，head=0，tail=0
```
**示例**：
```c
queue_clear(&q);
printf("清空后元素个数：%d\n", queue_count(&q));  // 输出0
```

---

### 2. 写入数据（3种方式，补充图示）
| 接口 | 功能 | 特点 |
|------|------|------|
| `queue_write_back` | 向队尾写入元素 | 标准FIFO用法，满了返回失败 |
| `queue_write_front` | 向队首插入元素 | 双端队列用法，插入后成为新队首 |
| `queue_write_back_overwrite` | 向队尾写入，满了自动覆盖最旧数据 | 永远不会失败，适合日志/数据采集 |

#### 2.1 普通写入队尾（标准FIFO）
```c
queue_err_t queue_write_back(queue_t *q, const void *data);
```
**操作图示**：
```
写入前：[10,20,30]，count=3，head=0，tail=3
写入40后：[10,20,30,40]，count=4，head=0，tail=0（绕回）
```
**示例**：
```c
int val = 123;
queue_err_t err = queue_write_back(&q, &val);
if (err == QUEUE_FULL) {
    printf("队列满了，写入失败\n");
}
```

#### 2.2 写入队首（双端队列）
```c
queue_err_t queue_write_front(queue_t *q, const void *data);
```
**操作图示**：
```
写入前：[10,20,30]，count=3，head=0，tail=3
写入队首40后：[40,10,20,30]，count=4，head=3（head前移1位），tail=3
```
**示例**：
```c
int val = 4;
queue_write_front(&q, &val);
queue_peek_front(&q, &out);  // out=4
```

#### 2.3 覆盖写入（永不失败）
```c
queue_err_t queue_write_back_overwrite(queue_t *q, const void *data);
```
**操作图示**：
```
队列已满：[1,2,3,4]，count=4，head=0，tail=0
覆盖写入5后：[2,3,4,5]，count=4，head=1，tail=1（最旧的1被丢弃）
```
**示例**：
```c
// 循环写入100个数据，队列满了自动覆盖旧数据
for (int i = 0; i < 100; i++) {
    queue_write_back_overwrite(&q, &i);
}
```

---

### 3. 读取数据（2种方式，补充图示）
| 接口 | 功能 | 特点 |
|------|------|------|
| `queue_read_front` | 从队首读取并移除元素 | 标准FIFO出队 |
| `queue_read_back` | 从队尾读取并移除元素 | 双端队列出队，相当于栈的pop |

#### 3.1 从队首读取（标准FIFO）
```c
queue_err_t queue_read_front(queue_t *q, void *out);
```
**操作图示**：
```
读取前：[10,20,30]，count=3，head=0，tail=3
读取后：[空,20,30]，count=2，head=1，tail=3，读出值=10
```
**示例**：
```c
int out;
queue_err_t err = queue_read_front(&q, &out);
if (err == QUEUE_EMPTY) {
    printf("队列为空，读取失败\n");
}
```

#### 3.2 从队尾读取（栈用法）
```c
queue_err_t queue_read_back(queue_t *q, void *out);
```
**操作图示**：
```
读取前：[10,20,30]，count=3，head=0，tail=3
读取后：[10,20,空]，count=2，head=0，tail=2，读出值=30
```
**示例**：
```c
int out;
queue_read_back(&q, &out);  // out=30
```

---

### 4. 查看数据（不移除元素，补充图示）
| 接口 | 功能 |
|------|------|
| `queue_peek_front` | 查看队首元素 |
| `queue_peek_back` | 查看队尾元素 |
| `queue_at` | 按逻辑索引随机访问元素 |

#### 4.1 查看队首/队尾
```c
// 查看队首
int front;
queue_peek_front(&q, &front);

// 查看队尾
int back;
queue_peek_back(&q, &back);
```
**操作图示**：
```
队列：[10,20,30]，count=3，head=0，tail=3
peek_front=10，peek_back=30
操作后队列不变：count=3，head=0，tail=3
```

#### 4.2 随机访问（补充索引计算原理）
```c
/**
 * @param index 逻辑索引，0=队首，count-1=队尾
 * @note 物理索引计算公式：real_idx = (head + index) % capacity
 */
queue_err_t queue_at(queue_t *q, uint32_t index, void *out);
```
**操作图示**：
```
队列：[空,20,30,40]，count=3，head=1，tail=0，capacity=4
逻辑索引0 → 物理索引(1+0)%4=1 → 值=20
逻辑索引1 → 物理索引(1+1)%4=2 → 值=30
逻辑索引2 → 物理索引(1+2)%4=3 → 值=40
```
**示例**：
```c
// 队列中有[10,20,30,40]
int val;
queue_at(&q, 0, &val);  // val=10（队首）
queue_at(&q, 2, &val);  // val=30
queue_at(&q, 3, &val);  // val=40（队尾）
```

---

### 5. 状态查询
| 接口 | 功能 | 返回值 |
|------|------|--------|
| `queue_is_empty` | 判断队列是否为空 | true=空，false=非空 |
| `queue_is_full` | 判断队列是否已满 | true=满，false=未满 |
| `queue_count` | 获取当前元素个数 | 0~capacity |
| `queue_free_space` | 获取剩余可写入个数 | 0~capacity |
| `queue_capacity` | 获取队列最大容量 | 初始化时设置的值 |

**示例**：
```c
if (queue_is_empty(&q)) {
    printf("队列为空\n");
}

if (queue_is_full(&q)) {
    printf("队列已满\n");
}

printf("当前元素：%d，剩余空间：%d，总容量：%d\n",
       queue_count(&q),
       queue_free_space(&q),
       queue_capacity(&q));
```

---

### 6. 批量操作（高性能，补充图示）
> 一次加锁完成多个元素的读写，比循环调用单个接口效率高10倍以上

| 接口 | 功能 | 返回值 |
|------|------|--------|
| `queue_write_back_bulk` | 批量向队尾写入元素 | 实际写入的个数 |
| `queue_read_front_bulk` | 批量从队首读取元素 | 实际读取的个数 |

#### 6.1 批量写入
```c
/**
 * @param n 期望写入的元素个数
 * @return 实际写入的个数（如果剩余空间不足，返回能写入的最大值）
 */
uint32_t queue_write_back_bulk(queue_t *q, const void *data, uint32_t n);
```
**操作图示**：
```
写入前：[空,空,空,空]，count=0，head=0，tail=0
批量写入[10,20,30,40,50]（期望5个，容量4）
写入后：[10,20,30,40]，count=4，head=0，tail=0
实际写入个数=4
```
**示例**：
```c
int src[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
// 批量写入10个元素，队列容量只有8，实际写入8个
uint32_t written = queue_write_back_bulk(&q, src, 10);
printf("实际写入：%d个\n", written);  // 输出8
```

#### 6.2 批量读取
```c
uint32_t queue_read_front_bulk(queue_t *q, void *out, uint32_t n);
```
**操作图示**：
```
读取前：[10,20,30,40]，count=4，head=0，tail=0
批量读取3个元素
读取后：[空,空,空,40]，count=1，head=3，tail=0
实际读取个数=3，读出数据[10,20,30]
```
**示例**：
```c
int dst[8];
// 批量读取8个元素
uint32_t read = queue_read_front_bulk(&q, dst, 8);
printf("实际读取：%d个\n", read);
```

---

## ⚙️ 编译配置
通过定义以下宏来切换队列的工作模式，在`queue.h`顶部或工程编译选项中添加即可。

| 宏定义 | 功能 |
|--------|------|
| `QUEUE_USE_PTHREAD` | 使用POSIX pthread互斥锁（Linux多线程环境） |
| `QUEUE_NO_MALLOC` | 禁用动态内存分配接口（MCU环境推荐） |
| `QUEUE_CRITICAL_ENTER` | 自定义裸机临界区进入（如关中断） |
| `QUEUE_CRITICAL_EXIT` | 自定义裸机临界区退出（如开中断） |

### 常用配置示例
#### 1. STM32裸机配置
```c
// 在queue.h顶部添加
#define QUEUE_NO_MALLOC
#define QUEUE_CRITICAL_ENTER(q)  __disable_irq()
#define QUEUE_CRITICAL_EXIT(q)   __enable_irq()
```

#### 2. Linux多线程配置
```c
// 编译时添加宏定义
gcc -DQUEUE_USE_PTHREAD main.c queue.c -lpthread -o app
```

---

## 🔍 补充技术细节（之前未提到的内容）
### 1. 空/满判断逻辑
本库采用`count`变量判断空/满，而非传统的`head==tail`，优势：
- 不浪费任何缓冲区槽位（传统方法浪费1个）
- 判断逻辑更直观：`count==0`为空，`count==capacity`为满
- 避免了`head==tail`时无法区分空/满的歧义

### 2. volatile关键字的作用
`head`、`tail`、`count`变量都加了`volatile`修饰，目的是：
- 防止编译器优化这些变量的读写操作
- 确保在多线程或中断环境下，变量的修改对所有上下文可见
- 避免出现“变量值已经改变，但CPU仍从寄存器读取旧值”的问题

### 3. 锁的粒度与原子性
所有队列操作（写入、读取、查询）都在**加锁→操作→解锁**的保护下进行，保证每个操作都是原子性的。即使在多线程或中断环境下，也不会出现数据错乱。

### 4. 静态内存与动态内存的选择
- **静态内存**：MCU环境首选，编译时确定内存大小，无运行时分配失败风险，无内存碎片
- **动态内存**：Linux环境使用，灵活分配内存，但需要注意内存泄漏问题（必须正确销毁队列）

### 5. 双端队列的使用场景
- 实现栈功能：用`queue_write_back`+`queue_read_back`（后进先出）
- 实现双向链表功能：支持两端插入和删除
- 优先级队列辅助：高优先级数据插入队首，低优先级数据插入队尾

### 6. 覆盖写入模式的使用场景
- 日志系统：只保留最近的N条日志，旧日志自动覆盖
- 数据采集：只保留最近的N个采样点，旧数据自动丢弃
- 环形缓冲区：用于高速数据传输，避免缓冲区溢出

---

## ❌ 常见问题与避坑指南
1. **静态缓冲区大小计算错误**
   > 错误：`int buf[5*sizeof(int)];`  
   > 正确：`int buf[5];`（数组元素个数就是队列容量）

2. **动态创建队列后忘记释放**
   > 必须先调用`queue_destroy`释放缓冲区，再`free`队列句柄本身

3. **裸机环境未配置临界区**
   > 如果在中断和主循环中同时操作队列，必须配置关中断临界区，否则会出现数据错乱

4. **写入/读取时数据指针为空**
   > 所有接口都有NULL指针检查，传入NULL会返回`QUEUE_NULLPTR`错误

5. **随机访问索引越界**
   > `queue_at`的索引范围是`[0, count-1]`，越界会返回`QUEUE_INVAL`错误

6. **多线程环境未启用pthread锁**
   > Linux多线程环境下必须定义`QUEUE_USE_PTHREAD`，否则队列不是线程安全的

---

## 📝 完整示例：传感器数据采集队列
```c
#include "queue.h"
#include <stdio.h>
#include <stdint.h>

// 定义传感器数据结构体
typedef struct {
    uint16_t id;
    float temperature;
    uint8_t status;
} sensor_data_t;

int main(void)
{
    // 初始化队列：容量10，存储sensor_data_t类型
    queue_t sensor_q;
    sensor_data_t buf[10];
    queue_init_static(&sensor_q, buf, 10, sizeof(sensor_data_t));

    // 模拟采集5个传感器数据
    sensor_data_t data[] = {
        {1, 25.5f, 0x01},
        {2, 26.3f, 0x01},
        {3, 24.8f, 0x01},
        {4, 27.1f, 0x01},
        {5, 25.9f, 0x01}
    };

    // 批量写入队列
    uint32_t written = queue_write_back_bulk(&sensor_q, data, 5);
    printf("采集完成，写入%d个数据\n", written);

    // 逐个读取并处理
    sensor_data_t out;
    while (!queue_is_empty(&sensor_q)) {
        queue_read_front(&sensor_q, &out);
        printf("传感器%d：温度=%.1f℃，状态=0x%02X\n",
               out.id, out.temperature, out.status);
    }

    // 销毁队列
    queue_destroy(&sensor_q);
    return 0;
}
```

### 运行结果
```
采集完成，写入5个数据
传感器1：温度=25.5℃，状态=0x01
传感器2：温度=26.3℃，状态=0x01
传感器3：温度=24.8℃，状态=0x01
传感器4：温度=27.1℃，状态=0x01
传感器5：温度=25.9℃，状态=0x01
```

---

## 🧪 运行自带测试用例
库中提供了完整的测试用例`test_queue.c`，可以验证所有接口的正确性：
```bash
# 编译测试程序（多线程模式）
gcc -Wall -Wextra -O2 -DQUEUE_USE_PTHREAD test_queue.c queue.c -lpthread -o test_queue
# 运行测试
./test_queue
```
所有测试用例通过后会输出：
```
========================================
  结果: PASS=XX  FAIL=0  总计=XX
========================================
```

---

## 📋 错误码对照表
| 错误码 | 值 | 含义 |
|--------|----|------|
| `QUEUE_OK` | 0 | 操作成功 |
| `QUEUE_FULL` | -1 | 队列已满 |
| `QUEUE_EMPTY` | -2 | 队列为空 |
| `QUEUE_NULLPTR` | -3 | 空指针参数 |
| `QUEUE_INVAL` | -4 | 非法参数 |
| `QUEUE_NOMEM` | -5 | 内存不足 |