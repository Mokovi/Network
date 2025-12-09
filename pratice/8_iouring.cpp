#include <iostream>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <liburing.h>
#include <cstring>
#include <cstdlib>
#include <cerrno>
#include <fcntl.h>

const uint16_t Port = 13145;
const uint16_t MaxClientNum = 10;
const uint16_t BufferSize = 1024;
const uint16_t QueueDepth = 32;

enum OpType{
    OP_ACCEPT = 1,
    OP_RECV = 2,
    OP_SEND = 3
};

struct ClientInfo{
    int clientFd = -1;
    char ipStr[INET_ADDRSTRLEN] = "";
    uint16_t port = 0;
    char buffer[BufferSize];
};

typedef struct {
    OpType type;
    ClientInfo* client;
    sockaddr_in addr;
    socklen_t addr_len;
    char* buf;
    size_t buf_len;
}IoData;

int set_nonblock(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags == -1) return -1;
    return fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd == -1) {
        perror("Server socket");
        exit(1);
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);

    if (bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr)) == -1) {
        perror("bind");
        close(serverFd);
        exit(1);
    }

    if (listen(serverFd, 128) == -1) {
        perror("listen");
        close(serverFd);
        exit(1);
    }

    if (set_nonblock(serverFd) < 0) {
        perror("set_nonblock");
        close(serverFd);
        return 1;
    }

    printf("io_uring Server listening on port %d\n", Port);

    // 初始化 io_uring
    io_uring ring;
    if (io_uring_queue_init(QueueDepth, &ring, 0) < 0) {
        perror("io_uring_queue_init");
        close(serverFd);
        exit(1);
    }

    // accept 异步请求
    IoData* initialAccept = new IoData();
    initialAccept->type = OP_ACCEPT;
    initialAccept->client = nullptr;
    initialAccept->addr_len = sizeof(initialAccept->addr);

    io_uring_sqe * sqe = io_uring_get_sqe(&ring);
    if (!sqe) {
        fprintf(stderr, "get sqe failed\n");
        delete initialAccept;
        io_uring_queue_exit(&ring);
        close(serverFd);
        return 1;
    }
    io_uring_prep_accept(sqe, serverFd, (sockaddr*)&initialAccept->addr, &initialAccept->addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
    io_uring_sqe_set_data(sqe, initialAccept);
    io_uring_submit(&ring);

    ClientInfo client[MaxClientNum];
    while (1) {
        io_uring_cqe *cqe = nullptr;
        int ret = io_uring_wait_cqe(&ring, &cqe);
        if (ret < 0) {
            fprintf(stderr, "io_uring_wait_cqe error: %s\n", strerror(-ret));
            break;
        }

        IoData* ioData = (IoData*)io_uring_cqe_get_data(cqe);
        int res = cqe->res;

        if (!ioData) {
            // unexpected but handle gracefully
            io_uring_cqe_seen(&ring, cqe);
            continue;
        }

        if (ioData->type == OP_ACCEPT) {
            // accept completed
            if (res < 0) {
                fprintf(stderr, "accept failed: %s\n", strerror(-res));
                // re-arm accept
                IoData* nextAccept = new IoData();
                nextAccept->type = OP_ACCEPT;
                nextAccept->client = nullptr;
                nextAccept->addr_len = sizeof(nextAccept->addr);
                io_uring_sqe* nsqe = io_uring_get_sqe(&ring);
                io_uring_prep_accept(nsqe, serverFd, (sockaddr*)&nextAccept->addr, &nextAccept->addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
                io_uring_sqe_set_data(nsqe, nextAccept);
                io_uring_submit(&ring);
                delete ioData;
                io_uring_cqe_seen(&ring, cqe);
                continue;
            }

            int newClientFd = res;
            // find slot
            int idx = -1;
            for (int i = 0; i < MaxClientNum; ++i) {
                if (client[i].clientFd == -1) {
                    idx = i;
                    break;
                }
            }
            if (idx == -1) {
                // too many clients
                fprintf(stderr, "Too many clients, closing fd=%d\n", newClientFd);
                close(newClientFd);
            } else {
                client[idx].clientFd = newClientFd;
                inet_ntop(AF_INET, &ioData->addr.sin_addr, client[idx].ipStr, INET_ADDRSTRLEN);
                client[idx].port = ntohs(ioData->addr.sin_port);
                printf("[%s:%d] has been connected.\n", client[idx].ipStr, client[idx].port);

                // submit recv for this client (IoData per request)
                IoData* recvIo = new IoData();
                recvIo->type = OP_RECV;
                recvIo->client = &client[idx];
                io_uring_sqe* rsqe = io_uring_get_sqe(&ring);
                io_uring_prep_recv(rsqe, client[idx].clientFd, client[idx].buffer, BufferSize - 1, 0);
                io_uring_sqe_set_data(rsqe, recvIo);
            }

            // re-arm accept (new IoData)
            IoData* nextAccept = new IoData();
            nextAccept->type = OP_ACCEPT;
            nextAccept->client = nullptr;
            nextAccept->addr_len = sizeof(nextAccept->addr);
            io_uring_sqe* asqe = io_uring_get_sqe(&ring);
            io_uring_prep_accept(asqe, serverFd, (sockaddr*)&nextAccept->addr, &nextAccept->addr_len, SOCK_NONBLOCK | SOCK_CLOEXEC);
            io_uring_sqe_set_data(asqe, nextAccept);

            io_uring_submit(&ring);
            // free this accept IoData
            delete ioData;
        }
        else if (ioData->type == OP_RECV) {
            ClientInfo* cur = ioData->client;
            if (!cur) {
                delete ioData;
                io_uring_cqe_seen(&ring, cqe);
                continue;
            }
            if (res < 0) {
                fprintf(stderr, "recv failed for fd=%d: %s\n", cur->clientFd, strerror(-res));
                close(cur->clientFd);
                cur->clientFd = -1;
                delete ioData;
                io_uring_cqe_seen(&ring, cqe);
                continue;
            } else if (res == 0) {
                printf("[%s:%d] has been disconnected.\n", cur->ipStr, cur->port);
                close(cur->clientFd);
                cur->clientFd = -1;
                delete ioData;
                io_uring_cqe_seen(&ring, cqe);
                continue;
            } else {
                int n = res;
                cur->buffer[n] = '\0';
                printf("[%s:%d]: %s\n", cur->ipStr, cur->port, cur->buffer);

                // prepare send: copy data
                IoData* sendIo = new IoData();
                sendIo->type = OP_SEND;
                sendIo->client = cur;
                sendIo->buf_len = n;
                sendIo->buf = (char*)malloc(n);
                if (!sendIo->buf) {
                    fprintf(stderr, "malloc failed for send buffer\n");
                    delete sendIo;
                } else {
                    memcpy(sendIo->buf, cur->buffer, n);
                    io_uring_sqe* wsqe = io_uring_get_sqe(&ring);
                    io_uring_prep_send(wsqe, cur->clientFd, sendIo->buf, sendIo->buf_len, 0);
                    io_uring_sqe_set_data(wsqe, sendIo);
                }

                // re-arm recv for this client (new IoData)
                IoData* nextRecv = new IoData();
                nextRecv->type = OP_RECV;
                nextRecv->client = cur;
                io_uring_sqe* rsqe2 = io_uring_get_sqe(&ring);
                io_uring_prep_recv(rsqe2, cur->clientFd, cur->buffer, BufferSize - 1, 0);
                io_uring_sqe_set_data(rsqe2, nextRecv);

                io_uring_submit(&ring);
                delete ioData;
            }
        }
        else if (ioData->type == OP_SEND) {
            // send completion: free send buffer and IoData
            if (ioData->buf) free(ioData->buf);
            delete ioData;
            // nothing else to do
        } else {
            // unknown op
            delete ioData;
        }

        io_uring_cqe_seen(&ring, cqe);
    }

    io_uring_queue_exit(&ring);
    close(serverFd);
    std::cout << "End!\n";
    return 0;
}
