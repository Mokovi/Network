#include <iostream>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h> // fcntl()
#include <cstring>

const uint16_t PORT = 13145;
const uint16_t MAXCLIENTNUM = 10;
const uint16_t BUFFERLEN = 512;

bool setNonBlock(int socketFd) {
    int flags = fcntl(socketFd, F_GETFL, 0);
    if (flags == -1) {
        perror("Fail to F_GETFL");
        return false;
    }
    if (fcntl(socketFd, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("Fail to F_SETFL");
        return false;
    }
    return true;
}

int main() {
    //1.服务器套接字
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("Socket.");
        exit(1);
    }
    //2. 设置端口复用
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    //3. 绑定服务器信息
    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    //4. 设置非阻塞模式
    if(!setNonBlock(serverFd)) {
        perror("Fail to set serverFd non-block.");
        exit(1);
    }
    //5. 监听
    listen(serverFd, 10);

    //6. 等待连接
    int clientFd[MAXCLIENTNUM];
    for (int i = 0; i < MAXCLIENTNUM; i++) clientFd[i] = -1;
    int currentClientNum = 0;
    sockaddr_in clientAddr[MAXCLIENTNUM]{};
    char clientIp[MAXCLIENTNUM][INET_ADDRSTRLEN]{};
    socklen_t clientAddrLen = sizeof(sockaddr_in);
    while (1) {
        if (currentClientNum < MAXCLIENTNUM) {
            clientFd[currentClientNum] = accept(serverFd, (sockaddr*)&clientAddr[currentClientNum], &clientAddrLen);
            if (clientFd[currentClientNum] != -1) {
                if (!setNonBlock(clientFd[currentClientNum])) {
                    perror("Fail to set non-block.");
                    exit(1);
                }
                inet_ntop(AF_INET, &clientAddr[currentClientNum].sin_addr.s_addr, clientIp[currentClientNum], INET_ADDRSTRLEN);
                printf("[%s:%d] has connected.\n", clientIp[currentClientNum], ntohs(clientAddr[currentClientNum].sin_port));
                currentClientNum++;
            }
        }
           
        //6.接收消息
        char buffer[BUFFERLEN];
        ssize_t byteRecv;
        for (int i = 0; i < currentClientNum; i++) {
            if (clientFd[i] < 0) continue;

            byteRecv = recv(clientFd[i], buffer, BUFFERLEN-1, 0);
            if (byteRecv > 0) {
                buffer[byteRecv] = '\0';
                printf("[%s:%d]: %s\n", clientIp[i], ntohs(clientAddr[i].sin_port), buffer);
                send(clientFd[i], buffer, strlen(buffer), 0);
            } 
            else if (byteRecv == 0) {
                close(clientFd[i]);
                printf("The client has disconnected.\n");
            }
        }
    }
    std::cout << "END.\n";
    return 0;

}