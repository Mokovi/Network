# 高级I/O模型

## 1. 文档概述

本文档详细介绍了Linux系统中的高级I/O模型，包括阻塞I/O、非阻塞I/O、I/O多路复用、信号驱动I/O和异步I/O等。这些I/O模型是构建高性能网络服务器的核心技术，掌握这些知识对于开发高并发、低延迟的网络应用至关重要。

## 2. I/O模型基础

### 2.1 I/O操作的两个阶段

网络I/O操作通常分为两个阶段：

1. **等待数据就绪**：等待数据从网络到达内核缓冲区
2. **数据拷贝**：将数据从内核缓冲区拷贝到用户空间

**I/O模型分类：**
- 根据等待数据就绪的方式分类
- 根据数据拷贝的方式分类
- 根据应用程序的阻塞状态分类

### 2.2 同步I/O vs 异步I/O

**同步I/O：**
- 应用程序发起I/O操作后，必须等待操作完成
- 包括：阻塞I/O、非阻塞I/O、I/O多路复用、信号驱动I/O

**异步I/O：**
- 应用程序发起I/O操作后，立即返回，不等待操作完成
- 操作系统完成I/O操作后，通知应用程序
- 包括：POSIX AIO、io_uring

### 2.3 阻塞 vs 非阻塞

**阻塞I/O：**
- 应用程序调用I/O操作时，如果数据未就绪，进程被阻塞
- 直到数据就绪或操作完成才返回
- 简单易用，但效率较低

**非阻塞I/O：**
- 应用程序调用I/O操作时，如果数据未就绪，立即返回错误
- 应用程序需要轮询检查数据是否就绪
- 效率较高，但编程复杂

## 3. 阻塞I/O模型

### 3.1 阻塞I/O特点

**优点：**
- 编程简单，易于理解
- 不需要复杂的错误处理
- 适合简单的网络应用

**缺点：**
- 一个线程只能处理一个连接
- 资源利用率低
- 不适合高并发应用

### 3.2 阻塞I/O示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080

int main() {
    int server_sockfd, client_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;

    // 创建套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Blocking I/O Server listening on port %d\n", PORT);

    while (1) {
        // 阻塞等待连接
        client_len = sizeof(client_addr);
        client_sockfd = accept(server_sockfd, 
                              (struct sockaddr*)&client_addr, &client_len);
        if (client_sockfd == -1) {
            perror("accept");
            continue;
        }

        printf("Client connected\n");

        // 阻塞处理客户端请求
        while (1) {
            bytes_received = recv(client_sockfd, buffer, BUFFER_SIZE - 1, 0);
            if (bytes_received <= 0) {
                break;
            }

            buffer[bytes_received] = '\0';
            printf("Received: %s", buffer);

            // 回显数据
            send(client_sockfd, buffer, bytes_received, 0);
        }

        close(client_sockfd);
        printf("Client disconnected\n");
    }

    close(server_sockfd);
    return 0;
}
```

## 4. 非阻塞I/O模型

### 4.1 非阻塞I/O特点

**优点：**
- 不会阻塞进程
- 可以处理多个连接
- 资源利用率高

**缺点：**
- 需要轮询检查
- CPU资源浪费
- 编程复杂

### 4.2 设置非阻塞模式

```c
#include <fcntl.h>

