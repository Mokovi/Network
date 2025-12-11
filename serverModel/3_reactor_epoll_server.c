// reactor_epoll_server.c
// 单线程 Reactor 示例（基于 epoll），用 C 实现
// 编译: gcc -std=c11 -O2 3_reactor_epoll_server.c -o server
// 运行: ./server [port]
// 说明: 简单 echo 服务，演示 Reactor 模式与 epoll 使用

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

#define DEAFULT_PORT 13145
#define MAX_EVENTS 1024
#define BUFFER_SIZE 4096

volatile int global_running = 1;

void signal_handler(int sig) {
    global_running = 0;
    printf("\nSignal %d received, shutting down...\n", sig);
}
struct connection_s;   // 前置声明
typedef struct connection_s{
    int fd;
    struct sockaddr_in addr;
    void (*read_handler)(int, struct connection_s*); // 读事件处理函数指针
    void (*write_handler)(int, struct connection_s*); // 写事件处理函数指针
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

connection_t* connection_create(int fd, struct sockaddr_in addr,void (*read_handler)(int, connection_t*), void (*write_handler)(int, connection_t*), connection_type_t type) {
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
    }
    return conn;
}

int connection_destroy(connection_t* conn) {
    if (!conn) return -1;
    close(conn->fd);
    if (conn->read_buffer) free(conn->read_buffer);
    if (conn->wbuffer) free(conn->wbuffer);
    free(conn);
    return 0;
}

int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}
int epoll_add_fd(int epoll_fd, int fd, connection_t* conn, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = conn;
    return epoll_ctl(epoll_fd, EPOLL_CTL_ADD, fd, &ev);
}
int epoll_del_fd(int epoll_fd, int fd) {
    return epoll_ctl(epoll_fd, EPOLL_CTL_DEL, fd, NULL);
}
int epoll_mod_fd(int epoll_fd, int fd, connection_t* conn, uint32_t events) {
    struct epoll_event ev;
    ev.events = events;
    ev.data.ptr = conn;
    return epoll_ctl(epoll_fd, EPOLL_CTL_MOD, fd, &ev);
}

// 前向声明
void accept_handler(int epoll_fd, connection_t* accept_conn);
void read_handler(int epoll_fd, connection_t* conn);
void write_handler(int epoll_fd, connection_t* conn);

void build_http_response(connection_t* conn, const char* body)
{
    static const char* header_fmt =
        "HTTP/1.1 200 OK\r\n"
        "Content-Type: text/plain\r\n"
        "Content-Length: %zu\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";

    size_t body_len = strlen(body);

    int header_len = snprintf(conn->wbuffer, conn->wbuffer_size,
                              header_fmt, body_len);

    memcpy(conn->wbuffer + header_len, body, body_len);

    conn->wbuffer_sent = header_len + body_len;
}
void accept_handler(int epoll_fd, connection_t* accept_conn) {
    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    int conn_fd;
    // 接受所有到来的连接 ET 模式
    while (1) {
        client_len = sizeof(client_addr);
        if (conn_fd = accept(accept_conn->fd, (struct sockaddr*)&client_addr, &client_len) == -1) {
            break; // 没有更多连接
        }
        if (set_nonblocking(conn_fd) < 0) {
            perror("set_nonblocking");
            close(conn_fd);
            continue;
        }

        connection_t* conn = connection_create(conn_fd, client_addr, read_handler, write_handler, CONN_CLIENT);

        if (epoll_add_fd(epoll_fd, conn_fd, conn, EPOLLIN | EPOLLET) < 0) {
            perror("epoll_add_fd");
            connection_destroy(conn);
            continue;
        }

        printf("Accepted connection from %s:%d, fd=%d\n",
               inet_ntoa(client_addr.sin_addr),
               ntohs(client_addr.sin_port),
               conn_fd);
    }
    if (errno != EAGAIN && errno != EWOULDBLOCK) {
        perror("accept");
    }
}
void read_handler(int epoll_fd, connection_t* conn) {
    ssize_t n;
    while (1) {
        // memset(conn->read_buffer, 0, conn->read_buffer_size);
        n = read(conn->fd, conn->read_buffer, conn->read_buffer_size-1);
        if (n > 0) {
            conn->read_buffer[n] = '\0';
            printf("[%s:%d]: %s\n", inet_ntoa(conn->addr.sin_addr), ntohs(conn->addr.sin_port), conn->read_buffer);
            // 回显数据
            #if 1
                build_http_response(conn, conn->read_buffer);
            #else
                memcpy(conn->wbuffer, conn->read_buffer, n);
                conn->wbuffer_sent = n;
            #endif
            
            epoll_mod_fd(epoll_fd, conn->fd, conn, EPOLLOUT | EPOLLET);
        } else if (n == 0) {
            // 客户端关闭连接
            printf("Client disconnected, fd=%d\n", conn->fd);
            epoll_del_fd(epoll_fd, conn->fd);
            connection_destroy(conn);
            return;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 读完所有数据
                break;
            } else {
                perror("read");
                epoll_del_fd(epoll_fd, conn->fd);
                connection_destroy(conn);
                return;
            }
        }
    }
}
void write_handler(int epoll_fd, connection_t* conn) {
    ssize_t n;
    while (conn->wbuffer_sent > 0) {
        n = write(conn->fd, conn->wbuffer, conn->wbuffer_sent);
        if (n > 0) {
            conn->wbuffer_sent -= n;
            if (conn->wbuffer_sent > 0) {
                memmove(conn->wbuffer, conn->wbuffer + n, conn->wbuffer_sent);
            }
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // 写缓冲区满，等待下一次写事件
                break;
            } else {
                perror("write");
                epoll_del_fd(epoll_fd, conn->fd);
                connection_destroy(conn);
                return;
            }
        }
    }
    if (conn->wbuffer_sent == 0) {
        // 切换回读事件
        epoll_mod_fd(epoll_fd, conn->fd, conn, EPOLLIN | EPOLLET);
    }
}


void reactor_loop(int epoll_fd, struct epoll_event* events, int max_events) {
    while (global_running) {
        int n = epoll_wait(epoll_fd, events, max_events, 1000); // 1 秒超时
        if (n < 0) {
            if (errno == EINTR) continue; // 被信号中断，继续等待
            perror("epoll_wait");
            break;
        }

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

    // 注册信号处理函数
    signal(SIGINT, signal_handler);

    int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (listen_fd < 0) {
        perror("socket");
        exit(EXIT_FAILURE);
    }
    int opt = 1;
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

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

    // 创建 epoll 实例
    int epoll_fd = epoll_create1(0);
    if (epoll_fd < 0) {
        perror("epoll_create1");
        close(listen_fd);
        exit(EXIT_FAILURE);
    }

    // 将监听套接字添加到 epoll 实例
    connection_t* listen_conn = (connection_t*)malloc(sizeof(connection_t));
    memset(listen_conn, 0, sizeof(connection_t));
    listen_conn->fd = listen_fd;
    listen_conn->read_handler = accept_handler;
    listen_conn->write_handler = NULL;

    if (epoll_add_fd(epoll_fd, listen_fd, listen_conn, EPOLLIN | EPOLLET) < 0) {
        perror("epoll_add_fd");
        close(listen_fd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    struct epoll_event events[MAX_EVENTS];
    memset(events, 0, sizeof(events));
    printf("Server listening on port %d\n", port);

    reactor_loop(epoll_fd, events, MAX_EVENTS);

    close(listen_fd);
    close(epoll_fd);
    free(listen_conn);

    printf("End.\n");
    return 0;
}