#include <iostream>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/poll.h>
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
    int activity;
    char buffer[BufferSize];
    pollfd fds[MaxClientNum+1];
    for(int i = 0; i < MaxClientNum+1; i++) fds[i].fd = -1;
    fds[0].fd = serverFd;
    fds[0].events = POLLIN;
    int nFds = 1;

    while(1) {
        //等待激活
        activity = poll(fds, nFds, 3000);
        if (activity < 0) {
            perror("poll!");
            break;
        }
        else if (activity == 0) {
            std::cout << "Timeout.\n";
            continue;
        }

        //处理服务器fd
        if (fds[0].revents & POLLIN) {
            sockaddr_in clientAddr{};
            socklen_t clientLen = sizeof(clientAddr);
            int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientLen);
            int i = 0;
            if (newClient != -1) {
                for (i = 0; i < MaxClientNum; i++) {
                    if(clientFd[i] == -1) {
                        clientFd[i] = newClient;
                        inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientIpStr[i], sizeof(sockaddr_in));
                        clientPort[i] = ntohs(clientAddr.sin_port);
                        fds[nFds].fd = clientFd[i];
                        fds[nFds].events = POLLIN;
                        nFds++;
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
            if(fds[i+1].revents & POLLIN) {
                int byteRecv = recv(clientFd[i], buffer, BufferSize-1, 0);
                if (byteRecv == 0) {
                    printf("Client %d [%s:%d] disconnected\n", i, clientIpStr[i], clientPort[i]);
                    close(clientFd[i]);
                    clientFd[i] = -1;//close不会将其置为-1，得手动置为-1
                    fds[i].fd = -1;
                }
                else if (byteRecv > 0) {
                    buffer[byteRecv] = '\0';
                    printf("Client %d [%s:%d]: %s\n", i, clientIpStr[i], clientPort[i], buffer);
                    //回显
                    send(clientFd[i], buffer, strlen(buffer), 0);
                }
            }
        }
    }
    
    close(serverFd);
    std::cout << "END.\n";
    return 0;
}