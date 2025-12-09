#include <iostream>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <unordered_map>
#include <memory>
#include <functional>
#include <mutex>


const uint16_t PORT = 13145;
const int MAX_EVENTS = 1024;    // epoll最大监听事件数
const int BUFFER_SIZE = 1024;   // 读写缓冲区大小
const int EPOLL_TIMEOUT = 100;  // epoll_wait超时时间(ms)

// 全局退出标记（线程安全，简化优雅退出）
volatile sig_atomic_t g_exit_flag = 0;

// 信号处理：捕获Ctrl+C，触发优雅退出
void handle_sigint(int sig) {
    (void)sig;
    g_exit_flag = 1;
    printf("\n[Reactor] Received SIGINT, preparing to exit...\n");
}

// 信号处理：捕获SIGPIPE，避免程序崩溃
void handle_sigpipe(int sig) {
    (void)sig;
}

// ====================== 1. Event Handler（事件处理器：抽象基类） ======================
// 所有事件处理器的基类，定义统一的事件处理接口
class EventHandler {
public:
    enum EventType {
        READ = EPOLLIN,   // 读事件
        WRITE = EPOLLOUT, // 写事件
        ERROR = EPOLLERR  // 错误事件
    };

    explicit EventHandler(int fd) : fd_(fd) {}
    virtual ~EventHandler() { close(fd_); }

    // 核心接口：处理事件（由子类实现）
    virtual void handle_event(int events) = 0;

    // 获取关联的文件描述符
    int get_fd() const { return fd_; }

protected:
    int fd_; // 关联的文件描述符（listen fd / client fd）
};

// ====================== 2. Event Demultiplexer（事件分发器：封装epoll） ======================
// 封装epoll的I/O多路复用操作，提供“注册/删除/等待事件”接口
class EventDemultiplexer {
public:
    EventDemultiplexer() {
        // 创建epoll实例
        epoll_fd_ = epoll_create1(0);
        if (epoll_fd_ == -1) {
            perror("[EventDemultiplexer] epoll_create1 failed");
            exit(EXIT_FAILURE);
        }
    }

    ~EventDemultiplexer() { close(epoll_fd_); }

    // 注册fd到epoll，关联事件和处理器
    bool add_event(int fd, int events, EventHandler* handler) {
        epoll_event ev{};
        ev.events = events | EPOLLET; // 边缘触发（高性能）
        ev.data.ptr = handler;        // 直接关联EventHandler指针（核心！）
        return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev) != -1;
    }

    // 从epoll中删除fd
    bool del_event(int fd) {
        return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) != -1;
    }

    // 等待事件触发，返回事件数组
    int wait_events(epoll_event* events, int max_events, int timeout) {
        return epoll_wait(epoll_fd_, events, max_events, timeout);
    }

private:
    int epoll_fd_; // epoll实例fd
};

// ====================== 3. Acceptor（接受器：处理新连接事件） ======================
// 继承EventHandler，专门处理listen fd的新连接事件
class Acceptor : public EventHandler {
public:
    // 构造函数：初始化监听fd
    Acceptor(uint16_t port, EventDemultiplexer* demultiplexer)
        : EventHandler(create_listen_fd(port)), demultiplexer_(demultiplexer) {
        // 将监听fd注册到epoll（监听读事件）
        if (!demultiplexer_->add_event(get_fd(), READ, this)) {
            perror("[Acceptor] add listen fd to epoll failed");
            exit(EXIT_FAILURE);
        }
        printf("[Acceptor] Listening on port %d (fd: %d)\n", port, get_fd());
    }

