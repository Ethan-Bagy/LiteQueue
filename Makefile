# ============================================================
#  Makefile — queue 消息队列
#
#  用法:
#    make          编译库 + 测试程序 (默认 pthread 模式)
#    make run      编译并运行测试
#    make bare     裸机模式 (不使用 pthread)
#    make clean    清理编译产物
#
# @author Ethan.Ba
# ============================================================

CC      := gcc
CFLAGS  := -Wall -Wextra -O2 -std=c11
TARGET  := test_queue
SRCS    := queue.c test_queue.c
OBJS    := $(SRCS:.c=.o)

# ---------- 默认目标: pthread 模式 ----------
.PHONY: all
all: CFLAGS += -DQUEUE_USE_PTHREAD
all: LDFLAGS += -lpthread
all: $(TARGET)
	@echo ""
	@echo "编译成功 → ./$(TARGET)"

# ---------- 裸机模式 (MCU，不依赖 pthread) ----------
.PHONY: bare
bare: $(TARGET)
	@echo ""
	@echo "编译成功 (裸机模式) → ./$(TARGET)"

# ---------- 编译并运行 ----------
.PHONY: run
run: all
	@echo ""
	@./$(TARGET)

# ---------- 链接 ----------
$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# ---------- 编译各 .c ----------
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------- 清理 ----------
.PHONY: clean
clean:
	rm -f $(OBJS) $(TARGET)
	@echo "清理完成"
