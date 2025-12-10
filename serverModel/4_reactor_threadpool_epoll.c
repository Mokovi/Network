// 4_reactor_threadpool_epoll.c
// 单线程 Reactor (epoll) + 线程池 (workers) 的 echo server，纯 C 实现
// 编译: gcc -std=c11 -O2 reactor_threadpool_epoll.c -o server -pthread
// 运行: ./server [port]
// 说明:
//  - epoll 使用 ET（边沿触发）+ ONESHOT（每次通知后需手动 re-arm）
//  - 主线程负责 accept + epoll_wait（事件分发）
//  - worker 线程负责真正的 I/O 读写（read until EAGAIN / write until EAGAIN）并在完成后重新 arm
//  - 连接通过 connection_t 结构体管理，使用互斥保护缓冲区 / 状态，避免竞态

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>
#include <signal.h>
#include <errno.h>
// 引入线程池头文件
#include "0_threadpool.h"

#define DEAFULT_PORT 13145
#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096

volatile int global_running = 1;
// 全局线程池指针（Reactor主线程创建，所有事件共享）
thread_pool_t* g_thread_pool = NULL;

/**
 * @brief 信号处理函数：触发优雅退出，销毁线程池
 */
void signal_handler(int sig) {
    global_running = 0;
    printf("\nSignal %d received, shutting down...\n", sig);
    // 销毁线程池（等待所有任务完成）
    if (g_thread_pool) {
        thread_pool_destroy(g_thread_pool, 0);
    }
}

// 前置声明
struct connection_s;
typedef struct connection_s{
    int fd;
    struct sockaddr_in addr;
    int epoll_fd; // 新增：关联的epoll_fd，用于任务中重新注册事件
    void (*read_handler)(int, struct connection_s*); // 读事件处理函数指针
    void (*write_handler)(int, struct connection_s*); // 写事件处理函数指针
    pthread_mutex_t lock; // 连接锁，保护缓冲区和状态
    char* wbuffer; // 写缓冲区
    size_t wbuffer_size; // 写缓冲区大小
    size_t wbuffer_sent; // 已发送数据大小
    char* read_buffer; // 读缓冲区
    size_t read_buffer_size; // 读缓冲区大小
} connection_t;

typedef enum {
    CONN_ACCEPTING,
    CONN_CLIENT,
}connection_type_t; 

/**
 * @brief 创建连接结构体
 * @param fd 套接字FD
 * @param addr 客户端地址
 * @param read_handler 读处理函数
 * @param write_handler 写处理函数
 * @param type 连接类型（监听/客户端）
 * @return 连接结构体指针
 */
connection_t* connection_create(int fd, struct sockaddr_in addr,
                                void (*read_handler)(int, connection_t*), 
                                void (*write_handler)(int, connection_t*), 
                                connection_type_t type) {
    connection_t* conn = (connection_t*)malloc(sizeof(connection_t));
    memset(conn, 0, sizeof(connection_t));
    conn->fd = fd;
    conn->addr = addr;
    conn->read_handler = read_handler;
    conn->write_handler = write_handler;
    if (type == CONN_CLIENT) {
        conn->read_buffer = (char*)malloc(BUFFER_SIZE);
        conn->read_buffer_size = BUFFER_SIZE;
        conn->wbuffer = (char*)malloc(BUFFER_SIZE);
        conn->wbuffer_size = BUFFER_SIZE;
        pthread_mutex_init(&conn->lock, NULL); // 初始化连接锁
    }
    return conn;
}

/**
 * @brief 销毁连接结构体，释放资源
 * @param conn 连接结构体指针
 * @return 成功0，失败-1
 */
int connection_destroy(connection_t* conn) {
    if (!conn) return -1;
    close(conn->fd);
    if (conn->read_buffer) free(conn->read_buffer);
    if (conn->wbuffer) free(conn->wbuffer);
    free(conn);
    return 0;
}

/**
 * @brief 设置文件描述符为非阻塞模式（ET模式必需）
 * @param fd 目标FD
 * @return 成功0，失败-1
 */
int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

/**
 * @brief 向epoll添加FD，注册事件（核心：添加ET+ONESHOT）
 * @param epoll_fd epoll实例FD
 * @param fd 目标FD
 * @param conn 关联的连接结构体
 * @param events 事件类型（EPOLLIN/EPOLLOUT）
 * @return 成功0，失败-1
 * @note ET（EPOLLET）：边缘触发，仅在状态变化时触发事件
 * @note ONESHOT（EPOLLONESHOT）：事件触发后自动禁用，需手动重新注册
 */
int epoll_add_fd(int epoll_fd, int fd, connection_t* conn, uint32_t events) {
    struct epoll_event ev;
    // 核心修改1：添加ET+ONESHOT
    ev.events = events | EPOLLET | EPOLLONESHOT; 
    ev.data.ptr = conn;
    // 关联epoll_fd到连接结构体，供后续重新注册事件使用
    conn->epoll_fd = epoll_fd;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}