    // 实现EventHandler的核心接口：处理新连接事件
    void handle_event(int events) override {
        if (!(events & READ)) return; // 只处理读事件

        // 循环accept（非阻塞模式，处理所有待处理连接）
        while (true) {
            sockaddr_in client_addr{};
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(get_fd(), (sockaddr*)&client_addr, &client_len);
            if (client_fd == -1) {
                // 无新连接时退出循环（非阻塞accept的正常情况）
                if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                perror("[Acceptor] accept failed");
                break;
            }

            // 设置客户端fd为非阻塞
            set_non_block(client_fd);

            // 提取客户端IP和端口
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            uint16_t client_port = ntohs(client_addr.sin_port);
            printf("[Acceptor] New connection: %s:%d (fd: %d)\n", ip_str, client_port, client_fd);

            // 创建客户端事件处理器，注册到epoll
            auto client_handler = std::make_shared<ClientHandler>(client_fd, ip_str, client_port);
            if (!demultiplexer_->add_event(client_fd, EventHandler::READ, client_handler.get())) {
                perror("[Acceptor] add client fd to epoll failed");
                close(client_fd);
                continue;
            }

            // 保存ClientHandler，避免析构（Reactor中通过fd映射管理）
            Reactor::instance()->add_handler(client_fd, client_handler);
        }
    }

private:
    // 创建并初始化监听fd
    int create_listen_fd(uint16_t port) {
        // 1. 创建socket
        int listen_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (listen_fd == -1) {
            perror("[Acceptor] create socket failed");
            exit(EXIT_FAILURE);
        }

        // 2. 设置端口复用
        int opt = 1;
        setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        // 3. 设置非阻塞
        set_non_block(listen_fd);

        // 4. 绑定地址
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port);
        if (bind(listen_fd, (sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
            perror("[Acceptor] bind failed");
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

        // 5. 监听
        if (listen(listen_fd, 10) == -1) {
            perror("[Acceptor] listen failed");
            close(listen_fd);
            exit(EXIT_FAILURE);
        }

        return listen_fd;
    }

    // 设置fd为非阻塞
    void set_non_block(int fd) {
        int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    }

    EventDemultiplexer* demultiplexer_; // 事件分发器（关联epoll）
};

// ====================== 4. ClientHandler（客户端事件处理器：处理读写） ======================
// 继承EventHandler，处理客户端fd的读写事件
class ClientHandler : public EventHandler {
public:
    ClientHandler(int fd, const std::string& ip, uint16_t port)
        : EventHandler(fd), client_ip_(ip), client_port_(port) {}

    // 实现EventHandler的核心接口：处理客户端读写事件
    void handle_event(int events) override {
        if (events & ERROR) {
            // 处理错误事件，关闭连接
            handle_error();
            return;
        }

        if (events & READ) {
            // 处理读事件（接收客户端数据）
            handle_read();
        }
    }

private:
    // 处理读事件
    void handle_read() {
        char buffer[BUFFER_SIZE];
        memset(buffer, 0, BUFFER_SIZE);

        ssize_t recv_bytes = recv(get_fd(), buffer, BUFFER_SIZE - 1, 0);
        if (recv_bytes < 0) {
            // 可重试错误（非阻塞无数据）
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) return;
            // 致命错误
            perror("[ClientHandler] recv failed");
            close_connection();
            return;
        }

        if (recv_bytes == 0) {
            // 客户端正常断开
            printf("[ClientHandler] %s:%d disconnected (fd: %d)\n", client_ip_.c_str(), client_port_, get_fd());
            close_connection();
            return;
        }

        // 正常接收数据，回显
        buffer[recv_bytes] = '\0';
        printf("[ClientHandler] %s:%d (fd: %d) -> %s\n", client_ip_.c_str(), client_port_, get_fd(), buffer);
        send(get_fd(), buffer, recv_bytes, 0);
    }

    // 处理错误事件
    void handle_error() {
        perror("[ClientHandler] fd error");
        close_connection();
    }

    // 关闭连接，清理资源
    void close_connection() {
        // 从Reactor中删除处理器
        Reactor::instance()->del_handler(get_fd());
        // 从epoll中删除fd
        Reactor::instance()->get_demultiplexer()->del_event(get_fd());
        // 关闭fd（EventHandler析构时会自动关闭，此处显式清理）
        close(get_fd());
    }

    std::string client_ip_;   // 客户端IP
    uint16_t client_port_;    // 客户端端口
};

// ====================== 5. Reactor（反应器：核心事件循环） ======================
// 单例模式，管理所有EventHandler，驱动事件循环
class Reactor {
public:
    // 单例实例（懒汉模式，简化实现）
    static Reactor* instance() {
        static Reactor reactor;
        return &reactor;
    }

    // 初始化Reactor：创建Acceptor，准备事件循环
    void init(uint16_t port) {
        // 1. 创建事件分发器（epoll）
        demultiplexer_ = std::make_unique<EventDemultiplexer>();
        // 2. 创建Acceptor（处理新连接）
        acceptor_ = std::make_unique<Acceptor>(port, demultiplexer_.get());
        printf("[Reactor] Initialized successfully\n");
    }

    // 核心：事件循环
    void run() {
        epoll_event events[MAX_EVENTS];
        printf("[Reactor] Event loop started\n");

        while (!g_exit_flag) {
            // 1. 等待事件触发（通过EventDemultiplexer）
            int n = demultiplexer_->wait_events(events, MAX_EVENTS, EPOLL_TIMEOUT);
            if (n < 0) {
                if (errno == EINTR) continue; // 信号中断，继续循环
                perror("[Reactor] epoll_wait failed");
                break;
            }

            // 2. 分发事件到对应的EventHandler
            for (int i = 0; i < n; ++i) {
                EventHandler* handler = static_cast<EventHandler*>(events[i].data.ptr);
                if (handler) {
                    handler->handle_event(events[i].events);
                }
            }
        }

        printf("[Reactor] Event loop stopped\n");
    }

    // 添加EventHandler（保存指针，避免析构）
    void add_handler(int fd, std::shared_ptr<EventHandler> handler) {
        std::lock_guard<std::mutex> lock(mtx_);
        handlers_[fd] = handler;
    }

    // 删除EventHandler
    void del_handler(int fd) {
        std::lock_guard<std::mutex> lock(mtx_);
        handlers_.erase(fd);
    }

    // 获取事件分发器
    EventDemultiplexer* get_demultiplexer() {
        return demultiplexer_.get();
    }

private:
    Reactor() = default;
    ~Reactor() = default;

    std::unique_ptr<EventDemultiplexer> demultiplexer_; // 事件分发器
    std::unique_ptr<Acceptor> acceptor_;               // 接受器（处理新连接）
    std::unordered_map<int, std::shared_ptr<EventHandler>> handlers_; // fd -> 事件处理器
    std::mutex mtx_; // 保护handlers_的线程安全（本示例单线程，可省略，预留扩展）
};

// ====================== 主函数：程序入口 ======================
int main() {
    // 注册信号处理
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, handle_sigpipe);

    // 初始化Reactor
    Reactor::instance()->init(PORT);

    // 启动Reactor事件循环
    Reactor::instance()->run();

    printf("[Main] Server exited successfully\n");
    return 0;
}