int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        return -1;
    }
    
    if (fcntl(sockfd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        return -1;
    }
    
    return 0;
}
```

### 4.3 非阻塞I/O示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_CLIENTS 10

int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int server_sockfd, client_sockfds[MAX_CLIENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int i, max_fd, new_client;

    // 初始化客户端套接字数组
    for (i = 0; i < MAX_CLIENTS; i++) {
        client_sockfds[i] = -1;
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置非阻塞模式
    if (set_nonblocking(server_sockfd) == -1) {
        perror("set_nonblocking");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Non-blocking I/O Server listening on port %d\n", PORT);

    while (1) {
        // 检查新连接
        client_len = sizeof(client_addr);
        new_client = accept(server_sockfd, 
                           (struct sockaddr*)&client_addr, &client_len);
        
        if (new_client != -1) {
            // 找到空闲位置
            for (i = 0; i < MAX_CLIENTS; i++) {
                if (client_sockfds[i] == -1) {
                    client_sockfds[i] = new_client;
                    set_nonblocking(new_client);
                    printf("Client %d connected\n", i);
                    break;
                }
            }
            
            if (i == MAX_CLIENTS) {
                printf("Too many clients, closing connection\n");
                close(new_client);
            }
        }

        // 处理所有客户端
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockfds[i] == -1) continue;

            bytes_received = recv(client_sockfds[i], buffer, BUFFER_SIZE - 1, 0);
            
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                printf("Client %d: %s", i, buffer);
                send(client_sockfds[i], buffer, bytes_received, 0);
            } else if (bytes_received == 0) {
                printf("Client %d disconnected\n", i);
                close(client_sockfds[i]);
                client_sockfds[i] = -1;
            } else if (errno != EAGAIN && errno != EWOULDBLOCK) {
                perror("recv");
                close(client_sockfds[i]);
                client_sockfds[i] = -1;
            }
        }

        // 短暂休眠，避免CPU占用过高
        usleep(1000);
    }

    close(server_sockfd);
    return 0;
}
```

## 5. I/O多路复用

### 5.1 I/O多路复用概念

I/O多路复用允许一个进程同时监听多个文件描述符，当其中任何一个文件描述符就绪时，进程就会被唤醒。

**优点：**
- 一个进程可以处理多个连接
- 不需要轮询，效率高
- 编程相对简单

**缺点：**
- 仍然需要系统调用
- 某些实现有文件描述符数量限制

### 5.2 select()函数

#### 5.2.1 select()函数原型

```c
#include <sys/select.h>

int select(int nfds, fd_set *readfds, fd_set *writefds, 
           fd_set *exceptfds, struct timeval *timeout);
```

**参数说明：**
- `nfds`：最大文件描述符值加1
- `readfds`：可读文件描述符集合
- `writefds`：可写文件描述符集合
- `exceptfds`：异常文件描述符集合
- `timeout`：超时时间

**返回值：**
- 成功：返回就绪的文件描述符数量
- 超时：返回0
- 错误：返回-1

#### 5.2.2 select()相关宏

```c
#include <sys/select.h>

// 清空文件描述符集合
void FD_ZERO(fd_set *set);

// 添加文件描述符到集合
void FD_SET(int fd, fd_set *set);

// 从集合中移除文件描述符
void FD_CLR(int fd, fd_set *set);

// 检查文件描述符是否在集合中
int FD_ISSET(int fd, fd_set *set);
```

#### 5.2.3 select()示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_CLIENTS 10

