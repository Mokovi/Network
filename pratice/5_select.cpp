#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <unistd.h>
#include <cstring>

const uint16_t Port = 13145;
const uint16_t MaxClientNum = 10;
const uint16_t BufferSize = 1024;

int main() {
    //创建套接字
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);


    //设置端口复用
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //绑定
    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);
    bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    //监听
    listen(serverFd, 10);
    printf("Select I/O Server listening on port %d\n", Port);

    int clientFd[MaxClientNum];
    char clientIpStr[MaxClientNum][INET_ADDRSTRLEN];
    uint16_t clientPort[MaxClientNum];
    for(int i = 0; i < MaxClientNum; i++) clientFd[i] = -1;
    fd_set readFds;
    int maxFd, activity;
    char buffer[BufferSize];
    while(1) {
        //清零，每次循环重新往fd集合中添加
        FD_ZERO(&readFds);

        //添加服务器fd
        FD_SET(serverFd, &readFds);
        maxFd = serverFd;

        //添加客户端fd
        for(int i = 0; i < MaxClientNum; i++) {
            if(clientFd[i] == -1) continue;
            FD_SET(clientFd[i], &readFds);
            maxFd = clientFd[i] > maxFd ? clientFd[i] : maxFd;
        }

        //设置超时时间
        timeval timeout;
        timeout.tv_sec = 3;
        timeout.tv_usec = 0;

        //等待激活
        activity = select(maxFd+1, &readFds, NULL, NULL, &timeout);
        if (activity < 0) {
            perror("Select!");
            break;
        }
        else if (activity == 0) {
            std::cout << "Timeout.\n";
            continue;
        }
        

        //处理服务器fd
        if (FD_ISSET(serverFd, &readFds)) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientLen);
            int i = 0;
            if (newClient != -1) {
                for (i = 0; i < MaxClientNum; i++) {
                    if(clientFd[i] == -1) {
                        clientFd[i] = newClient;
                        inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientIpStr[i], INET_ADDRSTRLEN);
                        clientPort[i] = ntohs(clientAddr.sin_port);
                        printf("Client %d [%s:%d] connected\n", i, clientIpStr[i], clientPort[i]);
                        break;
                    }
                }
            }
            if (i == MaxClientNum) {
                printf("Too many clients, closing connection\n");
                close(newClient);
            }
        }

        //处理客户端fd
        for(int i = 0; i < MaxClientNum; i++) {
            if(clientFd[i] == -1) continue;
            if(FD_ISSET(clientFd[i], &readFds)) {
                int byteRecv = recv(clientFd[i], buffer, BufferSize-1, 0);
                if (byteRecv == 0) {
                    printf("Client %d [%s:%d] disconnected\n", i, clientIpStr[i], clientPort[i]);
                    close(clientFd[i]);
                    clientFd[i] = -1;//close不会将其置为-1，得手动置为-1
                }
                else if (byteRecv > 0) {
                    buffer[byteRecv] = '\0';
                    printf("Client %d [%s:%d]: %s\n", i, clientIpStr[i], clientPort[i], buffer);
                    //回显
                    send(clientFd[i], buffer, strlen(buffer), 0);
                }
                else {
                    perror("recv error");
                    close(clientFd[i]);
                    clientFd[i] = -1;
                }
            }
        }
    }
    
    close(serverFd);
    std::cout << "END.\n";
    return 0;
}