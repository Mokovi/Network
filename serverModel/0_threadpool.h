#ifndef _THREAD_POOL_H_
#define _THREAD_POOL_H_

#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>

// ====================== 配置参数（可按需修改） ======================
#define DEFAULT_THREAD_NUM 4    // 默认线程数
#define MAX_TASK_QUEUE 1024     // 任务队列最大长度（0表示无限制）

// ====================== 任务结构体（通用任务封装） ======================
/**
 * @brief 线程池任务结构体
 * @note 用函数指针+void*参数适配任意任务逻辑
 */
typedef struct task {
    void (*func)(void*);        // 任务函数指针
    void* arg;                  // 任务函数参数
    struct task* next;          // 链表节点，指向下一个任务
} task_t;

// ====================== 线程池核心结构体 ======================
/**
 * @brief 线程池结构体
 * @note 所有成员变量需通过互斥锁保护，避免多线程竞争
 */
typedef struct thread_pool {
    pthread_t* threads;         // 线程数组
    int thread_num;             // 线程数量
    task_t* task_queue;         // 任务队列（链表头节点）
    int task_count;             // 当前任务数
    int max_task;               // 最大任务数（0表示无限制）
    pthread_mutex_t mutex;      // 保护任务队列的互斥锁
    pthread_cond_t cond;        // 任务通知条件变量
    int is_running;             // 线程池运行标记（1：运行，0：停止）
} thread_pool_t;

// ====================== 全局静态函数声明（内部使用） ======================
static void* worker_loop(void* arg);  // 工作线程函数
static task_t* task_create(void (*func)(void*), void* arg); // 创建任务
static void task_destroy(task_t* task); // 销毁任务

// ====================== 线程池核心接口 ======================
/**
 * @brief 创建线程池
 * @param thread_num 线程数量（≤0则使用默认值）
 * @return 成功返回线程池指针，失败返回NULL
 */
thread_pool_t* thread_pool_create(int thread_num);

/**
 * @brief 向线程池提交任务
 * @param pool 线程池指针
 * @param func 任务函数指针
 * @param arg 任务函数参数
 * @return 成功返回0，失败返回-1
 */
int thread_pool_add_task(thread_pool_t* pool, void (*func)(void*), void* arg);

/**
 * @brief 销毁线程池
 * @param pool 线程池指针
 * @param force 关闭模式：0-等待所有任务完成后退出，1-强制退出（丢弃未执行任务）
 */
void thread_pool_destroy(thread_pool_t* pool, int force);

// ====================== 内部实现函数 ======================
/**
 * @brief 工作线程函数（循环获取并执行任务）
 * @param arg 线程池指针
 * @return NULL
 */
static void* worker_loop(void* arg) {
    thread_pool_t* pool = (thread_pool_t*)arg;
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // 允许线程被取消

    while (1) {
        // 1. 加锁获取任务
        pthread_mutex_lock(&pool->mutex);

        // 2. 等待任务（队列为空且线程池运行中）
        while (pool->task_count == 0 && pool->is_running) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }

        // 3. 检查线程池是否停止
        if (!pool->is_running) {
            pthread_mutex_unlock(&pool->mutex);
            pthread_exit(NULL); // 退出线程
        }

        // 4. 从队列头部取出任务
        task_t* task = pool->task_queue;
        pool->task_queue = task->next;
        pool->task_count--;

        pthread_mutex_unlock(&pool->mutex);

        // 5. 执行任务（解锁后执行，避免长时间占用锁）
        if (task && task->func) {
            task->func(task->arg);
        }

        // 6. 销毁已执行的任务
        task_destroy(task);
    }

    return NULL;
}

/**
 * @brief 创建单个任务
 * @param func 任务函数
 * @param arg 任务参数
 * @return 任务指针，失败返回NULL
 */
static task_t* task_create(void (*func)(void*), void* arg) {
    task_t* task = (task_t*)malloc(sizeof(task_t));
    if (!task) {
        perror("malloc task failed");
        return NULL;
    }
    task->func = func;
    task->arg = arg;
    task->next = NULL;
    return task;
}

/**
 * @brief 销毁单个任务
 * @param task 任务指针
 */
static void task_destroy(task_t* task) {
    if (task) {
        free(task);
    }
}

/**
 * @brief 创建线程池（实现）
 */
