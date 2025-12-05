#include <iostream>
#include <unistd.h> // close()
#include <arpa/inet.h> // htons() inet_pton()
#include <sys/socket.h> // socket()
#include <netinet/in.h> // struct sockaddr_in
#include <cstring>

int main() {
    const char serverIp[] = "192.168.8.132";
    const uint16_t port = 13145;

    //1.创建套接字
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    //2.绑定套接字信息结构体（可选）
    //3.连接
    sockaddr_in serverAddr{};
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    if (connect(clientFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("Fail to connect to server!\n");
        close(clientFd);
        exit(1);
    }
    
    

    while(1) {
        //5. 发送数据
        char buffer[1024];
        std::cin >> buffer;
        //strcpy(buffer,"Hello Server.This is client.");
        send(clientFd, buffer, strlen(buffer), 0);
        //4.接收数据
        int byteRecv = recv(clientFd, buffer, sizeof(buffer)-1, 0);
        if (byteRecv == -1) {
            perror("Fail to recv.");
            close(clientFd);
            exit(1);
        }
        buffer[byteRecv] = '\0';
        printf("[Server]: %s\n", buffer);
    }

    std::cout << "End!\n";
    return 0;
}