int main() {
    int server_sockfd, client_sockfds[MAX_CLIENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int i, max_fd, new_client, activity;
    fd_set readfds;
    struct timeval timeout;

    // 初始化客户端套接字数组
    for (i = 0; i < MAX_CLIENTS; i++) {
        client_sockfds[i] = -1;
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Select I/O Server listening on port %d\n", PORT);

    while (1) {
        // 清空文件描述符集合
        FD_ZERO(&readfds);
        
        // 添加服务器套接字
        FD_SET(server_sockfd, &readfds);
        max_fd = server_sockfd;

        // 添加客户端套接字
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockfds[i] > 0) {
                FD_SET(client_sockfds[i], &readfds);
                if (client_sockfds[i] > max_fd) {
                    max_fd = client_sockfds[i];
                }
            }
        }

        // 设置超时时间
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;

        // 等待文件描述符就绪
        activity = select(max_fd + 1, &readfds, NULL, NULL, &timeout);
        
        if (activity < 0) {
            perror("select");
            break;
        } else if (activity == 0) {
            printf("Timeout, no activity\n");
            continue;
        }

        // 检查新连接
        if (FD_ISSET(server_sockfd, &readfds)) {
            client_len = sizeof(client_addr);
            new_client = accept(server_sockfd, 
                               (struct sockaddr*)&client_addr, &client_len);
            
            if (new_client != -1) {
                // 找到空闲位置
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockfds[i] == -1) {
                        client_sockfds[i] = new_client;
                        printf("Client %d connected\n", i);
                        break;
                    }
                }
                
                if (i == MAX_CLIENTS) {
                    printf("Too many clients, closing connection\n");
                    close(new_client);
                }
            }
        }

        // 处理客户端数据
        for (i = 0; i < MAX_CLIENTS; i++) {
            if (client_sockfds[i] == -1) continue;

            if (FD_ISSET(client_sockfds[i], &readfds)) {
                bytes_received = recv(client_sockfds[i], buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Client %d: %s", i, buffer);
                    send(client_sockfds[i], buffer, bytes_received, 0);
                } else if (bytes_received == 0) {
                    printf("Client %d disconnected\n", i);
                    close(client_sockfds[i]);
                    client_sockfds[i] = -1;
                } else {
                    perror("recv");
                    close(client_sockfds[i]);
                    client_sockfds[i] = -1;
                }
            }
        }
    }

    close(server_sockfd);
    return 0;
}
```

### 5.3 poll()函数

#### 5.3.1 poll()函数原型

```c
#include <poll.h>

int poll(struct pollfd *fds, nfds_t nfds, int timeout);
```

**参数说明：**
- `fds`：pollfd结构数组
- `nfds`：数组元素个数
- `timeout`：超时时间（毫秒）

**pollfd结构：**
```c
struct pollfd {
    int   fd;      // 文件描述符
    short events;  // 监听的事件
    short revents; // 返回的事件
};
```

**事件类型：**
- `POLLIN`：可读
- `POLLOUT`：可写
- `POLLERR`：错误
- `POLLHUP`：挂起
- `POLLNVAL`：无效文件描述符

#### 5.3.2 poll()示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_CLIENTS 10

int main() {
    int server_sockfd, client_sockfds[MAX_CLIENTS];
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int i, nfds, new_client, activity;
    struct pollfd fds[MAX_CLIENTS + 1];

    // 初始化客户端套接字数组
    for (i = 0; i < MAX_CLIENTS; i++) {
        client_sockfds[i] = -1;
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Poll I/O Server listening on port %d\n", PORT);

    // 初始化pollfd数组
    fds[0].fd = server_sockfd;
    fds[0].events = POLLIN;
    nfds = 1;

    while (1) {
        // 等待文件描述符就绪
        activity = poll(fds, nfds, 1000); // 1秒超时
        
        if (activity < 0) {
            perror("poll");
            break;
        } else if (activity == 0) {
            printf("Timeout, no activity\n");
            continue;
        }

        // 检查新连接
        if (fds[0].revents & POLLIN) {
            client_len = sizeof(client_addr);
            new_client = accept(server_sockfd, 
                               (struct sockaddr*)&client_addr, &client_len);
            
            if (new_client != -1) {
                // 找到空闲位置
                for (i = 0; i < MAX_CLIENTS; i++) {
                    if (client_sockfds[i] == -1) {
                        client_sockfds[i] = new_client;
                        fds[nfds].fd = new_client;
                        fds[nfds].events = POLLIN;
                        nfds++;
                        printf("Client %d connected\n", i);
                        break;
                    }
                }
                
                if (i == MAX_CLIENTS) {
                    printf("Too many clients, closing connection\n");
                    close(new_client);
                }
            }
        }

        // 处理客户端数据
        for (i = 1; i < nfds; i++) {
            if (fds[i].revents & POLLIN) {
                bytes_received = recv(fds[i].fd, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Client: %s", buffer);
                    send(fds[i].fd, buffer, bytes_received, 0);
                } else if (bytes_received == 0) {
                    printf("Client disconnected\n");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                } else {
                    perror("recv");
                    close(fds[i].fd);
                    fds[i].fd = -1;
                }
            }
        }
    }

    close(server_sockfd);
    return 0;
}
```

