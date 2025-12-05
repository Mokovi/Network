#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <cstring>

int main() {
    const char serverIp[] = "192.168.8.132";
    const uint16_t port = 13145;

    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd == -1) {
        perror("socket");
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    inet_pton(AF_INET, serverIp, &serverAddr.sin_addr);

    if (connect(clientFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        perror("connect");
        return 1;
    }

    char buffer[1024];

    while (true) {
        std::cin >> buffer;
        send(clientFd, buffer, strlen(buffer), 0);

        int n = recv(clientFd, buffer, sizeof(buffer) - 1, 0);
        if (n <= 0) {
            perror("recv");
            break;
        }
        buffer[n] = '\0';
        printf("[Server]: %s\n", buffer);
    }

    close(clientFd);
    return 0;
}
