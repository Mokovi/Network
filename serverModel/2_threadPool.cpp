#include <iostream>
#include <mutex>
#include <thread>
#include <queue>
#include <functional>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

const uint16_t Port = 13145;
const uint16_t BufferSize = 1024;

struct ClientInfo {
    int fd;
    sockaddr_in clientAddr;
};

class ThreadPool {
private:
    std::mutex mutex;
    std::atomic<bool> stopFlag;
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> taskQueue;
    std::condition_variable notFull;
    std::condition_variable notEmpty;
    size_t threadsSize, maxQueueSize;
public:
    ThreadPool(size_t threadsSize, size_t maxQueueSize = 1024) 
        : threadsSize(threadsSize), maxQueueSize(maxQueueSize),stopFlag(false) {
        for (size_t i = 0; i < threadsSize; ++i) {
            workers.emplace_back([this]{this->workLoop();});
        }
        printf("ThreadPool init completed.\n");
    }
    ~ThreadPool() {
        shudown();
    }

    void submit(std::function<void()> task) {
        std::unique_lock<std::mutex> lock(mutex);
        notFull.wait(lock,[this]{return stopFlag || taskQueue.size() < maxQueueSize;});
        if (stopFlag) return;
        taskQueue.push(std::move(task));
        notEmpty.notify_one();
    }

    void shudown() {
        {
            std::lock_guard<std::mutex> lock(mutex);
            if (stopFlag) return;
            stopFlag.store(true);
        }
        notFull.notify_all();
        notEmpty.notify_all();
        for (auto& t : workers) {
            if(t.joinable()) {
                t.join();
            }
        }
    }

private:
    void workLoop() {
        while(true) {
            std::function<void()> task;
            {
                std::unique_lock<std::mutex> lock(mutex);
                notEmpty.wait(lock, [this]{return stopFlag || !taskQueue.empty();});
                if (stopFlag && taskQueue.empty()) break;
                task = std::move(taskQueue.front());
                taskQueue.pop();
                notFull.notify_one();
            }
            try {
                task();
            }
            catch(const std::exception& e) {
                std::cerr << e.what() << '\n';
            }
        }
    }
};

void handleClientComm(int clientFd, sockaddr_in clientAddr) {
    char ipStr[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &clientAddr.sin_addr, ipStr, INET_ADDRSTRLEN);
    uint16_t port = ntohs(clientAddr.sin_port);
    printf("[%s:%d] has been connected.\n", ipStr, port);
    while(true) {
        char buffer[BufferSize];
        int byteRec = recv(clientFd, buffer, BufferSize-1, 0);
        if (byteRec < 0) {
            perror("Fail to recv.");
            continue;
        }
        else if (byteRec == 0) {
            printf("[%s:%d] has been disconnected.\n", ipStr, port);
            close(clientFd);
            break;
        }
        else {
            buffer[byteRec] = '\0';
            printf("[%s:%d]: %s\n", ipStr, port, buffer);
            if (send(clientFd, buffer, byteRec, 0) == -1) {
                perror("Fail to send.");
                continue;
            }

        }
    }
}




int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        perror("Server socket.");
        exit(1);
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);

    if(bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind.");
        close(serverFd);
        exit(1);
    }

    if (listen(serverFd, 10) == -1) {
        perror("listen.");
        close(serverFd);
        exit(1);
    }
    printf("Server is listening on port %d.\n", Port);

    //创建线程池
     size_t numCores = std::thread::hardware_concurrency();
    if (numCores == 0) numCores = 4;
    size_t poolSize = numCores * 2;
    //ThreadPool threadPool(poolSize, 512);
    ThreadPool threadPool(2, 512);

    while(true) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientAddrLen);
        if (newClient < 0) {
            perror("Accept.");
            continue;
        }
        threadPool.submit([newClient, clientAddr]{
            handleClientComm(newClient, clientAddr);
        });
    }

    threadPool.shudown();
    close(serverFd);
    std::cout << "End.\n";
    return 0;
}