thread_pool_t* thread_pool_create(int thread_num) {
    // 1. 参数校验
    if (thread_num <= 0) {
        thread_num = DEFAULT_THREAD_NUM;
    }

    // 2. 初始化线程池结构体
    thread_pool_t* pool = (thread_pool_t*)malloc(sizeof(thread_pool_t));
    if (!pool) {
        perror("malloc thread_pool failed");
        return NULL;
    }
    memset(pool, 0, sizeof(thread_pool_t));

    // 3. 设置线程池参数
    pool->thread_num = thread_num;
    pool->max_task = MAX_TASK_QUEUE;
    pool->is_running = 1; // 标记为运行状态

    // 4. 创建线程数组
    pool->threads = (pthread_t*)malloc(sizeof(pthread_t) * thread_num);
    if (!pool->threads) {
        perror("malloc threads failed");
        free(pool);
        return NULL;
    }

    // 5. 初始化互斥锁和条件变量
    if (pthread_mutex_init(&pool->mutex, NULL) != 0) {
        perror("pthread_mutex_init failed");
        free(pool->threads);
        free(pool);
        return NULL;
    }
    if (pthread_cond_init(&pool->cond, NULL) != 0) {
        perror("pthread_cond_init failed");
        pthread_mutex_destroy(&pool->mutex);
        free(pool->threads);
        free(pool);
        return NULL;
    }

    // 6. 创建工作线程
    for (int i = 0; i < thread_num; i++) {
        if (pthread_create(&pool->threads[i], NULL, worker_loop, pool) != 0) {
            perror("pthread_create failed");
            // 销毁已创建的线程
            for (int j = 0; j < i; j++) {
                pthread_cancel(pool->threads[j]);
                pthread_join(pool->threads[j], NULL);
            }
            pthread_mutex_destroy(&pool->mutex);
            pthread_cond_destroy(&pool->cond);
            free(pool->threads);
            free(pool);
            return NULL;
        }
    }

    printf("thread pool created: %d threads, max task: %d\n", thread_num, pool->max_task);
    return pool;
}

/**
 * @brief 提交任务到线程池（实现）
 */
int thread_pool_add_task(thread_pool_t* pool, void (*func)(void*), void* arg) {
    // 1. 参数校验
    if (!pool || !func || pool->is_running == 0) {
        fprintf(stderr, "invalid param or pool stopped\n");
        return -1;
    }

    // 2. 加锁保护任务队列
    pthread_mutex_lock(&pool->mutex);

    // 3. 检查任务队列是否已满（如果设置了最大任务数）
    if (pool->max_task > 0 && pool->task_count >= pool->max_task) {
        fprintf(stderr, "task queue full, reject task\n");
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    // 4. 创建新任务
    task_t* new_task = task_create(func, arg);
    if (!new_task) {
        pthread_mutex_unlock(&pool->mutex);
        return -1;
    }

    // 5. 将任务添加到队列尾部
    if (!pool->task_queue) {
        pool->task_queue = new_task; // 队列为空，直接作为头节点
    } else {
        task_t* tmp = pool->task_queue;
        while (tmp->next) {
            tmp = tmp->next;
        }
        tmp->next = new_task;
    }
    pool->task_count++;

    // 6. 唤醒等待的工作线程
    pthread_cond_signal(&pool->cond);
    pthread_mutex_unlock(&pool->mutex);

    return 0;
}

/**
 * @brief 销毁线程池（实现）
 */
void thread_pool_destroy(thread_pool_t* pool, int force) {
    if (!pool) return;

    // 1. 加锁标记线程池停止
    pthread_mutex_lock(&pool->mutex);
    pool->is_running = 0;
    pthread_mutex_unlock(&pool->mutex);

    // 2. 唤醒所有等待的线程
    pthread_cond_broadcast(&pool->cond);

    // 3. 等待所有线程退出
    for (int i = 0; i < pool->thread_num; i++) {
        if (pthread_join(pool->threads[i], NULL) != 0) {
            perror("pthread_join failed");
        }
    }

    // 4. 清理任务队列（强制退出时销毁未执行任务）
    if (force) {
        task_t* tmp = pool->task_queue;
        while (tmp) {
            task_t* next = tmp->next;
            task_destroy(tmp);
            tmp = next;
        }
        pool->task_queue = NULL;
        pool->task_count = 0;
    } else {
        // 非强制退出：等待所有任务执行完（已由工作线程处理）
        pthread_mutex_lock(&pool->mutex);
        while (pool->task_count > 0) {
            pthread_cond_wait(&pool->cond, &pool->mutex);
        }
        pthread_mutex_unlock(&pool->mutex);
    }

    // 5. 释放资源
    pthread_mutex_destroy(&pool->mutex);
    pthread_cond_destroy(&pool->cond);
    free(pool->threads);
    free(pool);

    printf("thread pool destroyed (force: %d)\n", force);
}

#endif // _THREAD_POOL_H_