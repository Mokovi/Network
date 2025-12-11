// 5_proactor.c
// gcc 5_proactor.c -luring -o server
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <liburing.h>
#include <stdbool.h>
#include <signal.h>

// ====================== 宏定义 ======================
#define LISTEN_PORT 8888
#define LISTEN_BACKLOG 1024
#define MAX_CONN 65536
#define BUF_SIZE 4096
#define IO_URING_QUEUE_DEPTH 1024  // io_uring队列深度（SQ/CQ大小）

bool global_running = true;

void handleSignal(int sig) {
    global_running = false;
    printf("\nReceived signal, exit...\n");
}


typedef enum {
    IO_TYPE_ACCEPT,
    IO_TYPE_READ,
    IO_TYPE_WRITE
} io_type_t;

struct proactor_ctx; // 前向声明

typedef struct io_request {
    io_type_t type;
    int fd;
    void *ctx; // 通常指向 conn_ctx_t 或 accept 所在 req
    struct proactor_ctx *proactor; // 指向 proactor，方便取 ring
    void (*cb)(struct io_request *req, int res);
    char buf[BUF_SIZE];
    size_t buf_len;

    // 当作为 accept 请求时，保存 client addr 在请求内，保证在 SQE 完成前有效
    struct sockaddr_in client_addr;
    socklen_t client_addr_len;
} io_request_t;

typedef struct conn_ctx {
    int fd;
    struct sockaddr_in addr;
    char read_buf[BUF_SIZE];
    char write_buf[BUF_SIZE];
    struct proactor_ctx *proactor; // backref，方便在回调中取 ring
} conn_ctx_t;

typedef struct proactor_ctx {
    struct io_uring ring;
    int listen_fd;
    conn_ctx_t *conn_pool;
} proactor_ctx_t;

// 前向声明
bool submitAccept(proactor_ctx_t* proactor);
void read_cb(io_request_t *req, int res);
void write_cb(io_request_t *req, int res);

void accept_cb(io_request_t *req, int res) {
    proactor_ctx_t *proactor = req->proactor;
    if (res < 0) {
        fprintf(stderr, "accept failed: %s\n", strerror(-res));
        // 继续提交 accept
        submitAccept(proactor);
        free(req);
        return;
    }

    int client_fd = res;

    if (client_fd < 0) {
        fprintf(stderr, "invalid client fd\n");
        submitAccept(proactor);
        free(req);
        return;
    }

    if (client_fd >= MAX_CONN) {
        fprintf(stderr, "client_fd %d >= MAX_CONN %d, closing\n", client_fd, MAX_CONN);
        close(client_fd);
        submitAccept(proactor);
        free(req);
        return;
    }

    // 初始化 conn
    conn_ctx_t *conn = &proactor->conn_pool[client_fd];
    conn->fd = client_fd;
    conn->addr = req->client_addr;
    conn->proactor = proactor;

    // 设置非阻塞
    int flags = fcntl(client_fd, F_GETFL, 0);
    if (flags != -1) fcntl(client_fd, F_SETFL, flags | O_NONBLOCK);

    // 创建 read 请求（把 conn 指针存到 req->ctx）
    io_request_t *read_req = (io_request_t *)malloc(sizeof(io_request_t));
    if (!read_req) {
        close(client_fd);
        free(req);
        return;
    }
    memset(read_req, 0, sizeof(*read_req));
    read_req->type = IO_TYPE_READ;
    read_req->fd = client_fd;
    read_req->ctx = conn;
    read_req->proactor = proactor;
    read_req->cb = read_cb;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&proactor->ring);
    io_uring_prep_read(sqe, client_fd, read_req->buf, BUF_SIZE, 0);
    io_uring_sqe_set_data(sqe, read_req);
    io_uring_submit(&proactor->ring);

    // 继续 accept
    submitAccept(proactor);

    free(req); // 释放 accept 请求对象（client_addr 已复制到 conn）
}

void read_cb(io_request_t *req, int res) {
    conn_ctx_t *conn = (conn_ctx_t *)req->ctx;
    proactor_ctx_t *proactor = req->proactor ? req->proactor : conn->proactor;

    if (res <= 0) {
        if (res < 0) fprintf(stderr, "read failed on fd %d: %s\n", conn->fd, strerror(-res));
        else fprintf(stdout, "client %d closed\n", conn->fd);
        close(conn->fd);
        free(req);
        return;
    }

    // 业务：回显
    fprintf(stdout, "recv from client[%d]: %.*s\n", conn->fd, res, req->buf);
    memcpy(conn->write_buf, req->buf, res);

    // 准备 write 请求
    io_request_t *write_req = (io_request_t *)malloc(sizeof(io_request_t));
    if (!write_req) {
        close(conn->fd);
        free(req);
        return;
    }
    memset(write_req, 0, sizeof(*write_req));
    write_req->type = IO_TYPE_WRITE;
    write_req->fd = conn->fd;
    write_req->ctx = conn;
    write_req->proactor = proactor;
    write_req->cb = write_cb;
    write_req->buf_len = res;
    memcpy(write_req->buf, conn->write_buf, res);

    struct io_uring_sqe *sqe = io_uring_get_sqe(&proactor->ring);
    io_uring_prep_write(sqe, conn->fd, write_req->buf, res, 0);
    io_uring_sqe_set_data(sqe, write_req);
    io_uring_submit(&proactor->ring);

    free(req);
}