### 5.4 select() vs poll() 对比

| 特性 | select() | poll() |
|------|----------|--------|
| 文件描述符限制 | FD_SETSIZE (通常1024) | 无限制 |
| 效率 | O(n) | O(n) |
| 可移植性 | 广泛支持 | 广泛支持 |
| 编程复杂度 | 中等 | 简单 |
| 内存使用 | 固定大小 | 动态分配 |

## 6. epoll - 高性能I/O多路复用

### 6.1 epoll特点

epoll是Linux特有的高性能I/O多路复用机制，具有以下特点：

**优点：**
- 无文件描述符数量限制
- 时间复杂度O(1)
- 支持边缘触发和水平触发
- 内存使用效率高

**缺点：**
- Linux特有，可移植性差
- 编程相对复杂

### 6.2 epoll API

#### 6.2.1 epoll_create()

```c
#include <sys/epoll.h>

int epoll_create(int size);
int epoll_create1(int flags);
```

**参数说明：**
- `size`：提示内核监听的文件描述符数量（已废弃）
- `flags`：创建标志（EPOLL_CLOEXEC）

#### 6.2.2 epoll_ctl()

```c
int epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
```

**操作类型：**
- `EPOLL_CTL_ADD`：添加文件描述符
- `EPOLL_CTL_MOD`：修改文件描述符
- `EPOLL_CTL_DEL`：删除文件描述符

**epoll_event结构：**
```c
struct epoll_event {
    uint32_t     events;    // 监听的事件
    epoll_data_t data;      // 用户数据
};

typedef union epoll_data {
    void    *ptr;
    int      fd;
    uint32_t u32;
    uint64_t u64;
} epoll_data_t;
```

**事件类型：**
- `EPOLLIN`：可读
- `EPOLLOUT`：可写
- `EPOLLERR`：错误
- `EPOLLHUP`：挂起
- `EPOLLET`：边缘触发
- `EPOLLONESHOT`：一次性事件

#### 6.2.3 epoll_wait()

```c
int epoll_wait(int epfd, struct epoll_event *events, 
               int maxevents, int timeout);
```

**参数说明：**
- `epfd`：epoll文件描述符
- `events`：就绪事件数组
- `maxevents`：数组大小
- `timeout`：超时时间（毫秒）

### 6.3 epoll示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_EVENTS 10

int main() {
    int server_sockfd, epoll_fd, client_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int i, nfds, new_client;
    struct epoll_event event, events[MAX_EVENTS];

    // 创建epoll实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 添加服务器套接字到epoll
    event.events = EPOLLIN;
    event.data.fd = server_sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sockfd, &event) == -1) {
        perror("epoll_ctl");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    printf("Epoll I/O Server listening on port %d\n", PORT);

    while (1) {
        // 等待事件
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        } else if (nfds == 0) {
            printf("Timeout, no activity\n");
            continue;
        }

        // 处理就绪的事件
        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_sockfd) {
                // 新连接
                client_len = sizeof(client_addr);
                new_client = accept(server_sockfd, 
                                   (struct sockaddr*)&client_addr, &client_len);
                
                if (new_client != -1) {
                    // 添加客户端到epoll
                    event.events = EPOLLIN;
                    event.data.fd = new_client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client, &event) == -1) {
                        perror("epoll_ctl");
                        close(new_client);
                    } else {
                        printf("Client connected\n");
                    }
                }
            } else {
                // 客户端数据
                client_sockfd = events[i].data.fd;
                bytes_received = recv(client_sockfd, buffer, BUFFER_SIZE - 1, 0);
                
                if (bytes_received > 0) {
                    buffer[bytes_received] = '\0';
                    printf("Received: %s", buffer);
                    send(client_sockfd, buffer, bytes_received, 0);
                } else if (bytes_received == 0) {
                    printf("Client disconnected\n");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sockfd, NULL);
                    close(client_sockfd);
                } else {
                    perror("recv");
                    epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sockfd, NULL);
                    close(client_sockfd);
                }
            }
        }
    }

    close(server_sockfd);
    close(epoll_fd);
    return 0;
}
```

### 6.4 边缘触发 vs 水平触发

#### 6.4.1 水平触发（Level Triggered, LT）

**特点：**
- 默认模式
- 只要文件描述符处于就绪状态，就会持续通知
- 编程简单，不容易出错

**示例：**
```c
// 水平触发模式
event.events = EPOLLIN;  // 不设置EPOLLET
```

#### 6.4.2 边缘触发（Edge Triggered, ET）

**特点：**
- 需要设置EPOLLET标志
- 只在状态变化时通知一次
- 效率更高，但编程复杂
- 必须将数据读干净

**ET模式示例：**
```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_EVENTS 10

