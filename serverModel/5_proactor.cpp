#include <iostream>
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
#include <liburing.h>

// ====================== 全局配置（可按需修改） ======================
const uint16_t SERVER_PORT = 13145;    // 服务器监听端口
const int MAX_QUEUE_DEPTH = 1024;      // io_uring 队列深度（异步操作最大并发数）
const int BUFFER_SIZE = 4096;          // 读写缓冲区大小
const int LISTEN_BACKLOG = 1024;       // listen 积压队列大小

// 全局退出标记（信号安全）
volatile sig_atomic_t g_exit_flag = 0;

// ====================== 信号处理函数 ======================
/**
 * @brief 捕获Ctrl+C信号，触发优雅退出
 * @param sig 信号值（SIGINT）
 */
void handle_sigint(int sig) {
    (void)sig;
    g_exit_flag = 1;
    printf("\n[Proactor] 捕获退出信号，准备优雅退出...\n");
}

/**
 * @brief 捕获SIGPIPE信号（避免客户端断开后写数据崩溃）
 * @param sig 信号值（SIGPIPE）
 */
void handle_sigpipe(int sig) {
    (void)sig;
}

// ====================== 前向声明 ======================
class ClientConn;                       // 客户端连接类
using ClientConnPtr = std::shared_ptr<ClientConn>; // 客户端连接智能指针

// ====================== 客户端连接类（智能指针管理） ======================
/**
 * @brief 客户端连接封装，包含fd、地址、读写缓冲区等
 * @note 使用std::shared_ptr管理，避免内存泄漏，适配io_uring异步操作
 */
class ClientConn {
public:
    /**
     * @brief 构造函数（新建连接）
     * @param fd 客户端fd
     * @param client_addr 客户端地址
     */
    ClientConn(int fd, const sockaddr_in& client_addr) 
        : fd_(fd), client_addr_(client_addr) {
        // 初始化读写缓冲区
        read_buf_ = std::make_unique<char[]>(BUFFER_SIZE);
        write_buf_ = std::make_unique<char[]>(BUFFER_SIZE);
        memset(read_buf_.get(), 0, BUFFER_SIZE);
        memset(write_buf_.get(), 0, BUFFER_SIZE);
        
        // 解析客户端IP和端口
        char ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, ip, INET_ADDRSTRLEN);
        client_ip_ = ip;
        client_port_ = ntohs(client_addr.sin_port);
        
        printf("[ClientConn] 新客户端连接：%s:%d (fd: %d)\n", 
               client_ip_.c_str(), client_port_, fd_);
    }

    /**
     * @brief 析构函数（关闭fd，释放缓冲区）
     */
    ~ClientConn() {
        if (fd_ >= 0) {
            close(fd_);
            printf("[ClientConn] 客户端%s:%d (fd: %d) 连接关闭\n", 
                   client_ip_.c_str(), client_port_, fd_);
        }
    }

    // ========== 成员访问接口 ==========
    int get_fd() const { return fd_; }
    char* get_read_buf() const { return read_buf_.get(); }
    char* get_write_buf() const { return write_buf_.get(); }
    const std::string& get_client_ip() const { return client_ip_; }
    uint16_t get_client_port() const { return client_port_; }

    // ========== 缓冲区操作 ==========
    void set_read_len(size_t len) { read_len_ = len; }
    size_t get_read_len() const { return read_len_; }

private:
    int fd_ = -1;                          // 客户端fd
    sockaddr_in client_addr_;              // 客户端地址结构
    std::string client_ip_;                // 客户端IP
    uint16_t client_port_ = 0;             // 客户端端口
    std::unique_ptr<char[]> read_buf_;     // 读缓冲区（智能指针自动释放）
    std::unique_ptr<char[]> write_buf_;    // 写缓冲区（智能指针自动释放）
    size_t read_len_ = 0;                  // 读数据长度
};