void write_cb(io_request_t *req, int res) {
    conn_ctx_t *conn = (conn_ctx_t *)req->ctx;
    proactor_ctx_t *proactor = req->proactor ? req->proactor : conn->proactor;

    if (res < 0) {
        fprintf(stderr, "write failed on fd %d: %s\n", conn->fd, strerror(-res));
        close(conn->fd);
        free(req);
        return;
    }

    fprintf(stdout, "send to client[%d]: %.*s\n", conn->fd, res, req->buf);

    // 继续读
    io_request_t *read_req = (io_request_t *)malloc(sizeof(io_request_t));
    if (!read_req) {
        close(conn->fd);
        free(req);
        return;
    }
    memset(read_req, 0, sizeof(*read_req));
    read_req->type = IO_TYPE_READ;
    read_req->fd = conn->fd;
    read_req->ctx = conn;
    read_req->proactor = proactor;
    read_req->cb = read_cb;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&proactor->ring);
    io_uring_prep_read(sqe, conn->fd, read_req->buf, BUF_SIZE, 0);
    io_uring_sqe_set_data(sqe, read_req);
    io_uring_submit(&proactor->ring);

    free(req);
}

int create_listen_fd() {
    int listen_fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    if (listen_fd < 0) {
        perror("socket create failed");
        return -1;
    }

    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(LISTEN_PORT);

    if (bind(listen_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("bind failed");
        close(listen_fd);
        return -1;
    }

    if (listen(listen_fd, LISTEN_BACKLOG) < 0) {
        perror("listen failed");
        close(listen_fd);
        return -1;
    }

    return listen_fd;
}

bool submitAccept(proactor_ctx_t* proactor) {
    io_request_t *accept_req = (io_request_t *)malloc(sizeof(io_request_t));
    if (!accept_req) return false;
    memset(accept_req, 0, sizeof(io_request_t));
    accept_req->type = IO_TYPE_ACCEPT;
    accept_req->fd = proactor->listen_fd;
    accept_req->ctx = NULL;
    accept_req->proactor = proactor;
    accept_req->cb = accept_cb;

    struct io_uring_sqe *sqe = io_uring_get_sqe(&proactor->ring);
    if (!sqe) {
        free(accept_req);
        return false;
    }

    accept_req->client_addr_len = sizeof(accept_req->client_addr);
    io_uring_prep_accept(sqe, proactor->listen_fd,
                         (struct sockaddr *)&accept_req->client_addr,
                         &accept_req->client_addr_len, 0);
    io_uring_sqe_set_data(sqe, accept_req);
    io_uring_submit(&proactor->ring);

    return true;
}

proactor_ctx_t *proactor_init() {
    proactor_ctx_t *proactor = (proactor_ctx_t *)malloc(sizeof(proactor_ctx_t));
    if (!proactor) return NULL;
    memset(proactor, 0, sizeof(proactor_ctx_t));

    int ret = io_uring_queue_init(IO_URING_QUEUE_DEPTH, &proactor->ring, 0);
    if (ret < 0) {
        fprintf(stderr, "io_uring init failed: %s\n", strerror(-ret));
        free(proactor);
        return NULL;
    }

    proactor->listen_fd = create_listen_fd();
    if (proactor->listen_fd < 0) {
        io_uring_queue_exit(&proactor->ring);
        free(proactor);
        return NULL;
    }

    proactor->conn_pool = (conn_ctx_t *)calloc(MAX_CONN, sizeof(conn_ctx_t));
    if (!proactor->conn_pool) {
        close(proactor->listen_fd);
        io_uring_queue_exit(&proactor->ring);
        free(proactor);
        return NULL;
    }

    // 首个 accept
    submitAccept(proactor);

    return proactor;
}

void proactor_run(proactor_ctx_t *proactor) {
    struct io_uring_cqe *cqe;
    unsigned int head;
    int ret;

    while (global_running) {
        ret = io_uring_wait_cqe(&proactor->ring, &cqe);
        if (ret < 0) {
             if (ret == -EINTR && !global_running) {
                // 收到 Ctrl+C，退出事件循环
                break;
            }
            fprintf(stderr, "io_uring_wait_cqe failed: %s\n", strerror(-ret));
            continue;
        }

        io_uring_for_each_cqe(&proactor->ring, head, cqe) {
            io_request_t *req = (io_request_t *)io_uring_cqe_get_data(cqe);
            int res = cqe->res;

            if (req && req->cb) {
                req->cb(req, res);
            } else {
                // 如果没有回调，释放请求
                free(req);
            }

            io_uring_cqe_seen(&proactor->ring, cqe);
        }
    }
}

int main() {
    
    signal(SIGINT, handleSignal);
    
    proactor_ctx_t *proactor = proactor_init();
    if (!proactor) {
        fprintf(stderr, "proactor init failed\n");
        return -1;
    }

    fprintf(stdout, "Proactor server start on port %d\n", LISTEN_PORT);

    proactor_run(proactor);

    close(proactor->listen_fd);
    free(proactor->conn_pool);
    io_uring_queue_exit(&proactor->ring);
    free(proactor);
    return 0;
}
