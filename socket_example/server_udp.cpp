// server_udp.cpp
// UDP 示例：Server 绑定端口等待客户端发送首个报文以记录对端地址。
// 读线程在 recvfrom() 阻塞读取来自任意对端的报文（记录第一个对端作为 peer）。
// 写线程在 std::getline() 阻塞读取用户输入并发送给已知 peer（若未知则提示）。
// 若接收到 "/quit" 则双方约定退出。
#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <atomic>
#include <thread>

// staic 使其作用域仅在当前文件
// constexpr 表示常量表达式，必须在编译期可计算出值。比const 更好
static constexpr const char* SERVER_IP = "192.168.0.140";
static constexpr short SERVER_PORT = 12345;
static constexpr size_t BUFF_SIZE = 1500; // UDP 常用报文上限

int main() {
    // 1. 创建套接字
    int socketfd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketfd == -1) {
        perror("socket.");
        exit(1);
    }
    //2.网络信息结构体绑定
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP);
    if (bind(socketfd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        perror("bind.");
        exit(1);
    }
    std::cout << "UDP server bound on port " << SERVER_PORT << "\n";

    std::atomic<bool> running(true);
    sockaddr_in peer_addr{};//记录接受到第一个客户端
    socklen_t peer_len = 0;
    bool have_peer = false;

    //读线程
    std::thread reader([&](){
        char buf[BUFF_SIZE];
        while (running) {
            sockaddr_in src{};
            socklen_t src_len = sizeof(src);
            ssize_t recv_num = recvfrom(socketfd, buf, sizeof(buf), 0, (sockaddr*)&src, &src_len);
            if (recv_num < 0) {
                perror("recv.");
                running.store(false);
                break;
            }
            std::string msg(buf, buf+recv_num);
            if (!have_peer) {
                peer_addr = src;
                peer_len = src_len;
                have_peer = true;
                auto peer_ip_str = inet_ntoa(src.sin_addr);
                std::cout << "Recorded peer: " << peer_ip_str << " : " << ntohs(src.sin_port) << std::endl;
            }
            std::cout << "[Peer] " << msg << std::flush;
            if (msg == "/quit") {
                std::cout << "Received quit command.Exiting...\n";
                running.store(false);
                break;
            }
        }
    });
    //写线程
    std::thread writer([&](){
        std::string line;
        while(running && std::getline(std::cin, line)) {
            if (!have_peer) {
                std::cout << "There is not a peer yet.\n";
                continue;
            }
            //line.push_back('\n');
            ssize_t send_num = sendto(socketfd, line.data(), line.size(), 0, (sockaddr*)&peer_addr, peer_len);
            if (send_num < 0) {
                perror("sendto.");
                running.store(false);
                break;
            }
            if (line == "/quit") {
                std::cout << "Send quit command.Exiting...\n";
                running.store(false);
                break;
            }
        }
    });

    if (writer.joinable()) writer.join();
    if (reader.joinable()) reader.join();

    return 0;
}