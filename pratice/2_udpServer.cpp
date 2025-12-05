#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h> // 套接字信息结构体 sockaddr_in
#include <arpa/inet.h> //端序转换，ip转换函数
#include <unistd.h> // close函数 关闭文件描述符




int main() {

    const char ip_str[] = "192.168.8.132";
    const uint16_t port = 13145;

    //1.创建套接字
    int serverFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (serverFd == -1) {
        perror("Fail to get socket.\n");
        exit(EXIT_FAILURE);
    }

    //2.创建服务器信息结构体
    sockaddr_in serverAddr{}; //C++11 的“零初始化（zero-initialization）”
    inet_pton(AF_INET, ip_str, &serverAddr.sin_addr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    //3.绑定
    if (bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1 ) {
        perror("Fail to bind.\n");
        close(serverFd);
        exit(1);
    }
    std::cout << "Udp server is listening on [" << ip_str << ":" << port << "]\n";

    //4.等待接收数据
    char buffer[1024];
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    ssize_t byteReceived = recvfrom(serverFd, buffer, sizeof(buffer)-1, 0, (sockaddr*)&clientAddr, &clientAddrLen);
    if (byteReceived == -1) {
        perror("Fail to recvfrom.\n");
        close(serverFd);
        exit(1);
    }
    else {
        char clientIpStr[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &clientAddr.sin_addr.s_addr, clientIpStr, INET_ADDRSTRLEN);
        buffer[1024] = '\0';
        printf("[%s:%d] : %s \n", clientIpStr, clientAddr.sin_port, buffer);
    }

    close(serverFd);
    std::cout << "End!\n";
    return 0;
}
