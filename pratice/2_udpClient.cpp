#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>// 套接字信息结构体 sockaddr_in
#include <arpa/inet.h> //端序转换 ip转换函数
#include <unistd.h> //close()
#include <cstring>

int main() {
    const char ip[] = "192.168.8.132";
    const uint16_t port = 13145;

    //1.创建套接字
    int clientFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (clientFd == -1) {
        perror("Fail to socket.\n");
        exit(1);
    }

    //2.服务器信息结构体
    sockaddr_in serverAddr{};
    inet_pton(AF_INET, ip, &serverAddr.sin_addr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);

    //3.发送数据
    char buffer[] = "Hello Udp Server!";
    ssize_t byteSend = sendto(clientFd, buffer, strlen(buffer), 0, (sockaddr*)&serverAddr, sizeof(serverAddr));
    if (byteSend == -1) {
        perror("Fail to sendto.\n");
        close(clientFd);
        exit(1);
    }

    std::cout << "End!\n";
    return 0;
}
