// client.cpp
// 简单 TCP client 示例：连接 server 后，读写在不同线程中阻塞进行。
// 使用方法：先启动 server（见 server.cpp），再启动 client 并连接到 server。

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <thread>

static constexpr const char* SERVER_IP = "192.168.0.140";
static constexpr short SERVER_PORT = 12345;

bool sendAll(int socketfd, char* buf, size_t bufSize) {
    size_t totalSent = 0;
    while (totalSent < bufSize) {
        ssize_t send_num = send(socketfd, buf, bufSize, 0);
        if (send_num < 0) return false;
        totalSent += send_num;
    }
    return true;
}

int main() {
    int clientFd = socket(AF_INET, SOCK_STREAM, 0);
    if (clientFd < 0) {
        perror("socket.");
        exit(1);
    }

    sockaddr_in serverAddr{};
    socklen_t serverAddrLen = sizeof(serverAddr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);
    serverAddr.sin_port = htons(SERVER_PORT);

    if (connect(clientFd, (sockaddr*)&serverAddr, serverAddrLen) < 0) {
        perror("fail to connect.\n");
        exit(1);
    }

    std::cout << "Connected to " << SERVER_IP << ":" << SERVER_PORT << "\n";

    std::atomic<bool> running(true);

    std::thread writer([&](){
        std::string line;
        while (running && std::getline(std::cin, line)) {
            if (line == "/quit") {
                std::cout << "Quit command seen. Exiting...\n";
                running.store(false);
                break;
            }
            line.push_back('\0');
            if (!sendAll(clientFd, line.data(), line.size())) {
                perror("send.");
                running.store(false);
                break;
            }
        }
        // 如果 stdin EOF（例如 ctrl+D），也关闭写端
        if (running.load() == true) {
            shutdown(clientFd, SHUT_WR);
            running.store(false);
        }
    });

    std::thread reader([&](){
        char buf[1024];
        while (running) {
            ssize_t rec_num = recv(clientFd, buf, sizeof(buf), 0);
            if (rec_num < 0) {
                perror("recv.");
                running.store(false);
                break;
            }
            else if (rec_num > 0) {
                std::cout << "Server[" << SERVER_IP << ":" << SERVER_PORT <<"]: " << buf << std::endl << std::flush;
            }
            else {
                std::cout << "Server[" << SERVER_IP << ":" << SERVER_PORT <<"] closed connection.\n";
                running.store(false);
                break;
            }
        }
    });

    if(writer.joinable()) writer.join();
    if(reader.joinable()) reader.join();
    return 0;
}