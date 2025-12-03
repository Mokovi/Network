// server_tcp.cpp
// 简单 TCP server 示例：等待客户端连接，建立后用两个线程进行阻塞式双向通信。
// 读线程在 recv() 阻塞读取来自对端的数据并打印到 stdout。
// 写线程在 std::getline() 阻塞读取用户输入并发送给对端。
// 输入 /quit 会关闭连接并退出程序。

#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <thread>
#include <string>

static constexpr short LISTEN_PORT = 12345;
static constexpr const char* SERVER_IP = "0.0.0.0";
static constexpr int BACKLOG = 1;

/**
 * @brief 避免短写导致数据没发完.
 * @details 在 Linux 下，send() 会把用户缓冲区里的数据 尽量拷贝到内核 socket 缓冲区。
如果内核缓冲区满了，或者数据太大，send() 可能只拷贝了一部分，然后就返回了 实际成功写入的字节数。
因此，send() 的返回值 ≤ 你请求发送的长度，这就造成了所谓的“短写”（short write）。
这不是错误，也不代表网络坏了，只是正常情况。
 */
bool sendAll(int& socketFd, const char * buf, size_t bufSize) {
    size_t totalSent = 0;
    while (totalSent < bufSize) {
        ssize_t sentNum = send(socketFd, buf, bufSize, 0);
        if (sentNum < 0) {
            return false;
        }
        totalSent += sentNum;
    }
    return true;
}

int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("serverfd.");
        exit(1);
    }

    sockaddr_in serverAddr{};
    socklen_t serverAddrLen = sizeof(serverAddr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(LISTEN_PORT);
    serverAddr.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (bind(serverFd, (sockaddr*)&serverAddr, serverAddrLen) < 0) {
        perror("bind.");
        exit(1);
    }

    if (listen(serverFd, BACKLOG) < 0) {
        perror("listen.");
        exit(1);
    }
    std::cout << "Server has started to listen.\n";

    int clientFd;
    sockaddr_in clientAddr{};
    socklen_t clientAddrLen = sizeof(clientAddr);
    if ((clientFd = accept(serverFd, (sockaddr*)&clientAddr, &clientAddrLen)) < 0) {
        perror("accpet.");
        exit(1);
    }
    auto clientStr = "Client[" + std::string(inet_ntoa(clientAddr.sin_addr)) 
                            + ":" + std::to_string(ntohs(clientAddr.sin_port)) + "]";
    std::cout <<  clientStr <<" has connected!\n";
    
    std::atomic<bool> running(true);

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
                buf[rec_num] = '\0';
                std::cout << clientStr << ": " << buf << std::endl << std::flush;//flush 确保能够及时观察到
            }
            else {
                 std::cout << "\n" << clientStr <<" closed connection.\n";
                running.store(false);
            }
        }
    });
    
    std::thread writer([&](){
        std::string line;
        while (running && std::getline(std::cin, line)) {
            if(line == "/quit") {
                std::cout << "Quit command seen. Exiting...\n";
                running.store(false);
                break;
            }
            line.push_back('\0');
            if(!sendAll(clientFd, line.data(), line.size())) {
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

    if(writer.joinable()) writer.join();
    if(reader.joinable()) reader.join();
    return 0;
}