/**
 * @brief 从epoll删除FD
 * @param epoll_fd epoll实例FD
 * @param fd 目标FD
 * @return 成功0，失败-1
 */
int epoll_del_fd(int epoll_fd, int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}

/**
 * @brief 修改epoll中FD的事件（用于ONESHOT后重新注册）
 * @param epoll_fd epoll实例FD
 * @param fd 目标FD
 * @param conn 关联的连接结构体
 * @param events 事件类型
 * @return 成功0，失败-1
 */
int epoll_mod_fd(int epoll_fd, int fd, connection_t* conn, uint32_t events) {
    struct epoll_event ev;
    ev.events = events | EPOLLET | EPOLLONESHOT; // 保持ET+ONESHOT
    ev.data.ptr = conn;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

// 前向声明
void accept_handler(int epoll_fd, connection_t* accept_conn);
void read_worker_task(void* arg);    // 读任务（线程池执行）
void write_worker_task(void* arg);   // 写任务（线程池执行）
void read_handler(int epoll_fd, connection_t* conn);
void write_handler(int epoll_fd, connection_t* conn);

/**
 * @brief 监听FD的读事件处理（接受新连接）
 * @param epoll_fd epoll实例FD
 * @param accept_conn 监听连接结构体
 * @note 仅由Reactor主线程处理，不提交到线程池（轻量操作）
 */
void accept_handler(int epoll_fd, connection_t* accept_conn) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_fd;
    // ET模式：循环accept直到无新连接
    while (1) {
        conn_fd = accept(accept_conn->fd, (struct sockaddr*)&client_addr, &client_len);
        if (conn_fd == -1) {
            break; // 无更多连接
        }
        // 设置客户端FD为非阻塞（ET模式必需）
        if (set_nonblocking(conn_fd) < 0) {
            perror("set_nonblocking");
            close(conn_fd);
            continue;
        }

        // 创建客户端连接结构体
        connection_t* conn = connection_create(conn_fd, client_addr, 
                                               read_handler, write_handler, CONN_CLIENT);

        // 注册客户端FD到epoll：EPOLLIN + ET + ONESHOT
        if (epoll_add_fd(epoll_fd, conn_fd, conn, EPOLLIN) < 0) {
            perror("epoll_add_fd");
            connection_destroy(conn);
            continue;
        }

        printf("Accepted connection from %s:%d, fd=%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               conn_fd);
    }
    epoll_mod_fd(epoll_fd, accept_conn->fd, accept_conn, EPOLLIN); // 重新注册accept事件
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("accept");
    }
}

/**
 * @brief 读任务（线程池执行）：实际处理客户端读数据
 * @param arg 连接结构体指针
 * @note 耗时操作移到线程池，Reactor主线程仅负责事件分发
 */
void read_worker_task(void* arg) {
    connection_t* conn = (connection_t*)arg;
    if (!conn || conn->fd < 0) return;

    ssize_t n;
    int have_pending_write = 0;
    // ET模式：循环读直到无数据
    while (1) {
        n = read(conn->fd, conn->read_buffer, conn->read_buffer_size-1);
        if (n > 0) {
            conn->read_buffer[n] = '\0';
            printf("[%s:%d][Thread %lu]: %s\n", 
                   inet_ntoa(conn->addr.sin_addr), 
                   ntohs(conn->addr.sin_port),
                   (unsigned long)pthread_self(), // 打印处理任务的线程ID
                   conn->read_buffer);
            // 回显数据：拷贝到写缓冲区
            pthread_mutex_lock(&conn->lock);
            memcpy(conn->wbuffer, conn->read_buffer, n);
            conn->wbuffer_sent = n;
            pthread_mutex_unlock(&conn->lock);
            have_pending_write = 1;
        } else if (n == 0) {
            // 客户端关闭连接
            printf("Client disconnected, fd=%d\n", conn->fd);
            epoll_del_fd(conn->epoll_fd, conn->fd);
            connection_destroy(conn);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 读完所有数据，重新注册读事件（ONESHOT必需）
                uint32_t newev = EPOLLIN;
                if (have_pending_write) newev |= EPOLLOUT;
                epoll_mod_fd(conn->epoll_fd, conn->fd, conn, newev);
                break;
            } else {
                perror("read");
                epoll_del_fd(conn->epoll_fd, conn->fd);
                connection_destroy(conn);
                return;
            }
        }
    }
}

/**
 * @brief 客户端读事件处理（Reactor主线程触发，提交到线程池）
 * @param epoll_fd epoll实例FD
 * @param conn 客户端连接结构体
 * @note 仅做任务提交，不处理实际逻辑，保证Reactor主线程轻量
 */
void read_handler(int epoll_fd, connection_t* conn) {
    // 将读任务提交到线程池
    if (thread_pool_add_task(g_thread_pool, read_worker_task, conn) != 0) {
        fprintf(stderr, "add read task failed, fd=%d\n", conn->fd);
        epoll_del_fd(epoll_fd, conn->fd);
        connection_destroy(conn);
    }
}

