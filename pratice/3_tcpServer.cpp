#include <iostream>
#include <unistd.h> // close()
#include <arpa/inet.h> // htons() inet_pton()
#include <sys/socket.h> // socket()
#include <netinet/in.h> // struct sockaddr_in
#include <cstring>



int main() {
    const char serverIp[] = "192.168.8.132";
    const uint16_t port = 13145;
    const int maxClientNum = 1;

    //1.创建套接字
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    //2. 绑定信息结构体
    sockaddr_in serverAddr{};
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr));
    //3. 监听
    listen(serverFd, maxClientNum);
    std::cout << "Tcp server is listening on port " << port << "!\n";
    //4. 等待连接
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    int clientFd = accept(serverFd, (sockaddr*)&clientAddr, &clientAddrLen);
    char clientIpStr[INET_ADDRSTRLEN];
    uint16_t clientPort = ntohs(clientAddr.sin_port);
    inet_ntop(AF_INET, &clientAddr.sin_addr, clientIpStr, INET_ADDRSTRLEN);
    printf("[%s:%d] has connected.\n", clientIpStr, clientPort);
    //5. 发送数据
    char buffer[1024] = "Hello.";
    send(clientFd, buffer, strlen(buffer), 0);
    //6. 接收数据
    int byteRecv = recv(clientFd, buffer, sizeof(buffer)-1, 0);
    buffer[byteRecv] = '\0';
    printf("[%s:%d]: %s\n", clientIpStr, clientPort, buffer);

    std::cout << "End!\n";
    return 0;
}