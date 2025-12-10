#include "0_threadpool.h"
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <pthread.h> // 替换threads.h（POSIX线程用pthread.h）
#include <string.h>
#include <stdio.h>
#include <signal.h>
#include <errno.h>

#define PORT 13145
#define BUFFER_SIZE 1024
#define THREAD_NUM 4

// 全局退出标记（信号安全）
volatile sig_atomic_t g_exit_flag = 0;

typedef struct clientinfo_s {
    int conn_fd;
    struct sockaddr_in client_addr;
} clientinfo_t;

/**
 * @brief 信号处理函数：捕获Ctrl+C，触发优雅退出
 */
void handle_sigint(int sig) {
    (void)sig;
    g_exit_flag = 1;
    printf("\n[Server] 收到退出信号，准备关闭...\n");
}

/**
 * @brief 忽略SIGPIPE信号，避免send导致程序崩溃
 */
void handle_sigpipe(int sig) {
    (void)sig;
}

/**
 * @brief 处理客户端连接（线程池任务函数）
 * @param arg clientinfo_t指针
 */
void handle_client(void* arg) {
    clientinfo_t* client = (clientinfo_t*)arg;
    if (!client || client->conn_fd < 0) {
        free(client); // 入参无效，直接释放
        return;
    }

    // 线程安全的IP转换（替换inet_ntoa）
    char client_ip[INET_ADDRSTRLEN] = {0};
    inet_ntop(AF_INET, &client->client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
    uint16_t client_port = ntohs(client->client_addr.sin_port);

    printf("[Client %s:%d] connected (fd: %d).\n", client_ip, client_port, client->conn_fd);

    char buffer[BUFFER_SIZE] = {0};
    while (!g_exit_flag) {
        ssize_t n = recv(client->conn_fd, buffer, BUFFER_SIZE - 1, 0);
        if (n < 0) {
            // 处理可重试错误（比如信号中断）
            if (errno == EINTR) continue;
            perror("[Error] recv failed");
            break;
        } else if (n == 0) {
            printf("[Client %s:%d] disconnected (fd: %d).\n", client_ip, client_port, client->conn_fd);
            break;
        }

        // 正常接收数据，回显
        buffer[n] = '\0';
        printf("[Client %s:%d (fd: %d)]: %s\n", client_ip, client_port, client->conn_fd, buffer);
        
        // 处理send错误（避免SIGPIPE）
        ssize_t send_len = send(client->conn_fd, buffer, n, MSG_NOSIGNAL);
        if (send_len < 0) {
            perror("[Error] send failed");
            break;
        }

        memset(buffer, 0, BUFFER_SIZE); // 清空缓冲区
    }

    // 资源清理（先关闭FD，再释放内存）
    close(client->conn_fd);
    free(client); // ❶ 最后释放，避免野指针
}

int main() {
    // 注册信号处理（优雅退出+避免SIGPIPE）
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, handle_sigpipe);

    // 1. 创建监听FD（带错误检查）
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("[Error] socket create failed");
        return -1;
    }

    // 2. 设置端口复用
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        perror("[Error] setsockopt failed");
        close(server_fd);
        return -1;
    }

    // 3. 绑定地址（带错误检查）
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("[Error] bind failed");
        close(server_fd);
        return -1;
    }

    // 4. 监听端口（带错误检查）
    if (listen(server_fd, 10) < 0) {
        perror("[Error] listen failed");
        close(server_fd);
        return -1;
    }
    printf("Thread Pool Server listening on port %d (thread num: %d)\n", PORT, THREAD_NUM);

    // 5. 创建线程池
    thread_pool_t* pool = thread_pool_create(THREAD_NUM);
    if (!pool) {
        perror("[Error] thread_pool_create failed");
        close(server_fd);
        return -1;
    }

    // 6. 循环接受连接（支持优雅退出）
    while (!g_exit_flag) {
        clientinfo_t* client = (clientinfo_t*)malloc(sizeof(clientinfo_t));
        if (!client) {
            perror("[Error] malloc clientinfo failed");
            continue;
        }

        socklen_t client_len = sizeof(client->client_addr);
        // 设置accept非阻塞（可选，避免主线程阻塞）
        // fcntl(server_fd, F_SETFL, O_NONBLOCK); 
        client->conn_fd = accept(server_fd, (struct sockaddr*)&client->client_addr, &client_len);
        if (client->conn_fd < 0) {
            // 处理可重试错误（信号中断）
            if (errno == EINTR && !g_exit_flag) {
                free(client);
                continue;
            }
            perror("[Error] accept failed");
            free(client);
            continue;
        }

        // 7. 提交任务到线程池（检查返回值，避免内存泄漏）
        if (thread_pool_add_task(pool, handle_client, (void*)client) != 0) {
            fprintf(stderr, "[Error] add task failed, close client fd: %d\n", client->conn_fd);
            close(client->conn_fd);
            free(client); // 任务提交失败，释放client
        }
    }

    // 8. 优雅退出：销毁线程池（等待所有任务完成）
    printf("[Server] 销毁线程池...\n");
    thread_pool_destroy(pool, 0); // force=0：等待所有任务完成
    close(server_fd); // 关闭监听FD
    printf("[Server] 正常退出\n");

    return 0;
}