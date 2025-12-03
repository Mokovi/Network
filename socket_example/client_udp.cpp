// client_udp.cpp
// UDP 客户端示例：向 server 的 IP:PORT 发送首个报文以“通知”并让 server 记录客户端地址。
// 读线程在 recvfrom() 阻塞接收 server 的消息；写线程在 getline() 阻塞读取并发送到 server。
// 使用 /quit 进行优雅退出。
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <thread>

constexpr const char* SERVER_IP = "192.168.0.140"; //C++17 之后顶层 constexpr 默认就是内部链接
static constexpr short SERVER_PORT = 12345;
static constexpr size_t BUFF_SIZE = 1500; // UDP 常用报文上限

int main() {
    int socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketfd < 0) {
        perror("socket.");
        exit(1);
    }
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);

    std::atomic<bool> running(true);
    //写线程
    std::thread writer([&](){
        std::string line;
        while (running && std::getline(std::cin, line)) {
            line.push_back('\n');
            ssize_t send_num = sendto(socketfd, line.data(), line.size(), 0, (sockaddr*)&server_addr, sizeof(server_addr));
            if (send_num < 0) {
                perror("sendto.");
                running.store(false);
                break;
            }
            if (line == "/quit\n") {
                std::cout << "Quit command seen. Exiting...\n";
                running.store(false);
                break;
            }
        }
        running.store(false);
    });
    //读线程
    std::thread reader([&](){
        char buf[BUFF_SIZE];
        while (running) {
            sockaddr_in src{};
            socklen_t src_len = sizeof(src);
            ssize_t recv_num = recvfrom(socketfd, buf, sizeof(buf), 0, (sockaddr*)&src, &src_len);
            if (recv_num < 0) {
                perror("recvfrom.");
                running.store(false);
                break;
            }
            std::string msg(buf, recv_num);
            std::cout << "[Peer]: " << msg << std::endl;
            if (msg == "/quit") {
                std::cout << "Quit command seen. Exiting...\n";
                running.store(false);
                break;
            }
        }
    });

    if (writer.joinable()) writer.join();
    if (reader.joinable()) reader.join();
    return 0;
}