/**
 * @brief 写任务（线程池执行）：实际处理客户端写数据
 * @param arg 连接结构体指针
 */
void write_worker_task(void* arg) {
    connection_t* conn = (connection_t*)arg;
    if (!conn || conn->fd < 0) return;

    ssize_t n;
    // ET模式：循环写直到数据发送完毕
    while (conn->wbuffer_sent > 0) {
        pthread_mutex_lock(&conn->lock);
        n = write(conn->fd, conn->wbuffer, conn->wbuffer_sent);
        if (n > 0) {
            conn->wbuffer_sent -= n;
            if (conn->wbuffer_sent > 0) {
                memmove(conn->wbuffer, conn->wbuffer + n, conn->wbuffer_sent);
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 写缓冲区满，重新注册写事件（ONESHOT必需）
                epoll_mod_fd(conn->epoll_fd, conn->fd, conn, EPOLLOUT);
                break;
            } else {
                perror("write");
                epoll_del_fd(conn->epoll_fd, conn->fd);
                connection_destroy(conn);
                pthread_mutex_unlock(&conn->lock);
                return;
            }
        }
    }
    pthread_mutex_unlock(&conn->lock);
    // 数据发送完毕，切换回读事件
    if (conn->wbuffer_sent == 0) {
        epoll_mod_fd(conn->epoll_fd, conn->fd, conn, EPOLLIN);
    }
}

/**
 * @brief 客户端写事件处理（Reactor主线程触发，提交到线程池）
 * @param epoll_fd epoll实例FD
 * @param conn 客户端连接结构体
 */
void write_handler(int epoll_fd, connection_t* conn) {
    // 将写任务提交到线程池
    if (thread_pool_add_task(g_thread_pool, write_worker_task, conn) != 0) {
        fprintf(stderr, "add write task failed, fd=%d\n", conn->fd);
        epoll_del_fd(epoll_fd, conn->fd);
        connection_destroy(conn);
    }
}

/**
 * @brief Reactor核心事件循环
 * @param epoll_fd epoll实例FD
 * @param events epoll事件数组
 * @param max_events 最大事件数
 * @note 仅负责事件监听和分发，耗时逻辑由线程池处理
 */
void reactor_loop(int epoll_fd, struct epoll_event* events, int max_events) {
    while (global_running) {
        int n = epoll_wait(epoll_fd, events, max_events, 1000); // 1秒超时
        if (n < 0) {
            if (errno == EINTR) continue; // 信号中断，继续循环
            perror("epoll_wait");
            break;
        }

        // 遍历触发的事件，分发到对应处理函数
        for (int i = 0; i < n; i++) {
            connection_t* conn = (connection_t*)events[i].data.ptr;
            if (events[i].events & EPOLLIN) {
                if (conn->read_handler) {
                    conn->read_handler(epoll_fd, conn);
                }
            }
            if (events[i].events & EPOLLOUT) {
                if (conn->write_handler) {
                    conn->write_handler(epoll_fd, conn);
                }
            }
        }
    }
}

int main(int argc, char* argv[]) {
    int port = DEAFULT_PORT;
    if (argc >= 2) {
        port = atoi(argv[1]);
    }

    // 注册信号处理函数（优雅退出）
    signal(SIGINT, signal_handler);

    // 1. 创建监听FD
    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    // 2. 绑定地址
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    // 3. 设置监听FD为非阻塞
    if(set_nonblocking(listen_fd) < 0) {
        perror("set_nonblocking");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (bind(listen_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    if (listen(listen_fd, SOMAXCONN) < 0) {
        perror("listen");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 4. 创建epoll实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 5. 创建监听连接结构体
    connection_t* listen_conn = (connection_t*)malloc(sizeof(connection_t));
    memset(listen_conn, 0, sizeof(connection_t));
    listen_conn->fd = listen_fd;
    listen_conn->read_handler = accept_handler;
    listen_conn->write_handler = NULL;

    // 6. 注册监听FD到epoll：EPOLLIN + ET + ONESHOT
    if (epoll_add_fd(epoll_fd, listen_fd, listen_conn, EPOLLIN) < 0) {
        perror("epoll_add_fd");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 7. 创建线程池（4个工作线程）
    g_thread_pool = thread_pool_create(4);
    if (!g_thread_pool) {
        fprintf(stderr, "create thread pool failed\n");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    memset(events, 0, sizeof(events));
    printf("Server listening on port %d (Reactor+ThreadPool, ET+ONESHOT)\n", port);

    // 7. 启动Reactor事件循环
    reactor_loop(epoll_fd, events, MAX_EVENTS);

    // 8. 资源清理
    close(listen_fd);
    close(epoll_fd);
    free(listen_conn);

    printf("End.\n");
    return 0;
}