int set_nonblocking(int sockfd) {
    int flags = fcntl(sockfd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(sockfd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int server_sockfd, epoll_fd, client_sockfd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int i, nfds, new_client;
    struct epoll_event event, events[MAX_EVENTS];

    // 创建epoll实例
    epoll_fd = epoll_create1(0);
    if (epoll_fd == -1) {
        perror("epoll_create1");
        exit(EXIT_FAILURE);
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 设置非阻塞模式
    if (set_nonblocking(server_sockfd) == -1) {
        perror("set_nonblocking");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    // 添加服务器套接字到epoll（边缘触发）
    event.events = EPOLLIN | EPOLLET;
    event.data.fd = server_sockfd;
    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, server_sockfd, &event) == -1) {
        perror("epoll_ctl");
        close(server_sockfd);
        close(epoll_fd);
        exit(EXIT_FAILURE);
    }

    printf("Epoll ET Server listening on port %d\n", PORT);

    while (1) {
        nfds = epoll_wait(epoll_fd, events, MAX_EVENTS, 1000);
        if (nfds == -1) {
            perror("epoll_wait");
            break;
        } else if (nfds == 0) {
            printf("Timeout, no activity\n");
            continue;
        }

        for (i = 0; i < nfds; i++) {
            if (events[i].data.fd == server_sockfd) {
                // 处理新连接（ET模式需要循环accept）
                while (1) {
                    client_len = sizeof(client_addr);
                    new_client = accept(server_sockfd, 
                                       (struct sockaddr*)&client_addr, &client_len);
                    
                    if (new_client == -1) {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 没有更多连接
                        } else {
                            perror("accept");
                            break;
                        }
                    }

                    // 设置非阻塞模式
                    if (set_nonblocking(new_client) == -1) {
                        close(new_client);
                        continue;
                    }

                    // 添加客户端到epoll（边缘触发）
                    event.events = EPOLLIN | EPOLLET;
                    event.data.fd = new_client;
                    if (epoll_ctl(epoll_fd, EPOLL_CTL_ADD, new_client, &event) == -1) {
                        perror("epoll_ctl");
                        close(new_client);
                    } else {
                        printf("Client connected\n");
                    }
                }
            } else {
                // 处理客户端数据（ET模式需要循环读取）
                client_sockfd = events[i].data.fd;
                while (1) {
                    bytes_received = recv(client_sockfd, buffer, BUFFER_SIZE - 1, 0);
                    
                    if (bytes_received > 0) {
                        buffer[bytes_received] = '\0';
                        printf("Received: %s", buffer);
                        send(client_sockfd, buffer, bytes_received, 0);
                    } else if (bytes_received == 0) {
                        printf("Client disconnected\n");
                        epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sockfd, NULL);
                        close(client_sockfd);
                        break;
                    } else {
                        if (errno == EAGAIN || errno == EWOULDBLOCK) {
                            break; // 没有更多数据
                        } else {
                            perror("recv");
                            epoll_ctl(epoll_fd, EPOLL_CTL_DEL, client_sockfd, NULL);
                            close(client_sockfd);
                            break;
                        }
                    }
                }
            }
        }
    }

    close(server_sockfd);
    close(epoll_fd);
    return 0;
}
```

## 7. 信号驱动I/O

### 7.1 信号驱动I/O概念

信号驱动I/O使用SIGIO信号来通知应用程序I/O操作就绪，避免轮询。

**优点：**
- 不需要轮询
- 可以处理多个文件描述符
- 效率较高

**缺点：**
- 信号处理复杂
- 可移植性差
- 调试困难

### 7.2 信号驱动I/O实现

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define BUFFER_SIZE 1024
#define PORT 8080

int server_sockfd, client_sockfd;
volatile sig_atomic_t got_signal = 0;

void signal_handler(int sig) {
    got_signal = 1;
}

int main() {
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_received;
    int flags;

    // 设置信号处理
    signal(SIGIO, signal_handler);

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    // 设置信号驱动I/O
    if (fcntl(server_sockfd, F_SETOWN, getpid()) == -1) {
        perror("fcntl F_SETOWN");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    flags = fcntl(server_sockfd, F_GETFL, 0);
    if (fcntl(server_sockfd, F_SETFL, flags | O_ASYNC) == -1) {
        perror("fcntl F_SETFL");
        close(server_sockfd);
        exit(EXIT_FAILURE);
    }

    printf("Signal-driven I/O Server listening on port %d\n", PORT);

    while (1) {
        if (got_signal) {
            got_signal = 0;
            
            // 接受连接
            client_len = sizeof(client_addr);
            client_sockfd = accept(server_sockfd, 
                                  (struct sockaddr*)&client_addr, &client_len);
            
            if (client_sockfd != -1) {
                printf("Client connected\n");
                
                // 设置客户端信号驱动I/O
                if (fcntl(client_sockfd, F_SETOWN, getpid()) == -1) {
                    perror("fcntl F_SETOWN");
                    close(client_sockfd);
                    continue;
                }
                
                flags = fcntl(client_sockfd, F_GETFL, 0);
                if (fcntl(client_sockfd, F_SETFL, flags | O_ASYNC) == -1) {
                    perror("fcntl F_SETFL");
                    close(client_sockfd);
                    continue;
                }
                
                // 处理客户端数据
                while (1) {
                    if (got_signal) {
                        got_signal = 0;
                        
                        bytes_received = recv(client_sockfd, buffer, BUFFER_SIZE - 1, 0);
                        
                        if (bytes_received > 0) {
                            buffer[bytes_received] = '\0';
                            printf("Received: %s", buffer);
                            send(client_sockfd, buffer, bytes_received, 0);
                        } else if (bytes_received == 0) {
                            printf("Client disconnected\n");
                            close(client_sockfd);
                            break;
                        } else {
                            perror("recv");
                            close(client_sockfd);
                            break;
                        }
                    }
                    
                    usleep(1000); // 短暂休眠
                }
            }
        }
        
        usleep(1000); // 短暂休眠
    }

    close(server_sockfd);
    return 0;
}
```