// ====================== Proactor核心类（基于io_uring） ======================
/**
 * @brief Proactor模型核心（管理io_uring，提交异步IO，处理完成事件）
 * @note Proactor核心逻辑：
 *       1. 应用层提交异步IO请求（accept/recv/send）到io_uring；
 *       2. 内核完成IO操作后，将完成事件放入io_uring的完成队列；
 *       3. 应用层轮询完成队列，处理IO完成结果（无需主动轮询IO状态）。
 */
class Proactor {
public:
    /**
     * @brief 构造函数（初始化io_uring）
     * @param port 服务器监听端口
     */
    Proactor(uint16_t port) : port_(port) {
        // 1. 初始化io_uring实例
        io_uring_params params{};
        int ret = io_uring_queue_init_params(MAX_QUEUE_DEPTH, &ring_, &params);
        if (ret < 0) {
            perror("[Proactor] io_uring_queue_init_params 失败");
            exit(EXIT_FAILURE);
        }

        // 2. 创建并初始化监听fd
        listen_fd_ = create_listen_fd();
        if (listen_fd_ < 0) {
            perror("[Proactor] 创建监听fd失败");
            exit(EXIT_FAILURE);
        }

        // 3. 提交第一个异步accept请求（Proactor启动的核心）
        submit_accept();

        printf("[Proactor] 初始化完成，监听端口：%d，io_uring队列深度：%d\n", 
               port_, MAX_QUEUE_DEPTH);
    }

    /**
     * @brief 析构函数（清理资源）
     */
    ~Proactor() {
        // 1. 关闭监听fd
        if (listen_fd_ >= 0) {
            close(listen_fd_);
        }

        // 2. 释放io_uring实例
        io_uring_queue_exit(&ring_);

        // 3. 清空客户端连接（智能指针自动释放）
        clients_.clear();

        printf("[Proactor] 资源已全部释放\n");
    }

    /**
     * @brief 启动Proactor事件循环（核心）
     */
    void run() {
        printf("[Proactor] 事件循环启动...\n");

        while (!g_exit_flag) {
            // 1. 等待io_uring完成事件（阻塞直到有事件完成）
            io_uring_cqe* cqe = nullptr;
            int ret = io_uring_wait_cqe(&ring_, &cqe);
            if (ret < 0 && errno != EINTR) {
                perror("[Proactor] io_uring_wait_cqe 失败");
                break;
            }
            if (g_exit_flag) break; // 收到退出信号，退出循环
            if (!cqe) continue;     // 无完成事件，继续

            // 2. 处理完成事件
            handle_cqe(cqe);

            // 3. 标记完成事件已处理（必须调用，否则完成队列溢出）
            io_uring_cqe_seen(&ring_, cqe);
        }

        printf("[Proactor] 事件循环停止\n");
    }

private:
    /**
     * @brief 创建并初始化监听fd
     * @return 成功返回监听fd，失败返回-1
     */
    int create_listen_fd() {
        // 1. 创建TCP socket
        int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
        if (fd < 0) {
            perror("[Proactor] socket 创建失败");
            return -1;
        }

        // 2. 设置端口复用
        int opt = 1;
        if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
            perror("[Proactor] setsockopt SO_REUSEADDR 失败");
            close(fd);
            return -1;
        }

        // 3. 绑定地址和端口
        sockaddr_in server_addr{};
        server_addr.sin_family = AF_INET;
        server_addr.sin_addr.s_addr = INADDR_ANY;
        server_addr.sin_port = htons(port_);
        if (bind(fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            perror("[Proactor] bind 失败");
            close(fd);
            return -1;
        }

        // 4. 开始监听
        if (listen(fd, LISTEN_BACKLOG) < 0) {
            perror("[Proactor] listen 失败");
            close(fd);
            return -1;
        }

        return fd;
    }

    /**
     * @brief 提交异步accept请求（Proactor核心：异步等待新连接）
     */
    void submit_accept() {
        // 1. 获取io_uring的提交队列项（SQE）
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            perror("[Proactor] io_uring_get_sqe 失败（accept）");
            return;
        }

