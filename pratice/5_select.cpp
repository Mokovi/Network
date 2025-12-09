#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>
#include <cerrno> // 新增：处理errno

const uint16_t Port = 13145;
const uint16_t MaxClientNum = 10;
const uint16_t BufferSize = 1024;

struct ClientInfo {
    int fd = -1;
    char ipStr[INET_ADDRSTRLEN] = "";
    uint16_t port = 0;
};

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
    char buffer[BufferSize];

    fd_set readFds;
    int maxFd, activity;
    while(1) {
        // 清零，每次循环重新添加fd
        FD_ZERO(&readFds);

        // 添加服务器fd
        FD_SET(serverFd, &readFds);
        maxFd = serverFd;

        // 添加客户端fd
        for(int i = 0; i < MaxClientNum; i++) {
            if(client[i].fd == -1) continue;
            FD_SET(client[i].fd, &readFds);
            maxFd = client[i].fd > maxFd ? client[i].fd : maxFd;
        }

        // 设置超时时间（每次循环重新初始化，避免select修改的影响）
        timeval timeout{3, 0}; // C++11 聚合初始化，更简洁

        // 等待事件触发（检查返回值）
        activity = select(maxFd + 1, &readFds, NULL, NULL, &timeout);
        if (activity < 0) {
            // 处理信号中断（可重试）
            if (errno == EINTR) {
                std::cout << "select interrupted by signal, retry\n";
                continue;
            }
            perror("select failed");
            break;
        }
        else if (activity == 0) {
            std::cout << "Timeout.\n";
            continue;
        }

        // 处理服务器fd（新连接）
        if (FD_ISSET(serverFd, &readFds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientLen);
            if (newClient == -1) {
                perror("accept failed");
                continue; // 非致命错误，继续运行
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

            // 填充客户端信息
            client[clientIdx].fd = newClient;
            inet_ntop(AF_INET, &clientAddr.sin_addr, client[clientIdx].ipStr, INET_ADDRSTRLEN);
            client[clientIdx].port = ntohs(clientAddr.sin_port);
            printf("[%s:%d] connected\n", client[clientIdx].ipStr, client[clientIdx].port); // 新增：连接日志
        }

        // 处理客户端fd（消息/断开）
        for(int i = 0; i < MaxClientNum; i++) {
            if(client[i].fd == -1 || !FD_ISSET(client[i].fd, &readFds)) continue;

            // 清空缓冲区，避免残留旧数据
            memset(buffer, 0, BufferSize);
            ssize_t byteRecv = recv(client[i].fd, buffer, BufferSize - 1, 0);
            if (byteRecv < 0) {
                // 区分可重试错误和致命错误
                if (errno == EINTR || errno == EAGAIN) {
                    continue; // 可重试，不关闭fd
                }
                perror("recv failed");
                close(client[i].fd);
                client[i].fd = -1;
                continue;
            }
            else if (byteRecv == 0) {
                // 客户端正常断开
                printf("[%s:%d] disconnected\n", client[i].ipStr, client[i].port);
                close(client[i].fd);
                client[i].fd = -1;
            }
            else {
                buffer[byteRecv] = '\0'; // 仅用于打印字符串
                printf("[%s:%d]: %s\n", client[i].ipStr, client[i].port, buffer);
                
                // 回显数据：用实际接收的字节数，避免\0截断
                ssize_t byteSend = send(client[i].fd, buffer, byteRecv, 0);
                if (byteSend < 0) {
                    perror("send failed");
                    close(client[i].fd);
                    client[i].fd = -1;
                } else if (byteSend != byteRecv) {
                    // 处理部分发送（阻塞fd下少见，但工业场景需考虑）
                    printf("[%s:%d] send partial data: %zd/%zd\n", 
                           client[i].ipStr, client[i].port, byteSend, byteRecv);
                }
            }
        }
    }
    
    // 程序退出前关闭所有fd（规范资源释放）
    close(serverFd);
    for (int i = 0; i < MaxClientNum; i++) {
        if (client[i].fd != -1) {
            printf("[%s:%d] close connection on exit\n", client[i].ipStr, client[i].port);
            close(client[i].fd);
            client[i].fd = -1;
        }
    }

    std::cout << "END.\n";
    return 0;
}