## 8. 异步I/O

### 8.1 POSIX AIO

POSIX异步I/O提供真正的异步I/O操作，应用程序发起I/O操作后立即返回。

#### 8.1.1 AIO API

```c
#include <aio.h>

// 异步读操作
int aio_read(struct aiocb *aiocbp);

// 异步写操作
int aio_write(struct aiocb *aiocbp);

// 检查操作状态
int aio_error(const struct aiocb *aiocbp);

// 获取返回值
ssize_t aio_return(struct aiocb *aiocbp);

// 等待操作完成
int aio_suspend(const struct aiocb *const list[], int nent,
                const struct timespec *timeout);
```

#### 8.1.2 aiocb结构

```c
struct aiocb {
    int             aio_fildes;     // 文件描述符
    off_t           aio_offset;     // 文件偏移
    volatile void  *aio_buf;        // 缓冲区
    size_t          aio_nbytes;     // 字节数
    int             aio_reqprio;    // 请求优先级
    struct sigevent aio_sigevent;   // 信号事件
    int             aio_lio_opcode; // 操作类型
};
```

### 8.2 io_uring - 现代异步I/O

io_uring是Linux 5.1引入的现代异步I/O接口，提供更高的性能。

#### 8.2.1 io_uring特点

**优点：**
- 更高的性能
- 更简单的API
- 支持批量操作
- 减少系统调用次数

