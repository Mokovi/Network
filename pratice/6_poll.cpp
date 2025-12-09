#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/poll.h>
#include <unistd.h>
#include <cstring>
#include <cerrno>

const uint16_t Port = 13145;
const uint16_t MaxClientNum = 10;
const uint16_t BufferSize = 1024;

struct ClientInfo {
    int fd = -1;
    char ipStr[INET_ADDRSTRLEN] = "";
    uint16_t port = 0;
};

// 整理fds数组，移除无效fd，更新nFds
void cleanFds(pollfd fds[], int& nFds, const int maxFds) {
    int validIdx = 1; // fds[0]是server，从1开始整理客户端fd
    for (int i = 1; i < nFds; ++i) {
        if (fds[i].fd != -1) {
            if (i != validIdx) {
                fds[validIdx] = fds[i];
                fds[i].fd = -1;
                fds[i].events = 0;
                fds[i].revents = 0;
            }
            validIdx++;
        }
    }
    nFds = validIdx;
}

int main() {
    // 1. 创建套接字（检查返回值）
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("socket failed");
        return -1;
    }

    // 2. 设置端口复用（检查返回值）
    int opt = 1;
    if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) == -1) {
        perror("setsockopt failed");
        close(serverFd);
        return -1;
    }

    // 3. 绑定（检查返回值）
    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);
    if (bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind failed");
        close(serverFd);
        return -1;
    }

    // 4. 监听（检查返回值）
    if (listen(serverFd, 10) == -1) {
        perror("listen failed");
        close(serverFd);
        return -1;
    }
    printf("Select I/O Server listening on port %d\n", Port);

    ClientInfo client[MaxClientNum];
    int activity;
    char buffer[BufferSize];
    const int maxFds = MaxClientNum + 1; // fds数组最大长度（server + 最大客户端数）
    pollfd fds[maxFds];
    
    // 初始化fds数组
    for(int i = 0; i < maxFds; i++) {
        fds[i].fd = -1;
        fds[i].events = 0;
        fds[i].revents = 0;
    }
    fds[0].fd = serverFd;
    fds[0].events = POLLIN;
    int nFds = 1;

    while(1) {
        // 等待事件触发（检查返回值）
        activity = poll(fds, nFds, 3000);
        if (activity < 0) {
            perror("poll failed");
            break;
        }
        else if (activity == 0) {
            std::cout << "Timeout.\n";
            continue;
        }

        // 5. 处理服务器fd（新连接）
        if (fds[0].revents & (POLLIN | POLLERR | POLLHUP)) {
            // 先处理异常事件
            if (fds[0].revents & (POLLERR | POLLHUP)) {
                perror("server fd error");
                break;
            }

            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientLen);
            if (newClient == -1) {
                perror("accept failed");
                continue; // 不是致命错误，继续运行
            }

            // 找client数组空位
            int clientIdx = -1;
            for (int i = 0; i < MaxClientNum; i++) {
                if (client[i].fd == -1) {
                    clientIdx = i;
                    break;
                }
            }
            if (clientIdx == -1) {
                printf("Too many clients, closing connection\n");
                close(newClient);
                continue;
            }

            // 检查fds数组是否已满
            if (nFds >= maxFds) {
                printf("fds array full, closing connection\n");
                close(newClient);
                client[clientIdx].fd = -1;
                continue;
            }

            // 填充客户端信息
            client[clientIdx].fd = newClient;
            inet_ntop(AF_INET, &clientAddr.sin_addr, client[clientIdx].ipStr, INET_ADDRSTRLEN);
            client[clientIdx].port = ntohs(clientAddr.sin_port);

            // 添加到fds数组
            fds[nFds].fd = newClient;
            fds[nFds].events = POLLIN;
            fds[nFds].revents = 0;
            nFds++;
            printf("[%s:%d] connected\n",  client[clientIdx].ipStr, client[clientIdx].port);
        }

        // 6. 处理客户端fd（消息/断开）
        for (int i = 1; i < nFds; i++) {
            if (fds[i].fd == -1) continue; // 跳过无效fd

            // 处理所有异常/可读事件
            if (fds[i].revents & (POLLIN | POLLERR | POLLHUP | POLLRDHUP)) {
                // 找对应的client索引
                int clientIdx = -1;
                for (int j = 0; j < MaxClientNum; j++) {
                    if (client[j].fd == fds[i].fd) {
                        clientIdx = j;
                        break;
                    }
                }
                // 未找到对应client，直接关闭fd
                if (clientIdx == -1) {
                    close(fds[i].fd);
                    fds[i].fd = -1;
                    continue;
                }

                // 异常事件：直接断开
                if (fds[i].revents & (POLLERR | POLLHUP | POLLRDHUP)) {
                    printf("[%s:%d] disconnected (error/hangup)\n",  client[clientIdx].ipStr, client[clientIdx].port);
                    close(client[clientIdx].fd);
                    client[clientIdx].fd = -1;
                    fds[i].fd = -1;
                    continue;
                }

                // 可读事件：接收数据
                memset(buffer, 0, BufferSize); // 清空缓冲区
                ssize_t byteRecv = recv(client[clientIdx].fd, buffer, BufferSize-1, 0);
                if (byteRecv < 0) {
                    perror("recv failed");
                    close(client[clientIdx].fd);
                    client[clientIdx].fd = -1;
                    fds[i].fd = -1;
                    continue;
                }
                else if (byteRecv == 0) {
                    // 客户端正常断开
                    printf("[%s:%d] disconnected\n",  client[clientIdx].ipStr, client[clientIdx].port);
                    close(client[clientIdx].fd);
                    client[clientIdx].fd = -1;
                    fds[i].fd = -1;
                }
                else {
                    buffer[byteRecv] = '\0'; // 保证字符串结束（仅用于打印）
                    printf("[%s:%d]: %s\n", client[clientIdx].ipStr, client[clientIdx].port, buffer);
                    // 回显数据：用实际接收的字节数，避免\0截断
                    ssize_t byteSend = send(client[clientIdx].fd, buffer, byteRecv, 0);
                    if (byteSend < 0) {
                        perror("send failed");
                        close(client[clientIdx].fd);
                        client[clientIdx].fd = -1;
                        fds[i].fd = -1;
                    }
                }
            }
        }

        // 7. 整理fds数组，移除无效fd，更新nFds
        cleanFds(fds, nFds, maxFds);
    }

    // 8. 程序退出前关闭所有fd
    close(serverFd);
    for (int i = 0; i < MaxClientNum; i++) {
        if (client[i].fd != -1) {
            close(client[i].fd);
            client[i].fd = -1;
        }
    }

    std::cout << "END.\n";
    return 0;
}