        // 2. 准备异步accept操作
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        // 注意：io_uring_prep_accept 要求fd为非阻塞
        io_uring_prep_accept(sqe, listen_fd_, (sockaddr*)&client_addr, 
                             &client_addr_len, SOCK_NONBLOCK);

        // 3. 设置SQE的user_data（关联操作类型，这里标记为ACCEPT）
        enum OpType { OP_ACCEPT, OP_READ, OP_WRITE };
        sqe->user_data = reinterpret_cast<uint64_t>(OP_ACCEPT);

        // 4. 提交异步accept请求到内核
        int ret = io_uring_submit(&ring_);
        if (ret < 0) {
            perror("[Proactor] io_uring_submit accept 失败");
        }
    }

    /**
     * @brief 提交异步read请求（Proactor核心：异步读取客户端数据）
     * @param conn 客户端连接智能指针
     */
    void submit_read(const ClientConnPtr& conn) {
        if (!conn || conn->get_fd() < 0) return;

        // 1. 获取io_uring的提交队列项（SQE）
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            perror("[Proactor] io_uring_get_sqe 失败（read）");
            return;
        }

        // 2. 准备异步read操作
        io_uring_prep_recv(sqe, conn->get_fd(), conn->get_read_buf(), 
                           BUFFER_SIZE - 1, 0);

        // 3. 设置user_data：关联客户端连接指针（智能指针地址）
        sqe->user_data = reinterpret_cast<uint64_t>(conn.get());

        // 4. 提交异步read请求
        int ret = io_uring_submit(&ring_);
        if (ret < 0) {
            perror("[Proactor] io_uring_submit read 失败");
        }
    }

    /**
     * @brief 提交异步write请求（Proactor核心：异步回显客户端数据）
     * @param conn 客户端连接智能指针
     */
    void submit_write(const ClientConnPtr& conn) {
        if (!conn || conn->get_fd() < 0 || conn->get_read_len() == 0) return;

        // 1. 拷贝读缓冲区数据到写缓冲区
        memcpy(conn->get_write_buf(), conn->get_read_buf(), conn->get_read_len());

        // 2. 获取io_uring的提交队列项（SQE）
        io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
        if (!sqe) {
            perror("[Proactor] io_uring_get_sqe 失败（write）");
            return;
        }

        // 3. 准备异步write操作
        io_uring_prep_send(sqe, conn->get_fd(), conn->get_write_buf(), 
                           conn->get_read_len(), 0);

        // 4. 设置user_data：关联客户端连接指针
        sqe->user_data = reinterpret_cast<uint64_t>(conn.get());

        // 5. 提交异步write请求
        int ret = io_uring_submit(&ring_);
        if (ret < 0) {
            perror("[Proactor] io_uring_submit write 失败");
        }
    }

    /**
     * @brief 处理io_uring完成事件（Proactor核心：处理IO完成结果）
     * @param cqe 完成队列项
     */
    void handle_cqe(io_uring_cqe* cqe) {
        // 1. 获取完成事件的结果（返回值）和user_data
        int res = cqe->res;          // IO操作结果（>0：成功字节数，<0：错误）
        uint64_t user_data = cqe->user_data; // 关联的用户数据

        // 2. 处理错误情况
        if (res < 0) {
            fprintf(stderr, "[Proactor] IO操作失败，错误码：%d，原因：%s\n", 
                    -res, strerror(-res));
            // 如果是读写操作失败，清理客户端连接
            if (user_data != reinterpret_cast<uint64_t>(OP_ACCEPT)) {
                ClientConn* conn = reinterpret_cast<ClientConn*>(user_data);
                if (conn) {
                    clients_.erase(conn->get_fd());
                }
            }
            return;
        }

        // 3. 区分操作类型并处理
        enum OpType { OP_ACCEPT, OP_READ, OP_WRITE };
        if (user_data == reinterpret_cast<uint64_t>(OP_ACCEPT)) {
            // 3.1 处理异步accept完成（新连接）
            handle_accept_complete(res);
        } else if (auto conn = get_client_conn(reinterpret_cast<ClientConn*>(user_data))) {
            if (cqe->opcode == IORING_OP_RECV) {
                // 3.2 处理异步read完成（读取客户端数据）
                handle_read_complete(conn, res);
            } else if (cqe->opcode == IORING_OP_SEND) {
                // 3.3 处理异步write完成（回显数据完成）
                handle_write_complete(conn);
            }
        }
    }

    /**
     * @brief 处理异步accept完成事件
     * @param client_fd 新客户端fd（io_uring accept的返回值）
     */
    void handle_accept_complete(int client_fd) {
        // 1. 重新提交异步accept（保证持续监听新连接）
        submit_accept();

        // 2. 处理新客户端连接
        sockaddr_in client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        // 补充获取客户端地址（io_uring_prep_accept的addr参数可能未填充）
        getpeername(client_fd, (sockaddr*)&client_addr, &client_addr_len);

        // 3. 创建客户端连接智能指针，加入管理表
        auto conn = std::make_shared<ClientConn>(client_fd, client_addr);
        clients_[client_fd] = conn;

        // 4. 提交异步read请求（Proactor：异步读取客户端数据）
        submit_read(conn);
    }

    /**
     * @brief 处理异步read完成事件
     * @param conn 客户端连接智能指针
     * @param read_len 读取的字节数
     */
    void handle_read_complete(const ClientConnPtr& conn, size_t read_len) {
        if (read_len == 0) {
            // 客户端断开连接，清理资源
            printf("[Proactor] 客户端%s:%d (fd: %d) 断开连接\n", 
                   conn->get_client_ip().c_str(), conn->get_client_port(), conn->get_fd());
            clients_.erase(conn->get_fd());
            return;
        }

        // 1. 记录读取的数据长度
        conn->set_read_len(read_len);
        printf("[Proactor] 读取客户端%s:%d (fd: %d) 数据：%.*s\n", 
               conn->get_client_ip().c_str(), conn->get_client_port(), conn->get_fd(),
               static_cast<int>(read_len), conn->get_read_buf());

        // 2. 提交异步write请求（Proactor：异步回显数据）
        submit_write(conn);
    }

    /**
     * @brief 处理异步write完成事件
     * @param conn 客户端连接智能指针
     */
    void handle_write_complete(const ClientConnPtr& conn) {
        printf("[Proactor] 客户端%s:%d (fd: %d) 数据回显完成\n", 
               conn->get_client_ip().c_str(), conn->get_client_port(), conn->get_fd());

        // 重新提交异步read请求（继续监听客户端数据）
        submit_read(conn);
    }

    /**
     * @brief 根据ClientConn指针获取智能指针（防止野指针）
     * @param raw_ptr 裸指针
     * @return 智能指针（空表示未找到）
     */
    ClientConnPtr get_client_conn(ClientConn* raw_ptr) {
        if (!raw_ptr) return nullptr;
        auto it = clients_.find(raw_ptr->get_fd());
        if (it != clients_.end()) {
            return it->second;
        }
        return nullptr;
    }

private:
    uint16_t port_;                          // 服务器端口
    int listen_fd_ = -1;                     // 监听fd
    io_uring ring_;                          // io_uring核心实例
    std::unordered_map<int, ClientConnPtr> clients_; // 客户端连接管理表（智能指针）
};

// ====================== 主函数（程序入口） ======================
int main() {
    // 1. 注册信号处理函数（优雅退出）
    signal(SIGINT, handle_sigint);
    signal(SIGPIPE, handle_sigpipe);

    try {
        // 2. 创建Proactor实例（初始化io_uring，监听端口）
        Proactor proactor(SERVER_PORT);

        // 3. 启动Proactor事件循环
        proactor.run();
    } catch (const std::exception& e) {
        fprintf(stderr, "程序异常：%s\n", e.what());
        return EXIT_FAILURE;
    }

    printf("[Main] 服务器正常退出\n");
    return EXIT_SUCCESS;
}