**核心概念：**
- 提交队列（SQ）：应用程序提交I/O请求
- 完成队列（CQ）：内核返回I/O结果
- 共享内存：用户空间和内核空间共享

#### 8.2.2 io_uring API

```c
#include <liburing.h>

// 初始化io_uring
int io_uring_queue_init(unsigned entries, struct io_uring *ring, unsigned flags);

// 获取提交队列条目
struct io_uring_sqe *io_uring_get_sqe(struct io_uring *ring);

// 准备读操作
void io_uring_prep_read(struct io_uring_sqe *sqe, int fd, void *buf, 
                        unsigned nbytes, off_t offset);

// 准备写操作
void io_uring_prep_write(struct io_uring_sqe *sqe, int fd, const void *buf, 
                         unsigned nbytes, off_t offset);

// 提交请求
int io_uring_submit(struct io_uring *ring);

// 等待完成
int io_uring_wait_cqe(struct io_uring *ring, struct io_uring_cqe **cqe_ptr);

// 获取完成队列条目
struct io_uring_cqe *io_uring_peek_cqe(struct io_uring *ring, unsigned *head);

// 标记完成
void io_uring_cqe_seen(struct io_uring *ring, struct io_uring_cqe *cqe);

// 清理io_uring
void io_uring_queue_exit(struct io_uring *ring);
```

#### 8.2.3 io_uring示例

```c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <liburing.h>

#define BUFFER_SIZE 1024
#define PORT 8080
#define MAX_CONNECTIONS 10

struct connection {
    int fd;
    char buffer[BUFFER_SIZE];
    int bytes_read;
    int bytes_written;
};

int main() {
    struct io_uring ring;
    struct io_uring_sqe *sqe;
    struct io_uring_cqe *cqe;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len;
    struct connection *conn;
    int server_sockfd, client_sockfd;
    int i, ret;

    // 初始化io_uring
    if (io_uring_queue_init(32, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        exit(EXIT_FAILURE);
    }

    // 创建服务器套接字
    server_sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sockfd == -1) {
        perror("socket");
        io_uring_queue_exit(&ring);
        exit(EXIT_FAILURE);
    }

    // 设置服务器地址
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(PORT);

    // 绑定地址
    if (bind(server_sockfd, (struct sockaddr*)&server_addr, 
             sizeof(server_addr)) == -1) {
        perror("bind");
        close(server_sockfd);
        io_uring_queue_exit(&ring);
        exit(EXIT_FAILURE);
    }

    // 开始监听
    if (listen(server_sockfd, 5) == -1) {
        perror("listen");
        close(server_sockfd);
        io_uring_queue_exit(&ring);
        exit(EXIT_FAILURE);
    }

    printf("io_uring Server listening on port %d\n", PORT);

    // 提交accept请求
    sqe = io_uring_get_sqe(&ring);
    io_uring_prep_accept(sqe, server_sockfd, (struct sockaddr*)&client_addr, 
                        &client_len, 0);
    io_uring_sqe_set_data(sqe, NULL);
    io_uring_submit(&ring);

    while (1) {
        // 等待完成事件
        ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            perror("io_uring_wait_cqe");
            break;
        }

        if (cqe->res < 0) {
            fprintf(stderr, "Async operation failed: %s\n", strerror(-cqe->res));
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        if (sqe->opcode == IORING_OP_ACCEPT) {
            // 新连接
            client_sockfd = cqe->res;
            printf("Client connected\n");

            // 分配连接结构
            conn = malloc(sizeof(struct connection));
            conn->fd = client_sockfd;
            conn->bytes_read = 0;
            conn->bytes_written = 0;

            // 提交读请求
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe, client_sockfd, conn->buffer, BUFFER_SIZE, 0);
            io_uring_sqe_set_data(sqe, conn);
            io_uring_submit(&ring);

            // 提交新的accept请求
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_accept(sqe, server_sockfd, (struct sockaddr*)&client_addr, 
                                &client_len, 0);
            io_uring_sqe_set_data(sqe, NULL);
            io_uring_submit(&ring);

        } else if (sqe->opcode == IORING_OP_READ) {
            // 读取完成
            conn = (struct connection*)io_uring_cqe_get_data(cqe);
            conn->bytes_read = cqe->res;

            if (conn->bytes_read > 0) {
                printf("Received: %.*s", conn->bytes_read, conn->buffer);
                
                // 提交写请求（回显）
                sqe = io_uring_get_sqe(&ring);
                io_uring_prep_write(sqe, conn->fd, conn->buffer, conn->bytes_read, 0);
                io_uring_sqe_set_data(sqe, conn);
                io_uring_submit(&ring);
            } else {
                printf("Client disconnected\n");
                close(conn->fd);
                free(conn);
            }

        } else if (sqe->opcode == IORING_OP_WRITE) {
            // 写入完成
            conn = (struct connection*)io_uring_cqe_get_data(cqe);
            
            // 提交新的读请求
            sqe = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe, conn->fd, conn->buffer, BUFFER_SIZE, 0);
            io_uring_sqe_set_data(sqe, conn);
            io_uring_submit(&ring);
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    close(server_sockfd);
    io_uring_queue_exit(&ring);
    return 0;
}
```

