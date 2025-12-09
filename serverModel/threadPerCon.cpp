#include <iostream>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <threads.h>
#include <cstring>

const uint16_t Port = 13145;
const uint16_t BufferSize = 1024;

struct ClientInfo{
    int fd;
    char buffer[BufferSize];
    char ipStr[INET_ADDRSTRLEN];
    uint16_t port;
    pthread_t thread;
};

void* clientComunication(void* args) {
    ClientInfo* client = (ClientInfo*)args;
    printf("[threadId:%lu][%s:%d] has been connected.\n", client->thread, client->ipStr, client->port);
    while(1) {
        int recvBytes = recv(client->fd, client->buffer, BufferSize-1, 0);
        if(recvBytes < 0) {
            perror("Recv.\n");
            break;
        }
        else if (recvBytes == 0) {
            printf("[%s:%d] has been disconnected.\n", client->ipStr, client->port);
            break;
        }
        else {
            client->buffer[recvBytes] = '\0';
            printf("[%s:%d]: %s\n", client->ipStr, client->port, client->buffer);
            send(client->fd, client->buffer, strlen(client->buffer), 0);
        }
    }
    close(client->fd);
    delete client;
    return NULL;
}


int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in serverAddr{};
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(Port);
    bind(serverFd, (sockaddr*)&serverAddr, sizeof(serverAddr));

    listen(serverFd, 10);

    printf("Thread-per-Connection Server listening on port %d\n", Port);

    while(1) {
        sockaddr_in clientAddr;
        socklen_t clientAddrLen = sizeof(clientAddr);
        int newClient = accept(serverFd, (sockaddr*)&clientAddr, &clientAddrLen);
        if (newClient < 0) {
            perror("Accept.\n");
            break;
        }
        ClientInfo* client = new ClientInfo;
        client->fd = newClient;
        inet_ntop(AF_INET, &clientAddr.sin_addr, client->ipStr, INET_ADDRSTRLEN);
        client->port = ntohs(clientAddr.sin_port);
        if (pthread_create(&client->thread, NULL, clientComunication, (void*)client) == -1) {
            perror("Fail to create thread.\n");
            close(client->fd);
            delete client;
            continue;
        }
        pthread_detach(client->thread);
    }

    close(serverFd);
    std::cout << "End!\n";
    return 0;
}