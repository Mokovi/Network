#include <iostream>
#include <unistd.h>       // close()
#include <sys/socket.h>
#include <sys/epoll.h>
#include <arpa/inet.h>    // inet_pton(), ntohs()
#include <netinet/in.h>   // sockaddr_in
#include <cstring>
#include <cstdlib>        // exit()
#include <cerrno>         // errno
#include <fcntl.h>

const uint16_t Port = 13145;
const uint16_t MaxClientNum = 10;
const uint16_t BufferSize = 1024;

struct ClientInfo {
    int fd = -1;
    char ipStr[INET_ADDRSTRLEN] = "";
    uint16_t port = 0;
};

// 设置fd为非阻塞（epoll推荐配置，避免recv/send阻塞）
bool setNonBlock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL failed");
        return false;
    }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL failed");
        return false;
    }
    return true;
}

int main() {
    // 创建服务器套接字
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("socket failed");
        exit(EXIT_FAILURE);
    }

    // 设置端口复用
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    // 绑定地址
    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);
    if (bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    // 监听
    if (listen(serverFd, 5) == -1) {
        perror("listen failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    // 设置服务器fd为非阻塞
    if (!setNonBlock(serverFd)) {
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    // 创建epoll实例
    int epollFd = epoll_create1(0);
    if (epollFd == -1) {
        perror("epoll_create1 failed");
        close(serverFd);
        exit(EXIT_FAILURE);
    }

    // 添加服务器套接字到epoll
    epoll_event event{};
    epoll_event events[MaxClientNum + 1]{}; // 事件数组
    event.events = EPOLLIN | EPOLLET;       // 边缘触发（高并发推荐）+ 可读
    event.data.fd = serverFd;
    if (epoll_ctl(epollFd, EPOLL_CTL_ADD, serverFd, &event) == -1) {
        perror("epoll_ctl add server fd failed");
        close(serverFd);
        close(epollFd);
        exit(EXIT_FAILURE);
    }
    printf("Epoll I/O Server is listening on Port %d\n", Port);

    ClientInfo client[MaxClientNum];
    char buffer[BufferSize];

    while (1) {
        // 等待事件触发
        int nfds = epoll_wait(epollFd, events, MaxClientNum + 1, 3000);
        if (nfds == -1) {
            // 处理信号中断（可重试）
            if (errno == EINTR) {
                printf("epoll_wait interrupted by signal, retry\n");
                continue;
            }
            perror("epoll_wait failed");
            break;
        } else if (nfds == 0) {
            printf("Timeout, no activity\n");
            continue;
        }

        // 遍历触发的事件
        for (int i = 0; i < nfds; ++i) {
            int curFd = events[i].data.fd;

            // 处理新连接
            if (curFd == serverFd) {
                sockaddr_in clientAddr{};
                socklen_t clientAddrLen = sizeof(clientAddr);
                int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientAddrLen);
                if (newClient == -1) {
                    perror("accept failed");
                    continue;
                }

                // 找client数组空位
                int clientIndex = -1;
                for (int j = 0; j < MaxClientNum; ++j) {
                    if (client[j].fd == -1) {
                        clientIndex = j;
                        break;
                    }
                }
                if (clientIndex == -1) {
                    printf("Too many clients, close new connection\n");
                    close(newClient);
                    continue;
                }

                // 设置客户端fd为非阻塞
                if (!setNonBlock(newClient)) {
                    close(newClient);
                    client[clientIndex].fd = -1;
                    continue;
                }

                // 填充客户端信息
                client[clientIndex].fd = newClient;
                inet_ntop(AF_INET, &clientAddr.sin_addr, client[clientIndex].ipStr, INET_ADDRSTRLEN);
                client[clientIndex].port = ntohs(clientAddr.sin_port);

                // 添加客户端fd到epoll（检查返回值）
                event.events = EPOLLIN | EPOLLET; // 边缘触发
                event.data.fd = newClient;
                if (epoll_ctl(epollFd, EPOLL_CTL_ADD, newClient, &event) == -1) {
                    perror("epoll_ctl add client fd failed");
                    close(newClient);
                    client[clientIndex].fd = -1;
                    continue;
                }

                printf("[%s:%d] has been connected.\n", client[clientIndex].ipStr, client[clientIndex].port);
            }
            // 处理客户端会话
            else {
                int clientIndex = -1;
                // 查找对应客户端索引
                for (int j = 0; j < MaxClientNum; ++j) {
                    if (client[j].fd == curFd) {
                        clientIndex = j;
                        break;
                    }
                }

                // 未找到客户端，清理epoll中的无效fd
                if (clientIndex == -1) {
                    printf("Can not find client for fd %d, remove from epoll\n", curFd);
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, curFd, nullptr); // DEL操作参数可传NULL
                    close(curFd);
                    continue;
                }

                // 清空缓冲区，避免脏数据
                memset(buffer, 0, BufferSize);
                ssize_t recvBytes = recv(curFd, buffer, BufferSize - 1, 0);
                if (recvBytes < 0) {
                    // 区分可重试错误和致命错误
                    if (errno == EINTR || errno == EAGAIN) {
                        continue; // 可重试，跳过
                    }
                    // 致命错误，关闭fd并清理
                    perror("recv failed");
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, curFd, nullptr);
                    close(curFd);
                    client[clientIndex].fd = -1;
                    printf("[%s:%d] recv error, disconnected\n", client[clientIndex].ipStr, client[clientIndex].port);
                    continue;
                }
                // 客户端正常断开
                else if (recvBytes == 0) {
                    printf("[%s:%d] has been disconnected.\n", client[clientIndex].ipStr, client[clientIndex].port);
                    epoll_ctl(epollFd, EPOLL_CTL_DEL, curFd, nullptr);
                    close(curFd);
                    client[clientIndex].fd = -1;
                }
                // 正常接收数据，回显
                else {
                    buffer[recvBytes] = '\0'; // 仅用于打印字符串
                    printf("[%s:%d]: %s\n", client[clientIndex].ipStr, client[clientIndex].port, buffer);

                    // 回显数据：用实际接收字节数，避免\0截断
                    ssize_t sendBytes = send(curFd, buffer, recvBytes, 0);
                    if (sendBytes < 0) {
                        perror("send failed");
                        epoll_ctl(epollFd, EPOLL_CTL_DEL, curFd, nullptr);
                        close(curFd);
                        client[clientIndex].fd = -1;
                        printf("[%s:%d] send error, disconnected\n", client[clientIndex].ipStr, client[clientIndex].port);
                    } else if (sendBytes != recvBytes) {
                        // 处理部分发送（非阻塞下常见）
                        printf("[%s:%d] send partial data: %zd/%zd\n",
                               client[clientIndex].ipStr, client[clientIndex].port, sendBytes, recvBytes);
                    }
                }
            }
        }
    }

    // 资源清理
    close(serverFd);
    close(epollFd);
    for (int i = 0; i < MaxClientNum; ++i) {
        if (client[i].fd != -1) {
            close(client[i].fd);
            client[i].fd = -1;
        }
    }

    std::cout << "End!\n";
    return 0;
}