## 9. I/O模型对比

### 9.1 性能对比

| I/O模型 | 并发连接数 | CPU使用率 | 内存使用 | 编程复杂度 | 可移植性 |
|---------|------------|-----------|----------|------------|----------|
| 阻塞I/O | 1 | 低 | 低 | 简单 | 好 |
| 非阻塞I/O | 中等 | 高 | 低 | 中等 | 好 |
| select() | 1024 | 中等 | 低 | 中等 | 好 |
| poll() | 无限制 | 中等 | 中等 | 简单 | 好 |
| epoll | 无限制 | 低 | 低 | 中等 | Linux |
| 信号驱动I/O | 中等 | 低 | 低 | 复杂 | 差 |
| 异步I/O | 高 | 低 | 中等 | 复杂 | 中等 |

### 9.2 选择建议

**选择阻塞I/O的情况：**
- 简单的网络应用
- 对性能要求不高
- 快速原型开发

**选择I/O多路复用的情况：**
- 需要处理多个连接
- 对性能有一定要求
- 需要跨平台支持

**选择epoll的情况：**
- Linux平台
- 高并发应用
- 对性能要求很高

**选择异步I/O的情况：**
- 极高并发应用
- 对延迟要求极高
- 可以接受复杂的编程

## 10. 总结

高级I/O模型是构建高性能网络服务器的核心技术，本章详细介绍了：

1. **I/O模型基础**：同步/异步、阻塞/非阻塞概念
2. **阻塞I/O**：简单但效率低
3. **非阻塞I/O**：需要轮询，编程复杂
4. **I/O多路复用**：select、poll、epoll的详细使用
5. **信号驱动I/O**：使用信号通知，编程复杂
6. **异步I/O**：POSIX AIO和现代io_uring
7. **模型对比**：性能特点和选择建议

在实际应用中，要根据具体需求选择合适的I/O模型：
- 简单应用选择阻塞I/O
- 一般应用选择I/O多路复用
- 高性能应用选择epoll或io_uring

掌握这些I/O模型后，您就能够构建各种高性能的网络服务器，为学习服务器架构设计奠定基础。

---

**文档版本**：v1.0  
**创建时间**：2025年09月30日 17:02:28 CST  
**最后更新**：2025年09月30日 17:02:28 CST  
**维护者**：Gamma
